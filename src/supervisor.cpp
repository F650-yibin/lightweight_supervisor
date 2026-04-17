#include "supervisor.hpp"
#include "child_status_json.hpp"
#include "ipc_client.hpp"

#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace supervisor {

namespace {
struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

std::string now_string() {
  auto now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%F %T");
  return oss.str();
}

std::vector<std::string> split_ws(const std::string &s) {
  std::istringstream iss(s);
  std::vector<std::string> out;
  std::string tok;
  while (iss >> tok)
    out.push_back(tok);
  return out;
}
} // namespace

Supervisor::Supervisor() = default;

Supervisor::~Supervisor() {
  try {
    enter_safe_mode();
  } catch (...) {
  }

  try {
    stop_all();
  } catch (...) {
  }

  try {
    stop_background_threads();
  } catch (...) {
  }

  try {
    stop_control_interface();
  } catch (...) {
  }
}

void Supervisor::add_process(ProcessSpec spec) {
  std::lock_guard<std::mutex> lk(mu_);
  if (spec.name.empty())
    throw Error("empty process name");
  if (spec.argv.empty())
    throw Error("empty argv for " + spec.name);
  if (spec.control_sock.empty())
    throw Error("empty control_sock for " + spec.name);
  if (specs_.count(spec.name))
    throw Error("duplicate process: " + spec.name);

  runtime_[spec.name] = ProcessRuntime{};
  specs_[spec.name] = std::move(spec);
}

void Supervisor::start_control_interface(const std::string &sock_path) {
  std::filesystem::create_directories("/tmp/robot");
  control_server_ = std::make_unique<ControlServer>(
    sock_path, [this](const std::string &cmdline) { return this->handle_command_(cmdline); });
  control_server_->start();
  log_event_("INFO", "-", "control interface started: " + sock_path);
}

void Supervisor::stop_control_interface() {
  if (control_server_) {
    control_server_->stop();
    control_server_.reset();
  }
}

void Supervisor::run_background_threads() {
  std::lock_guard<std::mutex> lk(mu_);
  if (bg_running_)
    return;
  bg_running_ = true;
  monitor_thread_ = std::thread([this] { monitor_children_loop_(); });
  poll_thread_ = std::thread([this] { poll_status_loop_(); });
}

void Supervisor::stop_background_threads() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    bg_running_ = false;
  }
  cv_.notify_all();
  if (monitor_thread_.joinable())
    monitor_thread_.join();
  if (poll_thread_.joinable())
    poll_thread_.join();
}

void Supervisor::start_all() {
  std::vector<std::string> order;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (started_)
      return;
    manager_state_ = "STARTING";
    order = topo_order_();
  }

  std::vector<std::string> started_names;
  try {
    for (const auto &name : order) {
      start_one_(name);
      started_names.push_back(name);
    }

    {
      std::lock_guard<std::mutex> lk(mu_);
      start_order_ = started_names;
      started_ = true;
      manager_state_ = "RUNNING";
    }

    set_mode_all("SAFE");
    log_event_("INFO", "-", "all processes started");
  } catch (...) {
    for (auto it = started_names.rbegin(); it != started_names.rend(); ++it) {
      try {
        stop_one_(*it);
      } catch (...) {
      }
    }
    {
      std::lock_guard<std::mutex> lk(mu_);
      manager_state_ = "ERROR";
      started_ = false;
    }
    throw;
  }
}

void Supervisor::stop_all() {
  std::vector<std::string> order;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!started_)
      return;
    manager_state_ = "STOPPING";
    order = start_order_;
  }

  enter_safe_mode();
  std::reverse(order.begin(), order.end());

  for (const auto &name : order) {
    try {
      stop_one_(name);
    } catch (const std::exception &e) {
      log_event_("ERROR", name, std::string("stop failed: ") + e.what());
    }
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    started_ = false;
    manager_mode_ = "STOPPED";
    manager_state_ = "STOPPED";
  }
  log_event_("INFO", "-", "all processes stopped");
}

void Supervisor::start_proc(const std::string &name) { start_one_(name); }

void Supervisor::stop_proc(const std::string &name) { stop_one_(name); }

void Supervisor::restart_proc(const std::string &name) { restart_one_(name); }

void Supervisor::set_mode_all(const std::string &mode) {
  validate_mode_(mode);
  apply_mode_ordered_(mode);
  {
    std::lock_guard<std::mutex> lk(mu_);
    manager_mode_ = mode;
  }
  log_event_("INFO", "-", "mode changed to " + mode);
}

void Supervisor::enter_safe_mode() {
  try {
    apply_mode_ordered_("SAFE");
    std::lock_guard<std::mutex> lk(mu_);
    manager_mode_ = "SAFE";
  } catch (...) {
  }
  log_event_("WARN", "-", "entered SAFE mode");
}

std::string Supervisor::status_text() {
  std::lock_guard<std::mutex> lk(mu_);
  std::ostringstream oss;
  oss << "OK manager_mode=" << manager_mode_ << " manager_state=" << manager_state_
      << " proc_count=" << specs_.size() << "\n";

  auto order = topo_order_();
  for (const auto &name : order) {
    const auto &spec = specs_.at(name);
    const auto &rt = runtime_.at(name);

    oss << "proc" << " name=" << name << " pid=" << rt.pid << " state=" << to_string(rt.state)
        << " reachable=" << (rt.child_status.reachable ? "true" : "false")
        << " child_mode=" << rt.child_status.mode << " health=" << rt.child_status.health
        << " role=" << to_string(spec.role) << " restarts=" << rt.restart_count
        << " critical=" << (spec.critical ? "true" : "false") << "\n";
  }
  oss << ".";
  return oss.str();
}

std::string Supervisor::proc_status_text(const std::string &name) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!specs_.count(name))
    throw Error("unknown process: " + name);

  const auto &spec = specs_.at(name);
  const auto &rt = runtime_.at(name);

  std::ostringstream oss;
  oss << "OK" << " name=" << name << " pid=" << rt.pid << " state=" << to_string(rt.state)
      << " reachable=" << (rt.child_status.reachable ? "true" : "false")
      << " child_mode=" << rt.child_status.mode << " health=" << rt.child_status.health
      << " detail=" << rt.child_status.detail << " role=" << to_string(spec.role)
      << " critical=" << (spec.critical ? "true" : "false")
      << " restart_policy=" << to_string(spec.restart_policy) << " restarts=" << rt.restart_count;
  return oss.str();
}

std::string Supervisor::events_text(size_t max_count) {
  std::lock_guard<std::mutex> lk(mu_);
  std::ostringstream oss;
  oss << "OK count=" << std::min(max_count, events_.size()) << "\n";

  size_t begin = events_.size() > max_count ? events_.size() - max_count : 0;
  for (size_t i = begin; i < events_.size(); ++i) {
    const auto &e = events_[i];
    oss << "event ts=\"" << e.ts << "\"" << " level=" << e.level << " proc=" << e.proc << " msg=\""
        << e.message << "\"\n";
  }
  oss << ".";
  return oss.str();
}

std::string Supervisor::handle_command_(const std::string &cmdline) {
  auto args = split_ws(cmdline);
  if (args.empty())
    return "ERR empty_command";

  try {
    if (args[0] == "ping")
      return "OK pong";
    if (args[0] == "start") {
      start_all();
      return "OK started";
    }
    if (args[0] == "stop") {
      stop_all();
      return "OK stopped";
    }
    if (args[0] == "start_proc") {
      if (args.size() != 2)
        return "ERR usage: start_proc <proc>";
      start_proc(args[1]);
      return "OK started " + args[1];
    }
    if (args[0] == "stop_proc") {
      if (args.size() != 2)
        return "ERR usage: stop_proc <proc>";
      stop_proc(args[1]);
      return "OK stopped " + args[1];
    }
    if (args[0] == "status")
      return status_text();
    if (args[0] == "events") {
      size_t n = 50;
      if (args.size() == 2)
        n = static_cast<size_t>(std::stoul(args[1]));
      return events_text(n);
    }
    if (args[0] == "mode") {
      if (args.size() != 2)
        return "ERR usage: mode <SAFE|IDLE|TELEOP|AUTO|DIAG>";
      set_mode_all(args[1]);
      return "OK mode=" + args[1];
    }
    if (args[0] == "restart") {
      if (args.size() != 2)
        return "ERR usage: restart <proc>";
      restart_proc(args[1]);
      return "OK restarted " + args[1];
    }
    if (args[0] == "proc_status") {
      if (args.size() != 2)
        return "ERR usage: proc_status <proc>";
      return proc_status_text(args[1]);
    }
    if (args[0] == "child") {
      if (args.size() < 3)
        return "ERR usage: child <proc> <raw_command...>";
      std::string proc = args[1];
      std::string raw;
      for (size_t i = 2; i < args.size(); ++i) {
        if (i > 2)
          raw += " ";
        raw += args[i];
      }
      return forward_child_command_(proc, raw);
    }

    return "ERR unknown_command";
  } catch (const std::exception &e) {
    return std::string("ERR ") + e.what();
  }
}

void Supervisor::start_one_(const std::string &name) {
  ProcessSpec spec;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!specs_.count(name))
      throw Error("unknown process: " + name);
    spec = specs_.at(name);
    auto state = runtime_.at(name).state;
    if (state == ProcState::Running || state == ProcState::Starting)
      return;
  }

  for (const auto &dep : spec.deps) {
    ping_or_throw_(dep);
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    runtime_[name].state = ProcState::Starting;
    runtime_[name].child_status.reachable = false;
    runtime_[name].child_status.mode = "UNKNOWN";
    runtime_[name].child_status.health = "UNKNOWN";
    runtime_[name].child_status.detail.clear();
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    std::lock_guard<std::mutex> lk(mu_);
    runtime_[name].state = ProcState::Failed;
    cv_.notify_all();
    throw Error("fork failed: " + name);
  }

  if (pid == 0) {
    ::setpgid(0, 0);

    std::vector<char *> argv;
    argv.reserve(spec.argv.size() + 1);
    for (auto &s : spec.argv)
      argv.push_back(const_cast<char *>(s.c_str()));
    argv.push_back(nullptr);

    ::execvp(argv[0], argv.data());
    _exit(127);
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    runtime_[name].pid = pid;
    runtime_[name].pgid = pid;
    runtime_[name].state = ProcState::Starting;
  }
  ::setpgid(pid, pid);

  wait_ready_or_throw_(name);

  {
    std::lock_guard<std::mutex> lk(mu_);
    if (runtime_[name].state == ProcState::Starting) {
      runtime_[name].state = ProcState::Running;
    }
    if (std::find(start_order_.begin(), start_order_.end(), name) == start_order_.end()) {
      start_order_.push_back(name);
    }
    started_ = true;
    if (manager_state_ == "INIT" || manager_state_ == "STOPPED") {
      manager_state_ = "RUNNING";
    }
  }
  cv_.notify_all();
  log_event_("INFO", name, "started");
}

void Supervisor::stop_one_(const std::string &name) {
  ProcessSpec spec;
  int pgid = -1;

  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!specs_.count(name))
      throw Error("unknown process: " + name);

    spec = specs_.at(name);
    auto &rt = runtime_.at(name);

    if (rt.pid <= 0) {
      rt.state = ProcState::Stopped;
      rt.child_status.reachable = false;
      cv_.notify_all();
      return;
    }

    pgid = rt.pgid;
    rt.state = ProcState::Stopping;
  }

  ::kill(-pgid, spec.graceful_signal);

  {
    std::unique_lock<std::mutex> lk(mu_);
    bool ok = cv_.wait_for(lk, spec.stop_timeout, [&] {
      const auto &rt = runtime_.at(name);
      return rt.state == ProcState::Stopped || rt.state == ProcState::Exited;
    });
    if (ok)
      return;
  }

  if (spec.kill_on_timeout) {
    ::kill(-pgid, SIGKILL);

    std::unique_lock<std::mutex> lk(mu_);
    bool ok = cv_.wait_for(lk, std::chrono::milliseconds(2000), [&] {
      const auto &rt = runtime_.at(name);
      return rt.state == ProcState::Stopped || rt.state == ProcState::Exited;
    });
    if (ok)
      return;
  }

  throw Error("stop timeout: " + name);
}

void Supervisor::restart_one_(const std::string &name) {
  log_event_("WARN", name, "manual restart requested");
  stop_one_(name);
  start_one_(name);
}

void Supervisor::monitor_children_loop_() {
  while (true) {
    {
      std::unique_lock<std::mutex> lk(mu_);
      if (!bg_running_)
        break;
    }

    std::vector<std::string> names;
    {
      std::lock_guard<std::mutex> lk(mu_);
      for (const auto &[name, _] : specs_)
        names.push_back(name);
    }

    for (const auto &name : names) {
      ProcessSpec spec;
      ProcessRuntime rt;
      {
        std::lock_guard<std::mutex> lk(mu_);
        spec = specs_.at(name);
        rt = runtime_.at(name);
      }

      if (rt.pid <= 0)
        continue;

      int status = 0;
      pid_t r = ::waitpid(rt.pid, &status, WNOHANG);
      if (r == 0 || r != rt.pid)
        continue;

      int exit_code = 0;
      if (WIFEXITED(status))
        exit_code = WEXITSTATUS(status);
      else if (WIFSIGNALED(status))
        exit_code = 128 + WTERMSIG(status);

      bool was_stopping = false;
      bool should_restart = false;

      {
        std::lock_guard<std::mutex> lk(mu_);
        auto &cur = runtime_[name];
        was_stopping = (cur.state == ProcState::Stopping);

        cur.pid = -1;
        cur.pgid = -1;
        cur.last_exit_code = exit_code;
        cur.child_status.reachable = false;

        if (was_stopping) {
          cur.state = ProcState::Stopped;
        } else {
          cur.state = ProcState::Exited;
          if (!spec.critical) {
            if (spec.restart_policy == RestartPolicy::Always)
              should_restart = true;
            if (spec.restart_policy == RestartPolicy::OnFailure && exit_code != 0)
              should_restart = true;
            if (cur.restart_count >= spec.max_restart_count)
              should_restart = false;
          }
        }
      }

      cv_.notify_all();

      if (was_stopping) {
        log_event_("INFO", name, "stopped");
        continue;
      }

      log_event_("ERROR", name, "unexpected exit code=" + std::to_string(exit_code));

      if (spec.critical) {
        log_event_("ERROR", name, "critical process exited; entering SAFE");
        enter_safe_mode();
        {
          std::lock_guard<std::mutex> lk(mu_);
          manager_state_ = "ERROR";
        }
        continue;
      }

      if (should_restart) {
        log_event_("WARN", name, "restarting after backoff");
        std::this_thread::sleep_for(spec.restart_backoff);
        try {
          {
            std::lock_guard<std::mutex> lk(mu_);
            runtime_[name].restart_count++;
          }
          start_one_(name);
        } catch (const std::exception &e) {
          log_event_("ERROR", name, std::string("restart failed: ") + e.what());
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void Supervisor::poll_status_loop_() {
  while (true) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (!bg_running_)
        break;
    }

    std::vector<std::string> names;
    {
      std::lock_guard<std::mutex> lk(mu_);
      for (const auto &[name, _] : specs_)
        names.push_back(name);
    }

    for (const auto &name : names) {
      try {
        refresh_child_status_(name);
      } catch (const std::exception &e) {
        log_event_("WARN", name, std::string("status poll failed: ") + e.what());
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

void Supervisor::refresh_child_status_(const std::string &name) {
  ProcessSpec spec;
  ProcessRuntime rt;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!specs_.count(name))
      throw Error("unknown process: " + name);
    spec = specs_.at(name);
    rt = runtime_.at(name);
  }

  if (rt.pid <= 0) {
    std::lock_guard<std::mutex> lk(mu_);
    runtime_[name].child_status = ChildStatus{};
    runtime_[name].child_status.reachable = false;
    return;
  }

  try {
    auto pong = IpcClient::request(spec.control_sock, "PING", spec.ping_timeout);
    if (pong.rfind("OK", 0) != 0)
      throw Error("PING bad response");

    auto status = IpcClient::request(spec.control_sock, "GET_STATUS", spec.status_timeout);

    ChildStatus cs;
    cs.reachable = true;

    ParsedChildJsonStatus js;
    if (parse_child_status_json(status, &js)) {
      if (!js.mode.empty())
        cs.mode = js.mode;
      if (!js.health.empty())
        cs.health = js.health;
      if (!js.detail.empty())
        cs.detail = js.detail;

      if (js.process_failed || js.health == "FAIL" || js.runtime_state == "Error") {
        cs.health = "FAIL";
      } else if (js.process_degraded || js.health == "DEGRADED") {
        cs.health = "ERR";
      } else if (cs.health.empty()) {
        cs.health = "OK";
      }
    } else {
      auto kv = parse_kv_(status);
      if (kv.count("mode"))
        cs.mode = kv["mode"];
      if (kv.count("health"))
        cs.health = kv["health"];
      if (kv.count("detail"))
        cs.detail = kv["detail"];
    }

    {
      std::lock_guard<std::mutex> lk(mu_);
      auto &cur = runtime_[name];
      cur.child_status = cs;
      if (cur.state == ProcState::Starting && cs.reachable) {
        cur.state = ProcState::Running;
      } else if ((cs.health == "ERR" || cs.health == "FAIL") && cur.state == ProcState::Running) {
        cur.state = ProcState::Degraded;
      } else if (cs.health != "ERR" && cs.health != "FAIL" && cur.state == ProcState::Degraded) {
        cur.state = ProcState::Running;
      }
    }
    cv_.notify_all();
  } catch (...) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto &cur = runtime_[name];
      cur.child_status.reachable = false;
      if (cur.state == ProcState::Running) {
        cur.state = ProcState::Degraded;
      }
    }
    cv_.notify_all();
    throw;
  }
}

void Supervisor::ping_or_throw_(const std::string &name) {
  ProcessSpec spec;
  int pid = -1;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!specs_.count(name))
      throw Error("unknown process: " + name);
    spec = specs_.at(name);
    pid = runtime_.at(name).pid;
  }

  if (pid <= 0)
    throw Error("dependency not running: " + name);

  auto resp = IpcClient::request(spec.control_sock, "PING", spec.ping_timeout);
  if (resp.rfind("OK", 0) != 0)
    throw Error("PING failed: " + name);
}

void Supervisor::wait_ready_or_throw_(const std::string &name) {
  ProcessSpec spec;
  {
    std::lock_guard<std::mutex> lk(mu_);
    spec = specs_.at(name);
  }

  auto deadline = std::chrono::steady_clock::now() + spec.start_timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    try {
      refresh_child_status_(name);
    } catch (...) {
    }

    {
      std::lock_guard<std::mutex> lk(mu_);
      const auto &rt = runtime_.at(name);
      if (rt.child_status.reachable)
        return;
      if (rt.state == ProcState::Exited || rt.state == ProcState::Failed) {
        throw Error("process exited during startup: " + name);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    runtime_[name].state = ProcState::Failed;
  }
  cv_.notify_all();
  throw Error("startup timeout: " + name);
}

void Supervisor::log_event_(const std::string &level, const std::string &proc,
                            const std::string &message) {
  std::lock_guard<std::mutex> lk(mu_);
  events_.push_back(Event{now_string(), level, proc, message});
  constexpr size_t kMaxEvents = 200;
  if (events_.size() > kMaxEvents) {
    events_.erase(events_.begin(), events_.begin() + (events_.size() - kMaxEvents));
  }
}

std::vector<std::string> Supervisor::topo_order_() const {
  std::unordered_map<std::string, int> indeg;
  std::unordered_map<std::string, std::vector<std::string>> out;

  for (const auto &[name, _] : specs_)
    indeg[name] = 0;
  for (const auto &[name, spec] : specs_) {
    for (const auto &dep : spec.deps) {
      if (!specs_.count(dep))
        throw Error("unknown dependency: " + name + " depends on " + dep);
      indeg[name]++;
      out[dep].push_back(name);
    }
  }

  std::vector<std::string> q;
  for (const auto &[name, deg] : indeg) {
    if (deg == 0)
      q.push_back(name);
  }

  std::vector<std::string> order;
  for (size_t i = 0; i < q.size(); ++i) {
    auto u = q[i];
    order.push_back(u);
    for (const auto &v : out[u]) {
      if (--indeg[v] == 0)
        q.push_back(v);
    }
  }

  if (order.size() != specs_.size())
    throw Error("dependency cycle detected");
  return order;
}

void Supervisor::apply_mode_ordered_(const std::string &mode) {
  validate_mode_(mode);

  std::vector<std::string> order;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!started_ && mode != "SAFE")
      throw Error("processes not started");
    order = topo_order_();
  }

  std::vector<Role> role_order;
  if (mode == "SAFE" || mode == "IDLE") {
    role_order = {Role::Actuator, Role::Compute, Role::Sensor};
  } else {
    role_order = {Role::Sensor, Role::Compute, Role::Actuator};
  }

  for (auto role : role_order) {
    for (const auto &name : order) {
      ProcessSpec spec;
      ProcessRuntime rt;
      {
        std::lock_guard<std::mutex> lk(mu_);
        spec = specs_.at(name);
        rt = runtime_.at(name);
      }

      if (spec.role != role || rt.pid <= 0)
        continue;

      auto resp =
        IpcClient::request(spec.control_sock, "SET_MODE " + mode, std::chrono::milliseconds(500));
      if (resp.rfind("OK", 0) != 0) {
        throw Error("SET_MODE failed for " + name + ": " + resp);
      }
    }
  }
}

void Supervisor::validate_mode_(const std::string &mode) const {
  static const std::unordered_set<std::string> kModes = {"SAFE", "IDLE", "TELEOP", "AUTO", "DIAG"};
  if (!kModes.count(mode))
    throw Error("invalid mode: " + mode);
}

std::string Supervisor::forward_child_command_(const std::string &name,
                                               const std::string &raw_command) {
  ProcessSpec spec;
  ProcessRuntime rt;

  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!specs_.count(name))
      throw Error("unknown process: " + name);
    spec = specs_.at(name);
    rt = runtime_.at(name);
  }

  if (rt.pid <= 0) {
    throw Error("process not running: " + name);
  }

  auto resp = IpcClient::request(spec.control_sock, raw_command, spec.status_timeout);
  log_event_("INFO", name, "forwarded child command: " + raw_command);
  return resp;
}

std::unordered_map<std::string, std::string> Supervisor::parse_kv_(const std::string &line) {
  std::unordered_map<std::string, std::string> out;
  auto toks = split_ws(line);

  for (size_t i = 0; i < toks.size(); ++i) {
    if (i == 0 && (toks[i] == "OK" || toks[i] == "ERR"))
      continue;
    auto pos = toks[i].find('=');
    if (pos == std::string::npos)
      continue;
    out[toks[i].substr(0, pos)] = toks[i].substr(pos + 1);
  }
  return out;
}

} // namespace supervisor

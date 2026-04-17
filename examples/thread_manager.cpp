#include "thread_manager.hpp"

#include <chrono>
#include <stdexcept>

namespace {

const char *ToString(PlannerThreadState s) {
  switch (s) {
  case PlannerThreadState::Init:
    return "Init";
  case PlannerThreadState::Starting:
    return "Starting";
  case PlannerThreadState::Running:
    return "Running";
  case PlannerThreadState::Stopping:
    return "Stopping";
  case PlannerThreadState::Stopped:
    return "Stopped";
  case PlannerThreadState::Error:
    return "Error";
  }
  return "Unknown";
}

const char *ToString(PlannerRuntimeState s) {
  switch (s) {
  case PlannerRuntimeState::Init:
    return "Init";
  case PlannerRuntimeState::Starting:
    return "Starting";
  case PlannerRuntimeState::Running:
    return "Running";
  case PlannerRuntimeState::Stopping:
    return "Stopping";
  case PlannerRuntimeState::Stopped:
    return "Stopped";
  case PlannerRuntimeState::Error:
    return "Error";
  }
  return "Unknown";
}

} // namespace

ThreadManager::~ThreadManager() {
  request_stop();
  join_all();
}

void ThreadManager::register_thread(const std::string &name, bool critical,
                                    std::uint64_t heartbeat_timeout_ms,
                                    std::uint64_t progress_timeout_ms) {
  std::lock_guard<std::mutex> lk(info_mu_);
  if (infos_.count(name) != 0) {
    throw std::runtime_error("duplicate thread registration: " + name);
  }

  PlannerThreadInfo info;
  info.name = name;
  info.critical = critical;
  info.heartbeat_timeout_ms = heartbeat_timeout_ms;
  info.progress_timeout_ms = progress_timeout_ms;
  info.heartbeat_ms.store(now_ms());
  info.progress_ms.store(now_ms());

  infos_.emplace(name, std::move(info));
}

void ThreadManager::set_thread_state(const std::string &name, PlannerThreadState state) {
  std::lock_guard<std::mutex> lk(info_mu_);
  auto it = infos_.find(name);
  if (it == infos_.end())
    return;
  it->second.state.store(state);
}

void ThreadManager::beat(const std::string &name) {
  std::lock_guard<std::mutex> lk(info_mu_);
  auto it = infos_.find(name);
  if (it == infos_.end())
    return;
  it->second.heartbeat_ms.store(now_ms());
}

void ThreadManager::progress(const std::string &name) {
  std::lock_guard<std::mutex> lk(info_mu_);
  auto it = infos_.find(name);
  if (it == infos_.end())
    return;
  it->second.progress_ms.store(now_ms());
}

void ThreadManager::bump_loop(const std::string &name) {
  std::lock_guard<std::mutex> lk(info_mu_);
  auto it = infos_.find(name);
  if (it == infos_.end())
    return;
  it->second.loop_count.fetch_add(1);
}

void ThreadManager::set_thread_detail(const std::string &name, const std::string &detail) {
  std::lock_guard<std::mutex> lk(info_mu_);
  auto it = infos_.find(name);
  if (it == infos_.end())
    return;
  it->second.detail = detail;
}

void ThreadManager::set_thread_error(const std::string &name, const std::string &error) {
  std::lock_guard<std::mutex> lk(info_mu_);
  auto it = infos_.find(name);
  if (it == infos_.end())
    return;
  it->second.last_error = error;
  it->second.error_count.fetch_add(1);
}

void ThreadManager::set_runtime_state(PlannerRuntimeState state) { runtime_state_.store(state); }

PlannerRuntimeState ThreadManager::runtime_state() const { return runtime_state_.load(); }

void ThreadManager::request_stop() { stop_requested_.store(true); }

bool ThreadManager::stop_requested() const { return stop_requested_.load(); }

bool ThreadManager::process_failed() const { return process_failed_.load(); }

bool ThreadManager::process_degraded() const { return process_degraded_.load(); }

void ThreadManager::watchdog_check() {
  const std::uint64_t now = now_ms();
  bool degraded = false;
  bool failed = false;

  std::lock_guard<std::mutex> lk(info_mu_);
  for (auto &kv : infos_) {
    auto &info = kv.second;
    auto state = info.state.load();

    if (state != PlannerThreadState::Running) {
      if (state == PlannerThreadState::Error) {
        if (info.critical) {
          failed = true;
        } else {
          degraded = true;
        }
      }
      continue;
    }

    const std::uint64_t hb_age = now - info.heartbeat_ms.load();
    const std::uint64_t prog_age = now - info.progress_ms.load();

    if (hb_age > info.heartbeat_timeout_ms || prog_age > info.progress_timeout_ms) {
      info.state.store(PlannerThreadState::Error);
      info.error_count.fetch_add(1);

      if (hb_age > info.heartbeat_timeout_ms && prog_age > info.progress_timeout_ms) {
        info.last_error = "heartbeat timeout and progress timeout";
      } else if (hb_age > info.heartbeat_timeout_ms) {
        info.last_error = "heartbeat timeout";
      } else {
        info.last_error = "progress timeout";
      }

      if (info.critical) {
        failed = true;
      } else {
        degraded = true;
      }
    }
  }

  process_degraded_.store(degraded);
  process_failed_.store(failed);
  if (failed) {
    runtime_state_.store(PlannerRuntimeState::Error);
  }
}

nlohmann::json ThreadManager::build_status_json(const std::string &module_name,
                                                const std::string &mode) const {
  nlohmann::json j;
  j["module"] = module_name;
  j["runtime_state"] = ToString(runtime_state_.load());
  j["health"] = process_failed_.load() ? "FAIL" : (process_degraded_.load() ? "DEGRADED" : "OK");
  j["process_failed"] = process_failed_.load();
  j["process_degraded"] = process_degraded_.load();
  j["mode"] = mode;
  j["threads"] = nlohmann::json::array();

  std::lock_guard<std::mutex> lk(info_mu_);
  for (const auto &kv : infos_) {
    const auto &info = kv.second;
    nlohmann::json t;
    t["name"] = info.name;
    t["state"] = ToString(info.state.load());
    t["critical"] = info.critical;
    t["heartbeat_ms"] = info.heartbeat_ms.load();
    t["progress_ms"] = info.progress_ms.load();
    t["heartbeat_timeout_ms"] = info.heartbeat_timeout_ms;
    t["progress_timeout_ms"] = info.progress_timeout_ms;
    t["loop_count"] = info.loop_count.load();
    t["error_count"] = info.error_count.load();
    t["detail"] = info.detail;
    t["last_error"] = info.last_error;
    j["threads"].push_back(std::move(t));
  }

  return j;
}

void ThreadManager::join_all() {
  std::lock_guard<std::mutex> lk(threads_mu_);
  for (auto &t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
  threads_.clear();
}

std::uint64_t ThreadManager::now_ms() const {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

PlannerThreadState ThreadManager::get_thread_state(const std::string &name) const {
  std::lock_guard<std::mutex> lk(info_mu_);
  auto it = infos_.find(name);
  if (it == infos_.end())
    return PlannerThreadState::Error;
  return it->second.state.load();
}
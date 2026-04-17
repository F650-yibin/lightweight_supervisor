#include "thread_manager.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <poll.h>

namespace {

std::atomic<bool> g_stop{false};

void SignalHandler(int) { g_stop.store(true); }

class PlannerProcess {
public:
  explicit PlannerProcess(std::string sock_path) : sock_path_(std::move(sock_path)) {}

  ~PlannerProcess() { ShutdownSocket(); }

  bool Init() {
    mode_ = "SAFE";
    tm_.set_runtime_state(PlannerRuntimeState::Starting);

    tm_.register_thread("planning_thread", true, 500, 2000);
    tm_.register_thread("ipc_thread", true, 500, 1000);
    tm_.register_thread("status_thread", false, 1000, 5000);
    tm_.register_thread("watchdog_thread", true, 500, 1000);

    return SetupSocket();
  }

  void Run() {
    LaunchThreads();
    tm_.set_runtime_state(PlannerRuntimeState::Running);

    while (!g_stop.load() && !tm_.stop_requested()) {
      if (tm_.process_failed()) {
        tm_.request_stop();
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Shutdown();
  }

private:
  bool SetupSocket() {
    std::error_code ec;
    std::filesystem::remove(sock_path_, ec);

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
      return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path_.c_str());

    if (::bind(server_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      ::close(server_fd_);
      server_fd_ = -1;
      return false;
    }

    if (::listen(server_fd_, 16) < 0) {
      ::close(server_fd_);
      server_fd_ = -1;
      return false;
    }

    return true;
  }

  void ShutdownSocket() {
    if (server_fd_ >= 0) {
      ::close(server_fd_);
      server_fd_ = -1;
    }

    std::error_code ec;
    std::filesystem::remove(sock_path_, ec);
  }

  void LaunchThreads() {
    tm_.launch_thread("planning_thread", [this]() { PlanningLoop(); });
    tm_.launch_thread("status_thread", [this]() { StatusLoop(); });
    tm_.launch_thread("watchdog_thread", [this]() { WatchdogLoop(); });
    tm_.launch_thread("ipc_thread", [this]() { IpcLoop(); });
  }

  void PlanningLoop() {
    while (!tm_.stop_requested() && !g_stop.load()) {
      tm_.beat("planning_thread");
      tm_.bump_loop("planning_thread");

      std::string mode_copy;
      {
        std::lock_guard<std::mutex> lk(mode_mu_);
        mode_copy = mode_;
      }

      if (mode_copy == "AUTO") {
        tm_.set_thread_detail("planning_thread", "planning in AUTO");
      } else if (mode_copy == "DIAG") {
        tm_.set_thread_detail("planning_thread", "planner diagnostics");
      } else if (mode_copy == "IDLE") {
        tm_.set_thread_detail("planning_thread", "planner idle");
      } else {
        tm_.set_thread_detail("planning_thread", "safe hold");
      }

      tm_.progress("planning_thread");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  void StatusLoop() {
    while (!tm_.stop_requested() && !g_stop.load()) {
      tm_.beat("status_thread");
      tm_.bump_loop("status_thread");

      std::string mode_copy;
      {
        std::lock_guard<std::mutex> lk(mode_mu_);
        mode_copy = mode_;
      }

      {
        std::lock_guard<std::mutex> lk(cached_mu_);
        cached_status_ = tm_.build_status_json("planner", mode_copy).dump() + "\n";
      }

      tm_.progress("status_thread");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }

  void WatchdogLoop() {
    while (!tm_.stop_requested() && !g_stop.load()) {
      tm_.beat("watchdog_thread");
      tm_.bump_loop("watchdog_thread");

      tm_.watchdog_check();
      tm_.progress("watchdog_thread");

      if (tm_.process_failed()) {
        tm_.set_thread_detail("watchdog_thread", "process failure detected");
        tm_.request_stop();
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  void IpcLoop() {
    while (!tm_.stop_requested() && !g_stop.load()) {
      tm_.beat("ipc_thread");
      tm_.bump_loop("ipc_thread");

      pollfd pfd{};
      pfd.fd = server_fd_;
      pfd.events = POLLIN;

      const int ready = ::poll(&pfd, 1, 200);
      if (ready == 0) {
        tm_.progress("ipc_thread");
        continue;
      }
      if (ready < 0) {
        if (errno == EINTR && !g_stop.load()) {
          tm_.progress("ipc_thread");
          continue;
        }
        if (tm_.stop_requested() || g_stop.load()) {
          break;
        }
        tm_.set_thread_error("ipc_thread", "poll failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }

      int client_fd = ::accept(server_fd_, nullptr, nullptr);
      if (client_fd < 0) {
        if (tm_.stop_requested() || g_stop.load()) {
          break;
        }
        if (errno == EINTR) {
          tm_.progress("ipc_thread");
          continue;
        }
        tm_.set_thread_error("ipc_thread", "accept failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }

      HandleClient(client_fd);
      ::close(client_fd);
      tm_.progress("ipc_thread");
    }
  }

  void HandleClient(int client_fd) {
    char buf[1024];
    std::memset(buf, 0, sizeof(buf));

    const ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0)
      return;

    std::string req(buf, static_cast<std::size_t>(n));
    Trim(req);

    const std::string resp = Dispatch(req);
    ::write(client_fd, resp.c_str(), resp.size());
  }

  std::string Dispatch(const std::string &req) {
    if (req == "PING") {
      return "OK pong\n";
    }

    if (req == "GET_STATUS") {
      std::lock_guard<std::mutex> lk(cached_mu_);
      if (!cached_status_.empty()) {
        return cached_status_;
      }

      std::string mode_copy;
      {
        std::lock_guard<std::mutex> mode_lk(mode_mu_);
        mode_copy = mode_;
      }

      return tm_.build_status_json("planner", mode_copy).dump() + "\n";
    }

    if (req.rfind("SET_MODE ", 0) == 0) {
      const std::string mode = req.substr(std::string("SET_MODE ").size());
      {
        std::lock_guard<std::mutex> lk(mode_mu_);
        mode_ = mode;
      }
      return "OK mode=" + mode + "\n";
    }

    return "ERR unknown command\n";
  }

  void Shutdown() {
    tm_.set_runtime_state(PlannerRuntimeState::Stopping);
    tm_.request_stop();

    if (server_fd_ >= 0) {
      ::shutdown(server_fd_, SHUT_RDWR);
      ::close(server_fd_);
      server_fd_ = -1;
    }

    tm_.join_all();
    tm_.set_runtime_state(PlannerRuntimeState::Stopped);

    std::error_code ec;
    std::filesystem::remove(sock_path_, ec);
  }

  static void Trim(std::string &s) {
    while (!s.empty() &&
           (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
      s.pop_back();
    }

    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
      ++i;
    }

    if (i > 0) {
      s.erase(0, i);
    }
  }

private:
  ThreadManager tm_;
  std::string sock_path_;
  int server_fd_{-1};

  std::mutex cached_mu_;
  std::string cached_status_;

  std::mutex mode_mu_;
  std::string mode_;
};

} // namespace

int main(int argc, char **argv) {
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  std::string sock_path = "/tmp/robot/planner.sock";
  if (argc >= 2) {
    sock_path = argv[1];
  }

  PlannerProcess proc(sock_path);
  if (!proc.Init()) {
    return 1;
  }

  proc.Run();
  return 0;
}
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

enum class PlannerThreadState { Init, Starting, Running, Stopping, Stopped, Error };

enum class PlannerRuntimeState { Init, Starting, Running, Stopping, Stopped, Error };

struct PlannerThreadInfo {
  std::string name;
  bool critical{true};

  std::atomic<PlannerThreadState> state{PlannerThreadState::Init};
  std::atomic<std::uint64_t> heartbeat_ms{0};
  std::atomic<std::uint64_t> progress_ms{0};

  std::uint64_t heartbeat_timeout_ms{1000};
  std::uint64_t progress_timeout_ms{3000};

  std::atomic<std::uint64_t> loop_count{0};
  std::atomic<std::uint64_t> error_count{0};

  std::string last_error;
  std::string detail;
};

class ThreadManager {
public:
  ThreadManager() = default;
  ~ThreadManager();

  void register_thread(const std::string &name, bool critical, std::uint64_t heartbeat_timeout_ms,
                       std::uint64_t progress_timeout_ms);

  void set_thread_state(const std::string &name, PlannerThreadState state);
  void beat(const std::string &name);
  void progress(const std::string &name);
  void bump_loop(const std::string &name);
  void set_thread_detail(const std::string &name, const std::string &detail);
  void set_thread_error(const std::string &name, const std::string &error);

  void set_runtime_state(PlannerRuntimeState state);
  PlannerRuntimeState runtime_state() const;

  void request_stop();
  bool stop_requested() const;

  void watchdog_check();

  bool process_failed() const;
  bool process_degraded() const;

  nlohmann::json build_status_json(const std::string &module_name, const std::string &mode) const;

  template <typename Fn> void launch_thread(const std::string &name, Fn &&fn) {
    std::lock_guard<std::mutex> lk(threads_mu_);
    threads_.emplace_back([this, name, fn = std::forward<Fn>(fn)]() mutable {
      set_thread_state(name, PlannerThreadState::Starting);
      try {
        set_thread_state(name, PlannerThreadState::Running);
        fn();
        if (get_thread_state(name) != PlannerThreadState::Error) {
          set_thread_state(name, PlannerThreadState::Stopped);
        }
      } catch (const std::exception &e) {
        set_thread_error(name, e.what());
        set_thread_state(name, PlannerThreadState::Error);
        runtime_state_.store(PlannerRuntimeState::Error);
        process_failed_.store(true);
      } catch (...) {
        set_thread_error(name, "unknown exception");
        set_thread_state(name, PlannerThreadState::Error);
        runtime_state_.store(PlannerRuntimeState::Error);
        process_failed_.store(true);
      }
    });
  }

  void join_all();

private:
  std::uint64_t now_ms() const;
  PlannerThreadState get_thread_state(const std::string &name) const;

private:
  mutable std::mutex info_mu_;
  std::unordered_map<std::string, PlannerThreadInfo> infos_;

  mutable std::mutex threads_mu_;
  std::vector<std::thread> threads_;

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> process_failed_{false};
  std::atomic<bool> process_degraded_{false};
  std::atomic<PlannerRuntimeState> runtime_state_{PlannerRuntimeState::Init};
};
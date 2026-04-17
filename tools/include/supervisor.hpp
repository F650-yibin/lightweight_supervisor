#pragma once
#include "control_server.hpp"
#include "supervisor_types.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace supervisor {

class Supervisor {
public:
  Supervisor();
  ~Supervisor();

  void add_process(ProcessSpec spec);

  void start_control_interface(const std::string &sock_path);
  void stop_control_interface();

  void start_all();
  void stop_all();

  void start_proc(const std::string &name);
  void stop_proc(const std::string &name);
  void restart_proc(const std::string &name);

  void set_mode_all(const std::string &mode);
  void enter_safe_mode();

  std::string status_text();
  std::string proc_status_text(const std::string &name);
  std::string events_text(size_t max_count = 50);

  void run_background_threads();
  void stop_background_threads();

private:
  std::string handle_command_(const std::string &cmdline);

  void start_one_(const std::string &name);
  void stop_one_(const std::string &name);
  void restart_one_(const std::string &name);

  void monitor_children_loop_();
  void poll_status_loop_();

  void refresh_child_status_(const std::string &name);
  void ping_or_throw_(const std::string &name);
  void wait_ready_or_throw_(const std::string &name);

  void log_event_(const std::string &level, const std::string &proc, const std::string &message);

  std::vector<std::string> topo_order_() const;
  void apply_mode_ordered_(const std::string &mode);
  void validate_mode_(const std::string &mode) const;

  std::string forward_child_command_(const std::string &name, const std::string &raw_command);

  static std::unordered_map<std::string, std::string> parse_kv_(const std::string &line);

private:
  mutable std::mutex mu_;
  std::condition_variable cv_;

  std::unordered_map<std::string, ProcessSpec> specs_;
  std::unordered_map<std::string, ProcessRuntime> runtime_;
  std::vector<std::string> start_order_;
  std::vector<Event> events_;

  std::string manager_mode_{"INIT"};
  std::string manager_state_{"INIT"};
  bool started_{false};

  bool bg_running_{false};
  std::thread monitor_thread_;
  std::thread poll_thread_;

  std::unique_ptr<ControlServer> control_server_;
};

} // namespace supervisor
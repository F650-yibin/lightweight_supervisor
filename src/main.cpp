#include "config_loader.hpp"
#include "supervisor.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using namespace supervisor;

namespace {
std::atomic<bool> g_running{true};

void handle_signal(int) { g_running = false; }
} // namespace

int main(int argc, char **argv) {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::string config_path = "supervisor_config.json";
  if (argc >= 2) {
    config_path = argv[1];
  }

  SupervisorConfig cfg;
  try {
    cfg = load_config_from_json_file(config_path);
  } catch (const std::exception &e) {
    std::cerr << "failed to load config: " << e.what() << "\n";
    return 1;
  }

  Supervisor sv;
  try {
    for (auto &proc : cfg.processes) {
      sv.add_process(proc);
    }

    sv.start_control_interface(cfg.control_socket);
    sv.run_background_threads();
  } catch (const std::exception &e) {
    std::cerr << "failed to start supervisor: " << e.what() << "\n";
    return 1;
  }

  std::cout << "supervisor started\n";
  std::cout << "config: " << config_path << "\n";
  std::cout << "control socket: " << cfg.control_socket << "\n\n";

  std::cout << "press Ctrl+C to shutdown gracefully\n";

  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  std::cout << "\nshutdown requested, stopping supervisor...\n";

  try {
    sv.enter_safe_mode();
  } catch (...) {
  }

  try {
    sv.stop_all();
  } catch (const std::exception &e) {
    std::cerr << "stop_all failed: " << e.what() << "\n";
  }

  try {
    sv.stop_background_threads();
  } catch (...) {
  }

  try {
    sv.stop_control_interface();
  } catch (...) {
  }

  std::cout << "supervisor stopped\n";
  return 0;
}
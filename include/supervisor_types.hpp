#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace supervisor {

enum class Role {
  Sensor,
  Compute,
  Actuator
};

enum class RestartPolicy {
  Never,
  OnFailure,
  Always
};

enum class ProcState {
  Stopped,
  Starting,
  Running,
  Degraded,
  Stopping,
  Exited,
  Failed
};

inline const char* to_string(Role r) {
  switch (r) {
    case Role::Sensor: return "Sensor";
    case Role::Compute: return "Compute";
    case Role::Actuator: return "Actuator";
  }
  return "Unknown";
}

inline const char* to_string(RestartPolicy p) {
  switch (p) {
    case RestartPolicy::Never: return "Never";
    case RestartPolicy::OnFailure: return "OnFailure";
    case RestartPolicy::Always: return "Always";
  }
  return "Unknown";
}

inline const char* to_string(ProcState s) {
  switch (s) {
    case ProcState::Stopped: return "Stopped";
    case ProcState::Starting: return "Starting";
    case ProcState::Running: return "Running";
    case ProcState::Degraded: return "Degraded";
    case ProcState::Stopping: return "Stopping";
    case ProcState::Exited: return "Exited";
    case ProcState::Failed: return "Failed";
  }
  return "Unknown";
}

struct ProcessSpec {
  std::string name;
  std::vector<std::string> argv;
  std::vector<std::string> deps;
  std::string control_sock;

  Role role{Role::Compute};
  bool critical{true};

  RestartPolicy restart_policy{RestartPolicy::Never};
  int max_restart_count{3};
  std::chrono::milliseconds restart_backoff{1000};

  std::chrono::milliseconds start_timeout{5000};
  std::chrono::milliseconds stop_timeout{2000};
  std::chrono::milliseconds ping_timeout{300};
  std::chrono::milliseconds status_timeout{500};

  int graceful_signal{15};
  bool kill_on_timeout{true};
};

struct ChildStatus {
  std::string mode{"UNKNOWN"};
  std::string health{"UNKNOWN"};
  std::string detail;
  bool reachable{false};
};

struct ProcessRuntime {
  int pid{-1};
  int pgid{-1};
  ProcState state{ProcState::Stopped};
  int last_exit_code{0};
  int restart_count{0};
  ChildStatus child_status;
};

struct Event {
  std::string ts;
  std::string level;
  std::string proc;
  std::string message;
};

} // namespace supervisor
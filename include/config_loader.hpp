#pragma once
#include "supervisor_types.hpp"

#include <string>
#include <vector>

namespace supervisor {

struct SupervisorConfig {
  std::string control_socket{"/tmp/robot/lifecycle_manager.sock"};
  std::vector<ProcessSpec> processes;
};

SupervisorConfig load_config_from_json_file(const std::string& path);

} // namespace supervisor
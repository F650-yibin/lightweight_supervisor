#include "config_loader.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace supervisor {

namespace {
using json = nlohmann::json;

struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

Role parse_role(const std::string& s) {
  if (s == "Sensor") return Role::Sensor;
  if (s == "Compute") return Role::Compute;
  if (s == "Actuator") return Role::Actuator;
  throw Error("invalid role: " + s);
}

RestartPolicy parse_restart_policy(const std::string& s) {
  if (s == "Never") return RestartPolicy::Never;
  if (s == "OnFailure") return RestartPolicy::OnFailure;
  if (s == "Always") return RestartPolicy::Always;
  throw Error("invalid restart_policy: " + s);
}
} // namespace

SupervisorConfig load_config_from_json_file(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) {
    throw Error("failed to open config file: " + path);
  }

  json j;
  ifs >> j;

  SupervisorConfig cfg;
  if (j.contains("control_socket")) {
    cfg.control_socket = j.at("control_socket").get<std::string>();
  }

  if (!j.contains("processes") || !j.at("processes").is_array()) {
    throw Error("config missing 'processes' array");
  }

  for (const auto& jp : j.at("processes")) {
    ProcessSpec p;

    p.name = jp.at("name").get<std::string>();
    p.argv = jp.at("argv").get<std::vector<std::string>>();
    p.control_sock = jp.at("control_sock").get<std::string>();

    if (jp.contains("deps")) {
      p.deps = jp.at("deps").get<std::vector<std::string>>();
    }

    if (jp.contains("role")) {
      p.role = parse_role(jp.at("role").get<std::string>());
    }

    if (jp.contains("critical")) {
      p.critical = jp.at("critical").get<bool>();
    }

    if (jp.contains("restart_policy")) {
      p.restart_policy = parse_restart_policy(jp.at("restart_policy").get<std::string>());
    }

    if (jp.contains("max_restart_count")) {
      p.max_restart_count = jp.at("max_restart_count").get<int>();
    }

    if (jp.contains("restart_backoff_ms")) {
      p.restart_backoff = std::chrono::milliseconds(jp.at("restart_backoff_ms").get<int>());
    }

    if (jp.contains("start_timeout_ms")) {
      p.start_timeout = std::chrono::milliseconds(jp.at("start_timeout_ms").get<int>());
    }

    if (jp.contains("stop_timeout_ms")) {
      p.stop_timeout = std::chrono::milliseconds(jp.at("stop_timeout_ms").get<int>());
    }

    if (jp.contains("ping_timeout_ms")) {
      p.ping_timeout = std::chrono::milliseconds(jp.at("ping_timeout_ms").get<int>());
    }

    if (jp.contains("status_timeout_ms")) {
      p.status_timeout = std::chrono::milliseconds(jp.at("status_timeout_ms").get<int>());
    }

    cfg.processes.push_back(std::move(p));
  }

  return cfg;
}

} // namespace supervisor
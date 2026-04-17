#include "child_status_json.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <sstream>

namespace supervisor {

namespace {

std::string build_detail_from_threads(const nlohmann::json &j) {
  std::ostringstream oss;

  if (j.contains("module") && j["module"].is_string()) {
    oss << "module=" << j["module"].get<std::string>();
  }
  if (j.contains("runtime_state") && j["runtime_state"].is_string()) {
    if (oss.tellp() > 0)
      oss << " ";
    oss << "runtime_state=" << j["runtime_state"].get<std::string>();
  }
  if (j.contains("health") && j["health"].is_string()) {
    if (oss.tellp() > 0)
      oss << " ";
    oss << "health=" << j["health"].get<std::string>();
  }
  if (j.contains("mode") && j["mode"].is_string()) {
    if (oss.tellp() > 0)
      oss << " ";
    oss << "mode=" << j["mode"].get<std::string>();
  }

  if (j.contains("threads") && j["threads"].is_array()) {
    for (const auto &t : j["threads"]) {
      std::string state = t.value("state", "");
      std::uint64_t error_count = t.value("error_count", 0ULL);
      std::string last_error = t.value("last_error", "");
      if (state == "Error" || error_count > 0 || !last_error.empty()) {
        oss << " | thread=" << t.value("name", "") << " state=" << state
            << " error_count=" << error_count;
        if (!last_error.empty()) {
          oss << " last_error=" << last_error;
        }
      }
    }
  }

  return oss.str();
}

} // namespace

bool parse_child_status_json(const std::string &text, ParsedChildJsonStatus *out) {
  if (!out)
    return false;

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(text);
  } catch (...) {
    return false;
  }

  out->ok = true;
  out->module = j.value("module", "");
  out->runtime_state = j.value("runtime_state", "");
  out->health = j.value("health", "UNKNOWN");
  out->process_failed = j.value("process_failed", false);
  out->process_degraded = j.value("process_degraded", false);
  out->mode = j.value("mode", "");
  out->detail = build_detail_from_threads(j);
  return true;
}

} // namespace supervisor
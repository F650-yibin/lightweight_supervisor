#pragma once

#include <string>

namespace supervisor {

struct ParsedChildJsonStatus {
  bool ok{false};
  std::string module;
  std::string runtime_state;
  std::string health;
  bool process_failed{false};
  bool process_degraded{false};
  std::string mode;
  std::string detail;
};

bool parse_child_status_json(const std::string &text, ParsedChildJsonStatus *out);

} // namespace supervisor
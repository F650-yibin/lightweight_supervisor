#pragma once
#include <chrono>
#include <string>

namespace supervisor {

class IpcClient {
public:
  static std::string request(const std::string& sock_path,
                             const std::string& line,
                             std::chrono::milliseconds timeout);
};

} // namespace supervisor
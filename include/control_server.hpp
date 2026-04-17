#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace supervisor {

class ControlServer {
public:
  using Handler = std::function<std::string(const std::string&)>;

  ControlServer(std::string socket_path, Handler handler);
  ~ControlServer();

  void start();
  void stop();

private:
  void run_();
  void poke_();

private:
  std::string socket_path_;
  Handler handler_;
  std::atomic<bool> running_{false};
  std::thread th_;
};

} // namespace supervisor
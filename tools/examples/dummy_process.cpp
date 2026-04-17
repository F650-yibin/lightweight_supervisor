#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

class DummyProcess {
public:
  explicit DummyProcess(std::string sock_path) : sock_path_(std::move(sock_path)) {}

  void run() {
    std::filesystem::create_directories("/tmp/robot");
    std::filesystem::remove(sock_path_);

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      std::perror("socket");
      return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      std::perror("bind");
      ::close(fd);
      return;
    }

    if (::listen(fd, 16) != 0) {
      std::perror("listen");
      ::close(fd);
      return;
    }

    std::cout << "[dummy] listening on " << sock_path_ << "\n";

    while (running_) {
      int cfd = ::accept(fd, nullptr, nullptr);
      if (cfd < 0)
        continue;

      std::string line;
      char ch;
      while (true) {
        ssize_t n = ::recv(cfd, &ch, 1, 0);
        if (n <= 0)
          break;
        if (ch == '\n')
          break;
        line.push_back(ch);
      }

      std::string resp = handle_command(line);
      if (!hang_) {
        if (resp.empty() || resp.back() != '\n')
          resp.push_back('\n');
        ::send(cfd, resp.data(), resp.size(), 0);
      }

      ::close(cfd);
    }

    ::close(fd);
    std::filesystem::remove(sock_path_);
  }

private:
  std::string handle_command(const std::string &line) {
    apply_delay_if_needed();

    if (hang_) {
      std::this_thread::sleep_for(std::chrono::hours(24));
    }

    if (line == "PING") {
      return "OK pong";
    }

    if (line == "GET_STATUS") {
      std::lock_guard<std::mutex> lk(mu_);
      return "OK mode=" + mode_ + " health=" + health_ + " detail=" + detail_;
    }

    if (starts_with(line, "SET_MODE ")) {
      auto new_mode = line.substr(std::string("SET_MODE ").size());
      {
        std::lock_guard<std::mutex> lk(mu_);
        mode_ = new_mode;
        if (new_mode == "SAFE")
          detail_ = "safe";
        else if (new_mode == "IDLE")
          detail_ = "idle";
        else if (new_mode == "TELEOP")
          detail_ = "teleop";
        else if (new_mode == "AUTO")
          detail_ = "auto";
        else if (new_mode == "DIAG")
          detail_ = "diag";
        else {
          health_ = "ERR";
          detail_ = "unknown_mode";
          return "ERR detail=unknown_mode";
        }
      }
      std::cout << "[dummy] mode => " << new_mode << "\n";
      return "OK";
    }

    if (starts_with(line, "SET_HEALTH ")) {
      auto h = line.substr(std::string("SET_HEALTH ").size());
      {
        std::lock_guard<std::mutex> lk(mu_);
        health_ = h;
      }
      std::cout << "[dummy] health => " << h << "\n";
      return "OK";
    }

    if (starts_with(line, "SET_DETAIL ")) {
      auto d = line.substr(std::string("SET_DETAIL ").size());
      {
        std::lock_guard<std::mutex> lk(mu_);
        detail_ = d;
      }
      std::cout << "[dummy] detail => " << d << "\n";
      return "OK";
    }

    if (starts_with(line, "SET_DELAY_MS ")) {
      auto s = line.substr(std::string("SET_DELAY_MS ").size());
      int ms = std::stoi(s);
      delay_ms_.store(ms);
      std::cout << "[dummy] delay_ms => " << ms << "\n";
      return "OK";
    }

    if (line == "HANG") {
      hang_.store(true);
      std::cout << "[dummy] hang enabled\n";
      return "OK";
    }

    if (line == "RECOVER") {
      hang_.store(false);
      delay_ms_.store(0);
      {
        std::lock_guard<std::mutex> lk(mu_);
        health_ = "OK";
        detail_ = "recovered";
      }
      std::cout << "[dummy] recovered\n";
      return "OK";
    }

    if (line == "CRASH") {
      std::cout << "[dummy] crashing via abort()\n";
      std::abort();
    }

    if (starts_with(line, "EXIT ")) {
      int code = std::stoi(line.substr(std::string("EXIT ").size()));
      std::cout << "[dummy] exiting with code " << code << "\n";
      std::exit(code);
    }

    return "ERR detail=unknown_command";
  }

  void apply_delay_if_needed() {
    int ms = delay_ms_.load();
    if (ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
  }

  static bool starts_with(const std::string &s, const std::string &prefix) {
    return s.rfind(prefix, 0) == 0;
  }

private:
  std::string sock_path_;
  std::atomic<bool> running_{true};
  std::atomic<bool> hang_{false};
  std::atomic<int> delay_ms_{0};

  std::mutex mu_;
  std::string mode_{"INIT"};
  std::string health_{"OK"};
  std::string detail_{"booted"};
};

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <sock_path>\n";
    return 1;
  }

  DummyProcess proc(argv[1]);
  proc.run();
  return 0;
}

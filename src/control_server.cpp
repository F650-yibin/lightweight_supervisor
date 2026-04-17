#include "control_server.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>

namespace supervisor {

ControlServer::ControlServer(std::string socket_path, Handler handler)
    : socket_path_(std::move(socket_path)), handler_(std::move(handler)) {}

ControlServer::~ControlServer() {
  stop();
}

void ControlServer::start() {
  running_ = true;
  th_ = std::thread([this] { run_(); });
}

void ControlServer::stop() {
  running_ = false;
  poke_();
  if (th_.joinable()) th_.join();
}

void ControlServer::run_() {
  std::filesystem::remove(socket_path_);

  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return;

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return;
  }

  if (::listen(fd, 16) != 0) {
    ::close(fd);
    return;
  }

  while (running_) {
    int cfd = ::accept(fd, nullptr, nullptr);
    if (cfd < 0) continue;

    std::string line;
    char ch;
    while (true) {
      ssize_t n = ::recv(cfd, &ch, 1, 0);
      if (n <= 0) break;
      if (ch == '\n') break;
      line.push_back(ch);
    }

    std::string resp = handler_(line);
    if (resp.empty() || resp.back() != '\n') resp.push_back('\n');

    ::send(cfd, resp.data(), resp.size(), 0);
    ::close(cfd);
  }

  ::close(fd);
  std::filesystem::remove(socket_path_);
}

void ControlServer::poke_() {
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return;

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
  ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  ::close(fd);
}

} // namespace supervisor
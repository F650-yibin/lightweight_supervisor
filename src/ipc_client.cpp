#include "ipc_client.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace supervisor {

namespace {
struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

timeval to_timeval(std::chrono::milliseconds timeout) {
  if (timeout.count() < 0)
    timeout = std::chrono::milliseconds(0);

  const auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout - secs);

  timeval tv{};
  tv.tv_sec = static_cast<decltype(tv.tv_sec)>(secs.count());
  tv.tv_usec = static_cast<decltype(tv.tv_usec)>(micros.count());

  if (tv.tv_sec == 0 && tv.tv_usec == 0)
    tv.tv_usec = 1;
  return tv;
}
} // namespace

std::string IpcClient::request(const std::string &sock_path, const std::string &line,
                               std::chrono::milliseconds timeout) {
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    throw Error("socket() failed");

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (sock_path.size() >= sizeof(addr.sun_path)) {
    ::close(fd);
    throw Error("socket path too long: " + sock_path);
  }
  std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
      break;
    if (std::chrono::steady_clock::now() > deadline) {
      ::close(fd);
      throw Error("connect timeout: " + sock_path);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
    deadline - std::chrono::steady_clock::now());
  if (remaining.count() <= 0) {
    ::close(fd);
    throw Error("request timeout: " + sock_path);
  }

  const auto tv = to_timeval(remaining);
  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
      ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
    ::close(fd);
    throw Error("setsockopt timeout failed: " + sock_path);
  }

  std::string msg = line;
  if (msg.empty() || msg.back() != '\n')
    msg.push_back('\n');

  if (::send(fd, msg.data(), msg.size(), 0) < 0) {
    ::close(fd);
    throw Error("send failed: " + sock_path);
  }

  std::string resp;
  char ch;
  while (true) {
    ssize_t n = ::recv(fd, &ch, 1, 0);
    if (n == 0)
      break;
    if (n < 0) {
      ::close(fd);
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        throw Error("recv timeout: " + sock_path);
      }
      throw Error("recv failed: " + sock_path);
    }
    if (ch == '\n')
      break;
    resp.push_back(ch);
  }

  ::close(fd);
  if (resp.empty())
    throw Error("empty response: " + sock_path);
  return resp;
}

} // namespace supervisor
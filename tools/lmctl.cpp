#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* RESET  = "\033[0m";
constexpr const char* RED    = "\033[31m";
constexpr const char* GREEN  = "\033[32m";
constexpr const char* YELLOW = "\033[33m";
constexpr const char* BLUE   = "\033[34m";
constexpr const char* CYAN   = "\033[36m";
constexpr const char* BOLD   = "\033[1m";

struct StatusSummary {
  int running{0};
  int degraded{0};
  int exited{0};
  int stopped{0};
  int failed{0};
  int reachable{0};
  int unreachable{0};
};

void print_usage(const char* prog) {
  std::cerr << "usage:\n";
  std::cerr << "  " << prog << " [--socket <path>] <command...>\n\n";
  std::cerr << "examples:\n";
  std::cerr << "  " << prog << " ping\n";
  std::cerr << "  " << prog << " start\n";
  std::cerr << "  " << prog << " stop\n";
  std::cerr << "  " << prog << " status\n";
  std::cerr << "  " << prog << " watch\n";
  std::cerr << "  " << prog << " watch 0.5 15 --errors\n";
  std::cerr << "  " << prog << " mode AUTO\n";
  std::cerr << "  " << prog << " proc_status controller\n";
  std::cerr << "  " << prog << " restart planner\n";
  std::cerr << "  " << prog << " stop_proc planner\n";
  std::cerr << "  " << prog << " start_proc planner\n";
  std::cerr << "  " << prog << " events\n";
  std::cerr << "  " << prog << " child planner GET_STATUS\n";
  std::cerr << "  " << prog << " child planner SET_HEALTH ERR\n";
  std::cerr << "  " << prog << " child planner CRASH\n";
}

int connect_unix_socket(const std::string& sock_path) {
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    std::perror("socket");
    return -1;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;

  if (sock_path.size() >= sizeof(addr.sun_path)) {
    std::cerr << "socket path too long: " << sock_path << "\n";
    ::close(fd);
    return -1;
  }

  std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::perror(("connect " + sock_path).c_str());
    ::close(fd);
    return -1;
  }

  return fd;
}

bool run_request(const std::string& socket_path,
                 const std::string& cmd,
                 std::string* response_out) {
  int fd = connect_unix_socket(socket_path);
  if (fd < 0) return false;

  std::string msg = cmd;
  if (msg.empty() || msg.back() != '\n') msg.push_back('\n');

  if (::send(fd, msg.data(), msg.size(), 0) < 0) {
    std::perror("send");
    ::close(fd);
    return false;
  }

  std::ostringstream oss;
  char buf[1024];
  while (true) {
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n < 0) {
      std::perror("recv");
      ::close(fd);
      return false;
    }
    if (n == 0) break;
    oss.write(buf, n);
  }

  ::close(fd);
  *response_out = oss.str();
  return true;
}

bool contains(const std::string& s, const std::string& needle) {
  return s.find(needle) != std::string::npos;
}

double parse_watch_interval(const std::vector<std::string>& cmd_parts) {
  if (cmd_parts.size() < 2) return 1.0;
  try {
    return std::stod(cmd_parts[1]);
  } catch (...) {
    return 1.0;
  }
}

int parse_watch_event_count(const std::vector<std::string>& cmd_parts) {
  if (cmd_parts.size() < 3) return 10;
  try {
    int n = std::stoi(cmd_parts[2]);
    return n > 0 ? n : 10;
  } catch (...) {
    return 10;
  }
}

void clear_screen() {
  std::cout << "\033[2J\033[H";
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

StatusSummary summarize_status(const std::string& status_resp) {
  StatusSummary sum;
  std::istringstream iss(status_resp);
  std::string line;

  while (std::getline(iss, line)) {
    if (!contains(line, "proc ")) continue;

    if (contains(line, "state=Running"))  sum.running++;
    if (contains(line, "state=Degraded")) sum.degraded++;
    if (contains(line, "state=Exited"))   sum.exited++;
    if (contains(line, "state=Stopped"))  sum.stopped++;
    if (contains(line, "state=Failed"))   sum.failed++;

    if (contains(line, "reachable=true"))  sum.reachable++;
    if (contains(line, "reachable=false")) sum.unreachable++;
  }

  return sum;
}

bool is_abnormal_status_line(const std::string& line) {
  if (!contains(line, "proc ")) return false;

  return contains(line, "state=Degraded") ||
         contains(line, "state=Exited") ||
         contains(line, "state=Stopped") ||
         contains(line, "state=Failed") ||
         contains(line, "reachable=false") ||
         contains(line, "health=ERR") ||
         contains(line, "health=FAIL");
}

bool is_abnormal_event_line(const std::string& line) {
  return contains(line, "level=WARN") || contains(line, "level=ERROR");
}

std::string filter_status_block(const std::string& input, bool errors_only) {
  if (!errors_only) return input;

  std::istringstream iss(input);
  std::ostringstream oss;
  std::string line;
  bool found = false;

  while (std::getline(iss, line)) {
    if (line.rfind("OK ", 0) == 0) {
      oss << line << "\n";
      continue;
    }
    if (line == ".") continue;
    if (is_abnormal_status_line(line)) {
      oss << line << "\n";
      found = true;
    }
  }

  if (!found) {
    oss << "OK no abnormal processes\n";
  }
  oss << ".";
  return oss.str();
}

std::string filter_events_block(const std::string& input, bool errors_only) {
  if (!errors_only) return input;

  std::istringstream iss(input);
  std::ostringstream oss;
  std::string line;
  bool found = false;

  while (std::getline(iss, line)) {
    if (line.rfind("OK ", 0) == 0) {
      oss << line << "\n";
      continue;
    }
    if (line == ".") continue;
    if (is_abnormal_event_line(line)) {
      oss << line << "\n";
      found = true;
    }
  }

  if (!found) {
    oss << "OK no warning/error events\n";
  }
  oss << ".";
  return oss.str();
}

std::string colorize_line(std::string line) {
  line = replace_all(line, "health=ERR", std::string("health=") + RED + "ERR" + RESET);
  line = replace_all(line, "health=FAIL", std::string("health=") + RED + "FAIL" + RESET);
  line = replace_all(line, "state=Exited", std::string("state=") + RED + "Exited" + RESET);
  line = replace_all(line, "state=Stopped", std::string("state=") + RED + "Stopped" + RESET);
  line = replace_all(line, "state=Failed", std::string("state=") + RED + "Failed" + RESET);
  line = replace_all(line, "reachable=false", std::string(RED) + "reachable=false" + RESET);
  line = replace_all(line, "level=ERROR", std::string("level=") + RED + "ERROR" + RESET);

  line = replace_all(line, "state=Degraded", std::string("state=") + YELLOW + "Degraded" + RESET);
  line = replace_all(line, "level=WARN", std::string("level=") + YELLOW + "WARN" + RESET);
  line = replace_all(line, "child_mode=SAFE", std::string("child_mode=") + YELLOW + "SAFE" + RESET);
  line = replace_all(line, "manager_mode=SAFE", std::string("manager_mode=") + YELLOW + "SAFE" + RESET);

  line = replace_all(line, "health=OK", std::string("health=") + GREEN + "OK" + RESET);
  line = replace_all(line, "state=Running", std::string("state=") + GREEN + "Running" + RESET);
  line = replace_all(line, "reachable=true", std::string(GREEN) + "reachable=true" + RESET);
  line = replace_all(line, "level=INFO", std::string("level=") + GREEN + "INFO" + RESET);
  line = replace_all(line, "child_mode=AUTO", std::string("child_mode=") + CYAN + "AUTO" + RESET);
  line = replace_all(line, "child_mode=TELEOP", std::string("child_mode=") + CYAN + "TELEOP" + RESET);
  line = replace_all(line, "child_mode=IDLE", std::string("child_mode=") + CYAN + "IDLE" + RESET);
  line = replace_all(line, "child_mode=DIAG", std::string("child_mode=") + CYAN + "DIAG" + RESET);

  line = replace_all(line, "manager_state=RUNNING", std::string("manager_state=") + GREEN + "RUNNING" + RESET);
  line = replace_all(line, "manager_state=ERROR", std::string("manager_state=") + RED + "ERROR" + RESET);
  line = replace_all(line, "manager_state=STOPPED", std::string("manager_state=") + RED + "STOPPED" + RESET);
  line = replace_all(line, "manager_state=STARTING", std::string("manager_state=") + YELLOW + "STARTING" + RESET);
  line = replace_all(line, "manager_state=STOPPING", std::string("manager_state=") + YELLOW + "STOPPING" + RESET);

  if (contains(line, "no abnormal processes")) line = std::string(GREEN) + line + RESET;
  if (contains(line, "no warning/error events")) line = std::string(GREEN) + line + RESET;

  return line;
}

std::string colorize_block(const std::string& input) {
  std::istringstream iss(input);
  std::ostringstream oss;
  std::string line;

  while (std::getline(iss, line)) {
    oss << colorize_line(line) << "\n";
  }
  return oss.str();
}

void print_section_header(const std::string& title) {
  std::cout << BOLD << BLUE << "==== " << title << " ====" << RESET << "\n";
}

void print_summary(const StatusSummary& s) {
  std::cout << BOLD << "summary " << RESET;
  std::cout << "running=" << GREEN << s.running << RESET << " ";
  std::cout << "degraded=" << (s.degraded > 0 ? YELLOW : GREEN) << s.degraded << RESET << " ";
  std::cout << "exited=" << (s.exited > 0 ? RED : GREEN) << s.exited << RESET << " ";
  std::cout << "stopped=" << (s.stopped > 0 ? RED : GREEN) << s.stopped << RESET << " ";
  std::cout << "failed=" << (s.failed > 0 ? RED : GREEN) << s.failed << RESET << " ";
  std::cout << "reachable=" << GREEN << s.reachable << RESET << " ";
  std::cout << "unreachable=" << (s.unreachable > 0 ? RED : GREEN) << s.unreachable << RESET << "\n";
}

void run_watch(const std::string& socket_path,
               double interval_sec,
               int event_count,
               bool errors_only) {
  if (interval_sec <= 0.0) interval_sec = 1.0;
  if (event_count <= 0) event_count = 10;

  const auto sleep_dur = std::chrono::duration<double>(interval_sec);

  while (true) {
    std::string status_resp;
    std::string events_resp;

    bool status_ok = run_request(socket_path, "status", &status_resp);
    bool events_ok = run_request(socket_path, "events " + std::to_string(event_count), &events_resp);

    clear_screen();
    std::cout << BOLD << CYAN
              << "lmctl watch"
              << RESET
              << "  socket=" << socket_path
              << "  interval=" << interval_sec << "s"
              << "  events=" << event_count
              << "  filter=" << (errors_only ? "errors-only" : "all")
              << "\n\n";

    print_section_header("STATUS");
    if (status_ok) {
      auto summary = summarize_status(status_resp);
      print_summary(summary);
      std::cout << "\n";
      auto filtered = filter_status_block(status_resp, errors_only);
      std::cout << colorize_block(filtered);
    } else {
      std::cout << RED << "ERR failed to query status" << RESET << "\n";
    }

    std::cout << "\n";
    print_section_header("RECENT EVENTS");
    if (events_ok) {
      auto filtered = filter_events_block(events_resp, errors_only);
      std::cout << colorize_block(filtered);
    } else {
      std::cout << RED << "ERR failed to query events" << RESET << "\n";
    }

    std::cout.flush();
    std::this_thread::sleep_for(sleep_dur);
  }
}

} // namespace

int main(int argc, char** argv) {
  const std::string default_socket = "/tmp/robot/lifecycle_manager.sock";
  std::string socket_path = default_socket;
  bool errors_only = false;

  std::vector<std::string> cmd_parts;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--socket") {
      if (i + 1 >= argc) {
        std::cerr << "--socket requires a path\n";
        print_usage(argv[0]);
        return 1;
      }
      socket_path = argv[++i];
      continue;
    }
    if (arg == "--errors") {
      errors_only = true;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    }
    cmd_parts.push_back(arg);
  }

  if (cmd_parts.empty()) {
    print_usage(argv[0]);
    return 1;
  }

  if (cmd_parts[0] == "watch") {
    double interval_sec = parse_watch_interval(cmd_parts);
    int event_count = parse_watch_event_count(cmd_parts);
    run_watch(socket_path, interval_sec, event_count, errors_only);
    return 0;
  }

  std::string cmd;
  for (size_t i = 0; i < cmd_parts.size(); ++i) {
    if (i) cmd += " ";
    cmd += cmd_parts[i];
  }

  std::string response;
  if (!run_request(socket_path, cmd, &response)) {
    return 1;
  }

  std::cout << response;
  return 0;
}

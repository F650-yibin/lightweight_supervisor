cmake_minimum_required(VERSION 3.16)

project(lightweight_supervisor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
)

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(supervisor_lib
  src/supervisor.cpp
  src/ipc_client.cpp
  src/control_server.cpp
  src/config_loader.cpp
  src/child_status_json.cpp
)

target_include_directories(supervisor_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(supervisor_lib
  PUBLIC
    Threads::Threads
    nlohmann_json::nlohmann_json
)

add_executable(supervisord
  src/main.cpp
)

target_link_libraries(supervisord
  PRIVATE
    supervisor_lib
)

add_executable(lmctl
  tools/lmctl.cpp
)

target_link_libraries(lmctl
  PRIVATE
    Threads::Threads
)

add_executable(dummy_process
  examples/dummy_process.cpp
)

target_link_libraries(dummy_process
  PRIVATE
    Threads::Threads
)

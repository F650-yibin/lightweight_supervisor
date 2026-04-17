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

add_executable(planner_process
  examples/planner_process.cpp
  examples/thread_manager.cpp
)

target_include_directories(planner_process
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples
)

target_link_libraries(planner_process
  PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
)

find_program(CLANG_FORMAT_BIN clang-format)

if(CLANG_FORMAT_BIN)
  file(GLOB_RECURSE ALL_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.hpp
  )

  add_custom_target(format
    COMMAND ${CLANG_FORMAT_BIN} -i ${ALL_SOURCE_FILES}
    COMMENT "Formatting C++ sources with clang-format"
  )

  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_BIN} --dry-run --Werror ${ALL_SOURCE_FILES}
    COMMENT "Checking C++ formatting with clang-format"
  )
endif()
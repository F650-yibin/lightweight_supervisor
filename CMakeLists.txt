cmake_minimum_required(VERSION 3.10)

# Project name
project(lightweight_supervisor)

# Define the executable targets
add_executable(supervisord src/supervisor.cpp src/ipc_client.cpp src/control_server.cpp src/config_loader.cpp src/main.cpp)
add_executable(lmctl tools/lmctl.cpp)
add_executable(dummy_process examples/dummy_process.cpp)

# Link libraries
find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)
target_link_libraries(supervisord PRIVATE nlohmann_json::nlohmann_json pthread)
target_link_libraries(lmctl PRIVATE nlohmann_json::nlohmann_json pthread)
target_link_libraries(dummy_process PRIVATE nlohmann_json::nlohmann_json pthread)
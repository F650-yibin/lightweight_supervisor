# lightweight_supervisor

A lightweight local robot process supervisor based on Unix domain socket IPC.

## Features

- resident process supervision
- dependency-aware startup
- mode switching through IPC
- SAFE-first actuator handling
- aggregated process status
- local CLI (`lmctl`)
- fault injection via dummy child processes
- watch mode with colors, summary, and error filtering

## Managed Modules

```text
base_driver   chassis / IMU / odometry driver
lidar         LiDAR sensor process
camera        camera sensor process
map_manager   map lifecycle / map data management
localization  robot pose estimation / localization
perception    environment perception
task_manager  high-level task / mission manager
planner       path / behavior planner
controller    actuator-side control executor
hmi           human-machine interface / local operator UI
```

## AMR Layering

```text
Sensors and platform I/O:
  base_driver
  lidar
  camera

Infrastructure:
  map_manager

Core perception and state:
  localization
  perception

Decision layer:
  task_manager

Planning layer:
  planner

Execution layer:
  controller

Interaction layer:
  hmi
```

## Directory Layout

```text
include/     public headers
src/         supervisor implementation
tools/       CLI tools
examples/    dummy and example child processes
docs/        architecture documents
```

## Build

```bash
mkdir -p build
cd build
cmake ..
make -j
```

## Run

Start supervisor:

```bash
./supervisord ../supervisor_config.json
```

Common commands:

```bash
./lmctl start
./lmctl status
./lmctl mode AUTO
./lmctl watch
./lmctl watch 0.5 20 --errors
./lmctl proc_status planner
./lmctl child planner GET_STATUS
./lmctl child planner SET_HEALTH ERR
./lmctl child planner CRASH
```

## Recommended Dependency Semantics

Recommended AMR-style dependencies:

```text
base_driver  <- []
lidar        <- []
camera       <- []
map_manager  <- []

localization <- base_driver, lidar, map_manager
perception   <- lidar, camera
task_manager <- localization, map_manager
planner      <- task_manager, localization, perception, map_manager
controller   <- localization, planner, base_driver
hmi          <- task_manager, map_manager
```

## Module Responsibilities

### base_driver
Responsible for:
- chassis communication
- odometry input/output
- IMU / wheel-state / low-level platform state
- low-level base/platform interface

### map_manager
Responsible for:
- map loading and switching
- map metadata and semantic area management
- station / charger / zone definitions
- virtual walls / restricted areas
- providing map-related information to localization, task_manager, planner, and hmi

### localization
Responsible for:
- robot pose estimation
- position / heading tracking
- state estimation based on sensor and map inputs

### perception
Responsible for:
- obstacle/environment understanding
- processing lidar/camera-derived environment information
- providing scene information to planning/decision layers

### task_manager
Responsible for:
- mission/task orchestration
- task state machine
- high-level goal management
- deciding what the robot should do next
- mission completion decisions based on upstream/downstream feedback

### planner
Responsible for:
- converting task goals into executable plans
- path planning
- behavior planning
- trajectory generation for downstream control
- returning planning / navigation execution status to task_manager

### controller
Responsible for:
- executing planned motion/control references
- actuator-facing control behavior
- safe stop / safe mode behavior

### hmi
Responsible for:
- local operator interaction
- displaying robot/task/system status
- presenting maps, goals, and alerts
- sending operator commands to upper-level modules

Recommended properties:
- non-critical by default
- should not block the core motion/control chain
- should not be required by localization, planner, or controller

## Configuration

Process topology and runtime policy are defined in:

```text
supervisor_config.json
```

## Architecture

See:

```text
docs/architecture.md
```
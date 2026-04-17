# Lightweight Supervisor Architecture

## Overview

This project provides a lightweight local supervisor for robot processes.

Core goals:

- all major processes stay resident
- mode switching is done through IPC
- actuator process enters active mode last and enters SAFE first
- process health is aggregated centrally
- local CLI is provided for control and debugging

---

## High-Level Architecture

```text
+-------------------+
|       lmctl       |
|  CLI / watch /    |
| child forwarding  |
+---------+---------+
          |
          | Unix Domain Socket
          v
+---------------------------+
|       supervisord         |
|---------------------------|
| Control Server            |
| Supervisor Core           |
| Process Monitor Thread    |
| Status Poll Thread        |
| Event Buffer              |
+----+----------+-----------+
     |          |
     |          |
     |          +-----------------------------------------------------------------+
     |                                                                            |
     | IPC: PING / GET_STATUS / SET_MODE                                          |
     v                                                                            v
+-------------+  +-----------+  +-----------+  +-------------+  +--------------+  +-----------+  +------+
| base_driver |  |   lidar   |  |  camera   |  | map_manager |  | localization |  | perception|  | hmi  |
+-------------+  +-----------+  +-----------+  +-------------+  +--------------+  +-----------+  +------+
       |                                                       ^               ^                     ^
       |                                                       |               |                     |
       +-------------------------------------------------------+               |                     |
                                                                               |                     |
                                                       +-----------------------+                     |
                                                       |                                             |
                                                       v                                             |
                                                +--------------+ ------------------------------------+
                                                | task_manager |
                                                +--------------+
                                                       |
                                                       v
                                                   +--------+ <------------------------------+
                                                   | planner|                                |
                                                   +--------+                                |
                                                        |                                    |
                                                        v                                    |
                                                   +-----------+                             |
                                                   | controller|                             |
                                                   +-----------+                             |
```

---

## AMR Functional Layers

### Sensors and Platform I/O Layer
- base_driver
- lidar
- camera

### Infrastructure Layer
- map_manager

### Perception and State Layer
- localization
- perception

### Decision Layer
- task_manager

### Planning Layer
- planner

### Execution Layer
- controller

### Interaction Layer
- hmi

---

## Dependency Graph

Recommended dependency semantics:

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

---

## Why hmi should be separate

`hmi` is the human-machine interface module.

Typical responsibilities:
- display robot state
- display task/mission state
- present alerts and operator feedback
- show map/goal information
- send human-triggered commands

It is usually best treated as:
- an upper-layer interaction module
- non-real-time
- non-safety-critical by default

So it should generally **not** be a dependency of:
- `localization`
- `planner`
- `controller`

A UI failure should not directly break the core autonomy/control chain.

---

## Why base_driver should be independent

`base_driver` is the platform/chassis-facing module.

It is responsible for:
- wheel odometry / chassis state
- IMU integration
- low-level platform communication
- exposing robot base status to upper layers

It is reasonable to group:
- IMU
- odometry
- chassis state

into one module because they are often tightly coupled to the robot base hardware and communication channel.

However, it is usually not ideal to merge:
- lidar
- camera

into the same process, because they have different resource profiles and fault domains.

---

## Why map_manager should be independent

`map_manager` is a foundational service module.

It is responsible for:
- map loading
- map switching
- map versioning
- semantic regions / station definitions
- virtual walls / restricted areas
- exposing map-related information to other modules

It should not be embedded primarily inside:
- `planner`
- `task_manager`
- `localization`

These modules consume map information, but they should not own the full map lifecycle.

---

## Why task_manager is above planner

### task_manager
`task_manager` is the high-level mission/task orchestration module.

It answers:

- what the robot should do next
- which mission step is active
- whether a task is completed, paused, canceled, or failed
- what high-level goal should be sent downstream
- whether the current mission should move to the next stage

Typical outputs:
- go to station A
- go to charger
- execute pickup mission
- pause current mission
- resume previous mission

### planner
`planner` is the motion/behavior planning module.

It answers:

- how the robot should accomplish the current goal
- which path to take
- how to avoid obstacles
- what trajectory or motion reference should be sent to control
- whether the current navigation/planning target has succeeded or failed

Typical outputs:
- global path
- local trajectory
- speed/steering references
- replanning decisions
- goal reached / blocked / planning failed

In short:

```text
task_manager = decide what to do
planner      = decide how to do it
controller   = execute it
```

---

## Mission Completion Semantics

Mission completion should generally be decided by `task_manager`, not directly by `planner`.

### planner reports
Examples:
- goal reached
- blocked
- planning failed
- executing
- canceled

These are planning/execution-layer results.

### task_manager decides
Examples:
- current task stage completed
- transition to next waypoint / next station
- mission completed
- mission paused / retried / failed

This keeps business/task logic separated from motion logic.

---

## Mode Transition Strategy

### Enter working modes
Target modes:
- AUTO
- TELEOP
- DIAG

Order:
1. Sensor / Platform I/O
2. Compute
3. Actuator

This ensures `controller` is enabled last.

### Enter SAFE or IDLE
Order:
1. Actuator
2. Compute
3. Sensor / Platform I/O

This ensures `controller` is disabled first.

---

## Supervisor Responsibilities

- load process configuration
- spawn child processes
- monitor child exit events
- poll child health/status
- aggregate status for CLI
- enter SAFE when critical failures happen
- optionally restart restartable processes
- forward child debug commands

---

## Child Process IPC Contract

Each child process exposes a local Unix domain socket.

Supported commands:

- `PING`
- `GET_STATUS`
- `SET_MODE <mode>`

For testing, dummy processes may also support:

- `SET_HEALTH <OK|ERR|FAIL>`
- `SET_DETAIL <text>`
- `SET_DELAY_MS <n>`
- `CRASH`
- `EXIT <code>`
- `HANG`
- `RECOVER`

---

## Main Threads

### Control thread
Handles external requests from `lmctl`.

### Monitor thread
The only thread allowed to reap child processes.

### Poll thread
Periodically queries:
- `PING`
- `GET_STATUS`

This updates aggregated process state.

---

## Status Model

Supervisor-level state examples:
- INIT
- STARTING
- RUNNING
- STOPPING
- STOPPED
- ERROR

Process-level state examples:
- Stopped
- Starting
- Running
- Degraded
- Stopping
- Exited
- Failed

---

## Typical Debug Flow

1. start supervisor
2. start all managed processes
3. use `lmctl watch`
4. inject faults with `lmctl child <proc> ...`
5. observe status and recent events
6. verify SAFE transition / restart behavior
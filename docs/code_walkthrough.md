# lightweight_supervisor Code Walkthrough

This document explains the `lightweight_supervisor` repository as a learning-oriented walkthrough.

The goal is to help readers understand:

- what this project does
- how the architecture is organized
- how the major source files fit together
- how process supervision works
- how IPC is implemented
- how the example child processes interact with the supervisor

---

## 1. What this repository is

`lightweight_supervisor` is a lightweight local process supervision framework for robot modules.

It is designed to manage a group of local processes such as:

- `base_driver`
- `lidar`
- `camera`
- `map_manager`
- `localization`
- `perception`
- `task_manager`
- `planner`
- `controller`
- `hmi`

The project is **not** a robot algorithm repository.  
It is a runtime lifecycle and health management layer.

Its core responsibilities are:

- starting processes
- stopping processes
- dependency-aware startup
- health/status polling
- monitoring unexpected exits
- switching modes
- entering SAFE on critical failures
- providing a local CLI for control and debugging

In short:

> this repository implements a lightweight local supervisor for robot processes.

---

## 2. High-level architecture

There are three main roles in the system.

### 2.1 `supervisord`
This is the central management process.

It is responsible for:

- loading configuration
- creating the control socket
- starting child processes
- stopping child processes
- monitoring child exits
- polling child status
- handling operator commands
- maintaining an event log
- aggregating system status

This is the core of the system.

---

### 2.2 Child processes
These are the managed modules, such as:

- `dummy_process`
- `planner_process`
- and any processes defined in `supervisor_config.json`

Each child exposes a local Unix Domain Socket control endpoint.

The supervisor sends commands such as:

- `PING`
- `GET_STATUS`
- `SET_MODE <mode>`

So each child is expected to implement a simple control interface.

---

### 2.3 `lmctl`
This is the CLI tool.

It connects to the supervisor control socket and sends commands like:

- `start`
- `stop`
- `status`
- `events`
- `mode AUTO`
- `proc_status planner`
- `child planner GET_STATUS`

It acts as the operator-facing control entry point.

---

## 3. Repository layout

The repository can be understood in several layers.

### 3.1 `include/`
Header files that define public interfaces and core types.

Typical responsibilities:

- supervisor interface
- process configuration/runtime types
- IPC client interface
- control server interface
- config loader interface
- child JSON status parsing interface

This is the abstraction layer.

---

### 3.2 `src/`
Core implementation files.

This is where the main logic lives:

- `supervisor.cpp`
- `ipc_client.cpp`
- `control_server.cpp`
- `config_loader.cpp`
- `child_status_json.cpp`
- `main.cpp`

This is the actual engine of the system.

---

### 3.3 `tools/`
Command-line tools.

Currently this mainly contains:

- `lmctl.cpp`

This is the operator/control tooling layer.

---

### 3.4 `examples/`
Example child processes.

Includes:

- `dummy_process.cpp`
- `planner_process.cpp`
- `thread_manager.cpp`
- `thread_manager.hpp`

These files are very important for learning because they show how a managed child process is expected to behave.

---

### 3.5 `docs/`
Architecture documentation.

For example:

- `docs/architecture.md`

This helps explain the system design and intended module relationships.

---

### 3.6 Repository root files
Important files include:

- `CMakeLists.txt`
- `supervisor_config.json`
- `README.md`

These define:

- how the project is built
- how processes are configured
- how the project is described and used

---

## 4. Main binaries

The project builds several important binaries.

### 4.1 `supervisord`
The main supervisor process.

Responsibilities:

- load process configuration
- manage lifecycle of child processes
- expose a control interface
- poll child health
- react to failures
- coordinate mode switching

This is the most important executable in the project.

---

### 4.2 `lmctl`
The CLI tool.

Used to:

- connect to supervisor
- send commands
- print status and events
- run watch mode
- forward raw child commands through supervisor

---

### 4.3 `dummy_process`
A simple example child process.

Used for testing:

- liveness checks
- mode switching
- status reporting
- fault injection

This is the easiest child process to study first.

---

### 4.4 `planner_process`
A more advanced example child process.

It demonstrates:

- multiple internal threads
- watchdog-based monitoring
- JSON status reporting
- more realistic internal health tracking

This is closer to how a real robot module might behave.

---

## 5. Core concepts and data structures

To understand the codebase, the most important types are the following.

---

### 5.1 `ProcessSpec`
This is the static process configuration model.

It describes how one managed process should be supervised.

Typical fields include:

- `name`
- `argv`
- `deps`
- `control_sock`
- `role`
- `critical`
- `restart_policy`
- `max_restart_count`
- `restart_backoff`
- `start_timeout`
- `stop_timeout`
- `ping_timeout`
- `status_timeout`
- `graceful_signal`
- `kill_on_timeout`

You can think of it as:

> how this module should be started, managed, and recovered

---

### 5.2 `ChildStatus`
This is the supervisor-side view of child health and mode.

Typical fields:

- `mode`
- `health`
- `detail`
- `reachable`

Meaning:

- `mode`: child mode such as `SAFE`, `AUTO`, `DIAG`
- `health`: `OK`, `ERR`, `FAIL`, `UNKNOWN`
- `detail`: extra status text
- `reachable`: whether IPC communication currently works

---

### 5.3 `ProcessRuntime`
This is the dynamic runtime state for a managed process.

Typical fields:

- `pid`
- `pgid`
- `state`
- `last_exit_code`
- `restart_count`
- `child_status`

This is different from `ProcessSpec`:

- `ProcessSpec` = desired configuration
- `ProcessRuntime` = actual current runtime state

---

### 5.4 `ProcState`
This is the lifecycle state of a process from the supervisor point of view.

Common values:

- `Stopped`
- `Starting`
- `Running`
- `Degraded`
- `Stopping`
- `Exited`
- `Failed`

Meaning:

- `Stopped`: not running
- `Starting`: launched and waiting for readiness
- `Running`: healthy and reachable
- `Degraded`: process exists but health/reachability is not normal
- `Stopping`: in shutdown flow
- `Exited`: unexpected process exit
- `Failed`: startup failure or similar failure condition

---

### 5.5 `Role`
Module role classification:

- `Sensor`
- `Compute`
- `Actuator`

This is mainly used to order mode transitions safely.

For example:

- entering working mode: Sensor -> Compute -> Actuator
- entering SAFE: Actuator -> Compute -> Sensor

This ensures actuators are enabled last and disabled first.

---

### 5.6 `RestartPolicy`
Controls whether a process should be restarted after exit.

Typical values:

- `Never`
- `OnFailure`
- `Always`

---

## 6. Main control flow of the supervisor

This is the most important part of the project.

---

### 6.1 Program startup
`src/main.cpp` typically does the following:

1. loads `supervisor_config.json`
2. constructs a `Supervisor`
3. adds all `ProcessSpec` objects
4. starts the control interface
5. starts background threads
6. enters the main runtime phase

The real logic lives inside the `Supervisor` class.

---

### 6.2 `add_process`
This registers one configured process with the supervisor.

It validates things like:

- non-empty process name
- non-empty argv
- non-empty control socket
- no duplicate names

Then it stores:

- static config in `specs_`
- runtime state in `runtime_`

This is how the in-memory process graph is built.

---

### 6.3 `start_control_interface`
This creates the supervisor control socket server.

Example socket path:

- `/tmp/robot/lifecycle_manager.sock`

`lmctl` connects to this socket to operate the system.

---

### 6.4 `run_background_threads`
The supervisor starts two important background threads:

- monitor thread
- poll thread

#### Monitor thread
Responsible for checking whether child processes exited.

It uses `waitpid(..., WNOHANG)` to observe child exit events.

#### Poll thread
Responsible for regularly sending:

- `PING`
- `GET_STATUS`

to child processes and refreshing internal state.

---

### 6.5 `start_all`
When the user runs:

```bash
./lmctl start
```

the supervisor eventually calls `start_all()`.

This function:

1. computes dependency order
2. launches processes in topological order
3. waits for each child to become reachable
4. stores start order
5. marks the manager state as running
6. applies initial safe mode

The dependency graph matters because upper-layer modules must not start before required lower-layer modules are ready.

---

### 6.6 `stop_all`
When the user runs:

```bash
./lmctl stop
```

the supervisor calls `stop_all()`.

This function:

1. enters SAFE first
2. shuts down processes in reverse start order
3. waits for graceful stop
4. uses force-kill if needed
5. marks the manager state as stopped

Stopping in reverse order preserves dependency correctness.

---

### 6.7 `set_mode_all`
When the user runs:

```bash
./lmctl mode AUTO
```

the supervisor calls `set_mode_all()`.

It validates the mode and applies it in role-aware order.

Working modes:

1. Sensor
2. Compute
3. Actuator

SAFE / IDLE:

1. Actuator
2. Compute
3. Sensor

This is a safety-oriented transition strategy.

---

### 6.8 `refresh_child_status_`
This is one of the key functions in the supervisor.

It performs the following steps for one child:

1. reads current spec/runtime
2. sends `PING`
3. sends `GET_STATUS`
4. parses the returned status
5. updates cached child status
6. updates supervisor-side process state

This is how the supervisor learns whether a child is reachable and healthy.

---

## 7. IPC design

The primary IPC mechanism in this repository is:

- Unix Domain Socket
- local request-response style
- text commands and JSON status payloads

---

### 7.1 Why Unix Domain Socket
This project is focused on local process supervision on one machine.

UDS is a good fit because:

- no TCP port management is needed
- communication is local-only
- socket paths are easy to inspect
- implementation is lightweight
- request-response control semantics map naturally

Typical socket paths look like:

- `/tmp/robot/lifecycle_manager.sock`
- `/tmp/robot/planner.sock`
- `/tmp/robot/base_driver.sock`

---

### 7.2 Supervisor to child protocol
The supervisor talks to child processes using commands like:

- `PING`
- `GET_STATUS`
- `SET_MODE <mode>`

Meaning:

- `PING`: confirm reachability
- `GET_STATUS`: retrieve health/mode/status
- `SET_MODE`: request mode transition

---

### 7.3 `IpcClient`
This component is the supervisor-side client used to talk to child processes.

Its responsibilities include:

- opening Unix socket connections
- sending request strings
- reading response strings
- handling timeout behavior

It acts like a lightweight RPC client.

---

### 7.4 `ControlServer`
This is the socket server used by `supervisord`.

Responsibilities:

- listen on supervisor control socket
- accept requests from `lmctl`
- dispatch requests to `Supervisor::handle_command_()`
- return response text

This is the external control surface of the supervisor.

---

## 8. How `lmctl` works

`lmctl` is a simple command-line client.

Its flow is roughly:

1. parse CLI arguments
2. connect to the supervisor control socket
3. send a command string
4. print the response

For example:

```bash
./lmctl status
```

sends a command like:

```text
status
```

to the supervisor and prints the returned status block.

---

### 8.1 Watch mode
`lmctl` also supports a watch mode.

Example:

```bash
./lmctl watch
```

This periodically requests:

- `status`
- `events N`

and prints a refreshed terminal view.

The watch mode also adds:

- color
- summary counts
- filtering for warnings/errors

This part of the code is useful if you want to learn how to build a lightweight local monitoring CLI.

---

## 9. Example child processes

The `examples/` directory is very important for learning.

---

### 9.1 `dummy_process`
This is the simplest child process example.

It demonstrates the minimal child contract:

- expose a control socket
- respond to `PING`
- respond to `GET_STATUS`
- respond to `SET_MODE`
- support fault injection for testing

This is the best place to start if you want to understand the child side first.

---

### 9.2 `planner_process`
This is a more advanced child example.

It demonstrates:

- internal threads
- watchdog logic
- JSON-based status reporting
- process-level health derived from thread states

This is closer to what a real module would do.

It shows that a child process can internally monitor multiple worker threads and expose structured health information to the supervisor.

---

## 10. `thread_manager` and internal health tracking

The `thread_manager` component is used inside `planner_process`.

It is not the same as the outer supervisor.

Instead, it is an internal mechanism used by a child process to track the health of its own threads.

---

### 10.1 Why it exists
A process may still be alive even if one of its internal threads is unhealthy or deadlocked.

For example:

- the process exists
- its control socket still responds
- but its planning thread is stalled

This means process-level health needs more than just "process exists".

---

### 10.2 What it does
The thread manager typically supports:

- thread registration
- heartbeat recording
- progress tracking
- error recording
- timeout detection
- watchdog-based health assessment
- JSON status generation

This makes `planner_process` a better simulation of a real robot module.

---

## 11. JSON child status support

This repository supports both old-style simple status text and newer JSON child status.

That transition is handled by the child JSON parsing code.

---

### 11.1 Why this exists
Originally, child status may have been simple key-value style text.

Now more advanced child processes, such as `planner_process`, can return structured JSON.

The supervisor therefore supports both formats.

---

### 11.2 `parse_child_status_json`
This function parses child JSON into a structured intermediate form.

Typical fields include:

- `module`
- `runtime_state`
- `health`
- `process_failed`
- `process_degraded`
- `mode`
- `detail`

---

### 11.3 Fallback behavior
The supervisor tries JSON parsing first.

If parsing fails, it falls back to older key-value parsing.

This allows gradual migration of child modules from text status to JSON status.

---

## 12. Monitoring child exits

The monitor thread runs logic similar to:

- enumerate known processes
- check runtime pids
- call `waitpid(..., WNOHANG)`
- detect exits
- update runtime state
- decide whether to restart
- decide whether to enter SAFE

This is the main unexpected-exit handling path.

---

### 12.1 Non-stopping exit
If a child exits unexpectedly, the supervisor records:

- exit code
- event log entry
- runtime state changes

If the process is critical, the supervisor may enter SAFE.

If the process is restartable and non-critical, it may be restarted based on policy.

---

## 13. Status polling

The polling thread periodically calls `refresh_child_status_()` for each child.

This updates:

- child reachability
- child mode
- child health
- supervisor-visible runtime state

This is how the supervisor maintains a continuously updated system view.

---

## 14. Status and events output

The system provides several text views for operators.

---

### 14.1 `status`
Global summary view.

Typically includes:

- manager mode
- manager state
- process count
- one summary line per process

This is the main high-level overview.

---

### 14.2 `proc_status <name>`
Detailed single-process view.

Useful for debugging one module in more detail.

---

### 14.3 `events`
Recent event log view.

Examples of events:

- process started
- process stopped
- unexpected exit
- restart attempted
- SAFE entered
- polling failure

This is one of the most useful debugging outputs.

---

## 15. How configuration drives the system

`supervisor_config.json` defines the full topology and behavior of the supervised system.

It specifies:

- supervisor control socket
- all child processes
- argv for each child
- dependencies
- control sockets
- roles
- critical flags
- restart behavior
- timeout policies

This is the assembly description of the system.

---

### 15.1 Why configuration-driven design matters
Because of this design, adding a new module often does not require changing supervisor core logic.

If you add a new process like `map_manager`, you mainly update:

- `supervisor_config.json`
- documentation

and only need new process code if the process itself is new.

This makes the system extensible.

---

## 16. How to study this repository effectively

A good learning order is:

### Step 1: Read `README.md`
Understand project purpose, build/run flow, and intended module topology.

### Step 2: Read `docs/architecture.md`
Understand design rationale and dependency layering.

### Step 3: Read `supervisor_config.json`
Understand how the runtime graph is declared.

### Step 4: Read `examples/dummy_process.cpp`
Understand the simplest child process contract.

### Step 5: Read `tools/lmctl.cpp`
Understand how operator commands reach the supervisor.

### Step 6: Read `src/ipc_client.cpp` and `src/control_server.cpp`
Understand both ends of the local IPC control path.

### Step 7: Read `src/supervisor.cpp`
This is the core logic and should be studied carefully.

### Step 8: Read `examples/planner_process.cpp` and `examples/thread_manager.cpp`
Understand how a more realistic child process reports structured health.

---

## 17. Strengths of the design

This repository has several strong design characteristics.

### 17.1 Clear separation of roles
- supervisor manages lifecycle
- child processes manage their own logic
- CLI handles operator interaction

### 17.2 Configuration-driven topology
The system graph is not hardcoded in one monolithic place.

### 17.3 Lightweight implementation
No heavy middleware is required for local control.

### 17.4 Easy to debug
Socket paths, text commands, and JSON status are all easy to inspect.

### 17.5 Gradual evolution path
The project already shows migration from simple key-value child status toward structured JSON status.

---

## 18. Limitations of the current design

This project also has some clear scope boundaries.

### 18.1 It is a control-plane framework
It is well-suited for:

- start/stop
- health
- mode control
- supervision

It is not intended to carry high-frequency business data such as:

- lidar streams
- pose streams
- point clouds
- trajectories

That requires a separate data bus design.

---

### 18.2 Primarily single-machine oriented
The current implementation is clearly optimized for local supervision on one machine.

### 18.3 Protocol simplicity
The control protocol is intentionally lightweight.
More advanced systems might eventually want a more formal JSON-RPC style command interface.

---

## 19. One-sentence summary

This repository implements:

> a configuration-driven local robot process supervisor built around Unix Domain Socket IPC, dependency-aware startup, health polling, and SAFE-oriented failure handling.

---

## 20. Suggested next step

If you want to continue studying this repository, the best next deep-dive is:

- `src/supervisor.cpp` function-by-function walkthrough

That file contains the core lifecycle logic and ties the whole project together.
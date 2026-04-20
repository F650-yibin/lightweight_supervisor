# supervisor.cpp Walkthrough

This document explains `src/supervisor.cpp` function by function.

The goal is to understand:

- how the supervisor manages process lifecycle
- how dependencies are enforced
- how child health is polled
- how failures are handled
- how commands from `lmctl` are dispatched

This file is the core of the repository.

---

## 1. Role of `supervisor.cpp`

`src/supervisor.cpp` contains the implementation of the `Supervisor` class.

This class is responsible for:

- storing configured processes
- storing runtime state
- starting and stopping child processes
- polling child status
- monitoring child exits
- forwarding commands to children
- switching modes
- generating status and event text
- exposing the control-plane behavior of the whole system

If the repository has a "brain", this file is it.

---

## 2. Internal helper utilities

At the top of the file there is an anonymous namespace with helper utilities.

These are file-local helpers and are not part of the public interface.

### 2.1 `Error`
A small custom exception type derived from `std::runtime_error`.

Purpose:
- throw meaningful internal supervisor errors
- distinguish internal supervisor logic failures cleanly

---

### 2.2 `now_string()`
Builds a formatted local time string.

Used when recording events.

Purpose:
- create readable timestamps for the event log

---

### 2.3 `split_ws(const std::string&)`
Splits a command line into whitespace-separated tokens.

Used mainly when handling commands received from the control socket.

Purpose:
- parse commands such as:
  - `start`
  - `mode AUTO`
  - `proc_status planner`

Important note:
- this is a simple whitespace split, not a shell parser

---

## 3. Constructor and destructor

---

### 3.1 `Supervisor::Supervisor() = default;`
The constructor itself is simple.

Most initialization is done by default member initialization in the class definition.

---

### 3.2 `Supervisor::~Supervisor()`
The destructor tries to shut the system down safely.

It performs several cleanup phases, each wrapped in `try/catch` so that one failure does not block later cleanup:

1. `enter_safe_mode()`
2. `stop_all()`
3. `stop_background_threads()`
4. `stop_control_interface()`

Purpose:
- make destruction robust
- reduce risk of leaving child processes running
- stop the local control server cleanly

Design observation:
- this is a defensive systems-programming style destructor

---

## 4. Process registration

---

### 4.1 `add_process(ProcessSpec spec)`

This function registers one process with the supervisor.

It validates:

- process name is not empty
- argv is not empty
- control socket is not empty
- process name is not duplicated

Then it inserts:

- a new `ProcessRuntime` into `runtime_`
- the `ProcessSpec` into `specs_`

Purpose:
- build the in-memory process graph from configuration

Important detail:
- registration happens before runtime actions such as start/stop
- this creates both static config and runtime state entries

---

## 5. Supervisor control interface

---

### 5.1 `start_control_interface(const std::string &sock_path)`

This function starts the supervisor's own control socket server.

It first creates `/tmp/robot` if needed, then constructs a `ControlServer` using a callback:

- incoming command line text is passed to `handle_command_()`

Then it starts the server and logs an event.

Purpose:
- expose the main command interface for `lmctl`

This is the entry point for commands like:

- `start`
- `stop`
- `status`
- `events`
- `mode AUTO`

---

### 5.2 `stop_control_interface()`

Stops and destroys the control server if it exists.

Purpose:
- stop the supervisor's external control endpoint during shutdown

---

## 6. Background threads

The supervisor runs two important background loops.

---

### 6.1 `run_background_threads()`

This function starts background monitoring only once.

It protects internal state with a mutex, checks `bg_running_`, sets it to true, and spawns:

- `monitor_thread_`
- `poll_thread_`

Purpose:
- enable continuous runtime supervision after initialization

---

### 6.2 `stop_background_threads()`

This function stops the background loops.

It does:

1. lock and set `bg_running_ = false`
2. notify condition variables
3. join the monitor thread
4. join the poll thread

Purpose:
- shut background activity down cleanly
- avoid leaving running threads during destruction

---

## 7. Starting and stopping the whole system

---

### 7.1 `start_all()`

This starts all configured processes in dependency order.

### What it does
1. lock state
2. if already started, return
3. set `manager_state_ = "STARTING"`
4. compute topological order using `topo_order_()`
5. unlock and iterate through the order
6. call `start_one_()` for each process
7. remember successful start order
8. mark system started
9. set `manager_state_ = "RUNNING"`
10. call `set_mode_all("SAFE")`
11. log event

### Why this is important
This is the main startup orchestration function.

It ensures:

- dependencies are respected
- system state moves to running only after successful startup
- initial mode is safe

### Error handling
If startup fails in the middle:

- already-started processes are stopped in reverse order
- manager state becomes `ERROR`
- exception is rethrown

This is strong cleanup behavior.

---

### 7.2 `stop_all()`

Stops all started processes.

### What it does
1. lock state
2. if not started, return
3. set `manager_state_ = "STOPPING"`
4. capture `start_order_`
5. call `enter_safe_mode()`
6. reverse the start order
7. stop each process with `stop_one_()`
8. after all stop attempts, mark:
   - `started_ = false`
   - `manager_mode_ = "STOPPED"`
   - `manager_state_ = "STOPPED"`
9. log event

### Why reverse order
Processes are stopped in reverse dependency/start order so that upper-layer processes go away before their dependencies.

---

## 8. Per-process start/stop/restart entry points

---

### 8.1 `start_proc(const std::string &name)`
Simple wrapper around `start_one_()`.

Purpose:
- allow operator to start one named process

---

### 8.2 `stop_proc(const std::string &name)`
Simple wrapper around `stop_one_()`.

Purpose:
- allow operator to stop one named process

---

### 8.3 `restart_proc(const std::string &name)`
Simple wrapper around `restart_one_()`.

Purpose:
- allow operator to restart one named process

---

## 9. Mode switching

---

### 9.1 `set_mode_all(const std::string &mode)`

This validates the mode and applies it across all processes using `apply_mode_ordered_()`.

Then it updates `manager_mode_` and logs an event.

Purpose:
- provide a global mode switch across the supervised system

---

### 9.2 `enter_safe_mode()`

This is a defensive helper.

It attempts to apply mode `SAFE` to all processes, updates `manager_mode_`, and logs a warning event.

Even if mode application throws, the function suppresses the exception and still logs.

Purpose:
- force best-effort transition into a safe system-wide mode

This is heavily used in error handling paths.

---

## 10. Text output methods

These functions generate text returned through the control socket.

---

### 10.1 `status_text()`

Builds the main global status string.

### Output structure
- first line:
  - manager mode
  - manager state
  - process count
- then one line per process including:
  - name
  - pid
  - state
  - reachable
  - child_mode
  - health
  - role
  - restarts
  - critical
  - detail

Finally it ends with a line terminator marker `.`.

Purpose:
- provide a machine-readable but human-readable global status view

Important observation:
- this is the output used by `lmctl status`
- `lmctl watch` also depends on this output shape

---

### 10.2 `proc_status_text(const std::string &name)`

Builds a single-process detailed status string.

This includes:

- name
- pid
- state
- reachable
- child_mode
- health
- detail
- role
- critical
- restart_policy
- restarts

Purpose:
- show more detail for one process than the global summary

Used by:
- `lmctl proc_status <name>`

---

### 10.3 `events_text(size_t max_count)`

Returns recent event log entries.

### Output structure
- first line says how many events are included
- then each event line includes:
  - timestamp
  - level
  - process
  - message

Ends with `.`.

Purpose:
- provide operator-visible event history

Used by:
- `lmctl events`
- `lmctl watch`

---

## 11. Command dispatcher

---

### 11.1 `handle_command_(const std::string &cmdline)`

This is one of the most important functions.

It parses a command line from the control socket and dispatches behavior.

### Supported commands include
- `ping`
- `start`
- `stop`
- `start_proc <proc>`
- `stop_proc <proc>`
- `status`
- `events [n]`
- `mode <SAFE|IDLE|TELEOP|AUTO|DIAG>`
- `restart <proc>`
- `proc_status <proc>`
- `child <proc> <raw_command...>`

### Dispatch behavior
- checks argument count
- calls the correct internal function
- returns text response
- catches exceptions and converts them into `ERR ...`

Purpose:
- central command-plane dispatch for all operator control

Very important:
- this is where `lmctl` commands are translated into supervisor operations

---

## 12. Starting one process

---

### 12.1 `start_one_(const std::string &name)`

This is the main process-launch function.

### Step-by-step behavior

#### Step 1: validate and snapshot spec
- check process exists
- get config
- if already `Running` or `Starting`, return

#### Step 2: verify dependencies
For each dependency in `spec.deps`, call `ping_or_throw_(dep)`.

This means:
- dependency must exist
- dependency pid must be valid
- dependency must respond to `PING`

This is stricter than just "was started once".

#### Step 3: initialize runtime state
Set:
- state = `Starting`
- child_status.reachable = false
- mode = `UNKNOWN`
- health = `UNKNOWN`
- detail cleared

#### Step 4: `fork()`
Create child process.

If fork fails:
- mark runtime state `Failed`
- notify waiters
- throw

#### Step 5: child branch
In the child:
- call `setpgid(0, 0)`
- build `argv`
- call `execvp`
- if exec fails, `_exit(127)`

Purpose:
- launch the configured executable in its own process group

#### Step 6: parent branch
Store:
- `pid`
- `pgid`
- `state = Starting`

Then call `setpgid(pid, pid)`.

#### Step 7: wait for readiness
Call `wait_ready_or_throw_(name)`.

This waits until the child becomes reachable.

#### Step 8: finalize success state
If still `Starting`, switch to `Running`.

Also:
- ensure it appears in `start_order_`
- mark `started_ = true`
- if manager state was `INIT` or `STOPPED`, move to `RUNNING`

Then notify and log event.

### Key design point
A process is not considered successfully started just because `fork/exec` worked.

It is considered started only after the child becomes reachable through its control socket.

This is a very important design choice.

---

## 13. Stopping one process

---

### 13.1 `stop_one_(const std::string &name)`

This function stops a process gracefully, then forcefully if needed.

### Step-by-step behavior

#### Step 1: validate and snapshot state
- ensure process exists
- load spec
- get runtime

If pid is invalid:
- mark as `Stopped`
- mark child unreachable
- notify and return

#### Step 2: move state to `Stopping`
Store process group id and change runtime state.

#### Step 3: send graceful signal
Call:

- `kill(-pgid, spec.graceful_signal)`

The negative PGID means:
- signal the whole process group

This is important because a child process may itself create subprocesses.

#### Step 4: wait for stop
Wait on a condition variable until:
- state becomes `Stopped` or `Exited`

If this happens within timeout, return success.

#### Step 5: force kill if configured
If graceful stop timed out and `kill_on_timeout` is true:
- send `SIGKILL` to process group
- wait again briefly

If that succeeds, return.

#### Step 6: otherwise fail
Throw `Error("stop timeout: " + name)`.

### Key design point
The stop flow is process-group aware, not just single-PID aware.

That is good systems design.

---

## 14. Restarting one process

---

### 14.1 `restart_one_(const std::string &name)`

Very straightforward:

1. log a warning event
2. stop the process
3. start the process again

Purpose:
- operator-triggered restart path

---

## 15. Monitor thread loop

---

### 15.1 `monitor_children_loop_()`

This thread watches for child exits.

### High-level behavior
Repeatedly:
1. check whether background running is still enabled
2. snapshot all process names
3. for each process:
   - snapshot spec/runtime
   - skip if pid invalid
   - call `waitpid(pid, &status, WNOHANG)`
   - if child exited:
     - determine exit code
     - update runtime
     - decide restart or failure handling
4. sleep briefly

### Exit handling logic
If the process was in `Stopping`:
- transition to `Stopped`
- log "stopped"

Otherwise:
- transition to `Exited`
- log unexpected exit

### Critical process behavior
If a critical process exits unexpectedly:
- log error
- enter SAFE mode
- set `manager_state_ = "ERROR"`

### Non-critical restartable process behavior
If restart policy allows restart:
- log restart warning
- sleep for backoff
- increment restart count
- attempt restart

### Why this matters
This is the core failure-recovery loop.

It is what turns the project from "simple launcher" into an actual supervisor.

---

## 16. Poll thread loop

---

### 16.1 `poll_status_loop_()`

This thread periodically refreshes child status.

### High-level behavior
Repeatedly:
1. check whether background running is still enabled
2. snapshot process names
3. for each process, call `refresh_child_status_(name)`
4. log warnings if polling fails
5. sleep for a fixed interval

Purpose:
- keep supervisor-side status current
- detect degraded or unreachable processes

---

## 17. Child status refresh

---

### 17.1 `refresh_child_status_(const std::string &name)`

This function is central to health supervision.

### Step-by-step behavior

#### Step 1: snapshot spec/runtime
Make local copies under lock.

#### Step 2: handle non-running case
If `pid <= 0`:
- reset child status
- mark unreachable
- return

#### Step 3: send `PING`
Use `IpcClient::request(...)`.

If response does not start with `OK`, throw an error.

#### Step 4: send `GET_STATUS`
Request detailed status from child.

#### Step 5: parse response
There are two parsing paths:

- JSON path via `parse_child_status_json`
- fallback key-value path via `parse_kv_`

If JSON parsing works:
- fill mode/health/detail from structured status
- derive summary health:
  - `FAIL` if process failed or runtime state is error
  - `ERR` if degraded
  - `OK` if nothing bad is reported

If JSON parsing fails:
- fallback to older key-value parsing

#### Step 6: update supervisor cache
Store parsed child status into runtime.

Then update runtime state:
- if `Starting` and reachable -> `Running`
- if health is `ERR` or `FAIL` while `Running` -> `Degraded`
- if health returns normal while `Degraded` -> `Running`

#### Step 7: on failure
If any exception occurs:
- mark child unreachable
- if runtime state was `Running`, set it to `Degraded`
- notify and rethrow

### Why this function is important
This function combines:
- communication
- protocol parsing
- state derivation
- health interpretation

It is the main bridge between child-reported health and supervisor state.

---

## 18. Dependency checking

---

### 18.1 `ping_or_throw_(const std::string &name)`

Checks whether a dependency is actually up.

It verifies:

- process exists
- pid is valid
- child responds to `PING`

If any of these checks fails, startup of the dependent process is blocked.

Purpose:
- prevent starting a dependent process when its prerequisites are not truly ready

---

## 19. Readiness waiting

---

### 19.1 `wait_ready_or_throw_(const std::string &name)`

This function waits for a just-started process to become reachable.

### How it works
1. read process startup timeout
2. loop until deadline
3. repeatedly call `refresh_child_status_(name)`
4. if child becomes reachable, return success
5. if process exits or fails during startup, throw
6. sleep briefly between checks

If timeout expires:
- mark state `Failed`
- notify waiters
- throw startup timeout error

### Why this is important
This enforces a clear readiness model:

- launched is not enough
- reachable over control socket means ready

That is a much stronger and better model than "fork succeeded".

---

## 20. Event logging

---

### 20.1 `log_event_(const std::string &level, const std::string &proc, const std::string &message)`

Pushes one event into the in-memory event buffer.

Also trims the event list to a maximum size.

Purpose:
- maintain a recent event history for operator inspection

This powers:
- `events`
- `watch`

---

## 21. Topological ordering

---

### 21.1 `topo_order_() const`

This computes dependency order among configured processes.

### How it works
1. build indegree map
2. build outgoing adjacency map
3. enqueue all zero-indegree nodes
4. run Kahn-style topological sort
5. if result size does not match process count, detect a dependency cycle

Purpose:
- compute safe process startup order
- validate dependency graph consistency

This is a key architecture function because startup correctness depends on it.

---

## 22. Applying modes in safe order

---

### 22.1 `apply_mode_ordered_(const std::string &mode)`

This function applies a mode to processes in role-based order.

### Step-by-step behavior
1. validate the mode
2. ensure processes are started unless mode is `SAFE`
3. compute topo order
4. choose role order based on target mode
5. for each role in role order:
   - iterate processes in topo order
   - if process role matches and process is running:
     - send `SET_MODE <mode>` via IPC
     - verify response starts with `OK`

### Role order
For `SAFE` or `IDLE`:
- Actuator
- Compute
- Sensor

For work modes:
- Sensor
- Compute
- Actuator

### Why this matters
This is the repository's safety-oriented mode transition strategy.

---

## 23. Mode validation

---

### 23.1 `validate_mode_(const std::string &mode) const`

Allows only:

- `SAFE`
- `IDLE`
- `TELEOP`
- `AUTO`
- `DIAG`

Purpose:
- ensure control commands do not push arbitrary invalid mode strings into child processes

---

## 24. Forwarding raw child commands

---

### 24.1 `forward_child_command_(const std::string &name, const std::string &raw_command)`

This lets the operator send raw commands to one child through the supervisor.

### Flow
1. validate child exists
2. validate child has a running pid
3. send raw command through `IpcClient`
4. log an event
5. return the child's response

Used by:
- `lmctl child <proc> <raw_command...>`

Examples:
- `child planner GET_STATUS`
- `child planner SET_HEALTH ERR`
- `child planner CRASH`

Purpose:
- provide a debugging and testing backdoor through supervisor

---

## 25. Legacy key-value parsing

---

### 25.1 `parse_kv_(const std::string &line)`

Parses a simple whitespace-delimited key-value response such as:

```text
OK mode=AUTO health=OK detail=ready
```

It:
- splits by whitespace
- skips leading `OK` or `ERR`
- parses `key=value` tokens

Purpose:
- support older/simple child status formats

Important limitation:
- values containing spaces are not well represented in this legacy format

That is one reason JSON status is a stronger long-term direction.

---

## 26. How the major pieces connect

The overall runtime interaction looks like this:

### Operator path
`lmctl` -> supervisor control socket -> `handle_command_()` -> supervisor actions

### Child monitoring path
poll thread -> `refresh_child_status_()` -> `IpcClient` -> child control socket

### Exit monitoring path
monitor thread -> `waitpid(..., WNOHANG)` -> exit handling / restart / SAFE

### Mode path
operator command -> `set_mode_all()` -> `apply_mode_ordered_()` -> `SET_MODE` to child processes

---

## 27. Most important design ideas in this file

### 27.1 Readiness is IPC reachability
A child is not truly considered started until it responds over its control socket.

### 27.2 Dependencies are active, not passive
Dependencies are checked using live `PING`, not just static config order.

### 27.3 Safety is built into mode order
Actuators are enabled last and disabled first.

### 27.4 Critical failures trigger SAFE
Critical process exits are treated as system-level safety events.

### 27.5 Polling and reaping are separate concerns
One thread handles exit detection.
Another handles health/status polling.

This separation keeps responsibilities cleaner.

---

## 28. What to study next after this file

After understanding `supervisor.cpp`, the best next files are:

1. `src/ipc_client.cpp`
   - how supervisor talks to children

2. `src/control_server.cpp`
   - how supervisor accepts external commands

3. `examples/dummy_process.cpp`
   - simplest child-side protocol implementation

4. `examples/planner_process.cpp`
   - more realistic child behavior and JSON status

If `supervisor.cpp` is the brain, those files are the nervous system and end devices.

---

## 29. One-sentence summary

`src/supervisor.cpp` implements a configuration-driven process supervisor that launches children in dependency order, tracks their health via local IPC, reacts to failures, and exposes a control interface for operating the whole system.
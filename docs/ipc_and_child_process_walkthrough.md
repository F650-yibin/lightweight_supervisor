# IPC and Child Process Walkthrough

This document explains how local IPC works in `lightweight_supervisor` and how child processes are expected to behave.

It focuses on these parts of the repository:

- `src/ipc_client.cpp`
- `src/control_server.cpp`
- `examples/dummy_process.cpp`
- `examples/planner_process.cpp`
- `examples/thread_manager.cpp`
- `src/child_status_json.cpp`

The goal is to understand:

- how `lmctl` talks to `supervisord`
- how `supervisord` talks to child processes
- what contract a child process must implement
- how simple and advanced child status reporting works

---

## 1. IPC in this repository

The repository uses **Unix Domain Socket IPC**.

This means processes communicate through local socket paths such as:

- `/tmp/robot/lifecycle_manager.sock`
- `/tmp/robot/planner.sock`
- `/tmp/robot/base_driver.sock`

This is local-machine IPC only.  
It is not TCP networking and not shared memory.

The communication style is mostly:

- connect
- send one command
- read one response
- close

So the model is a lightweight request-response protocol.

---

## 2. Two IPC paths in the system

There are two major IPC paths.

### 2.1 `lmctl` -> `supervisord`
This is the operator control path.

Example commands:

- `start`
- `stop`
- `status`
- `events`
- `mode AUTO`

This uses the supervisor control socket, typically:

- `/tmp/robot/lifecycle_manager.sock`

---

### 2.2 `supervisord` -> child process
This is the supervisor-to-module control path.

Example commands:

- `PING`
- `GET_STATUS`
- `SET_MODE SAFE`
- `SET_MODE AUTO`

This uses each child's control socket, for example:

- `/tmp/robot/planner.sock`
- `/tmp/robot/localization.sock`

---

## 3. Why Unix Domain Socket is used

This repository chooses Unix Domain Socket because it is a good fit for:

- local process communication
- simple request-response control
- low configuration overhead
- no need for network ports
- easy debugging

It is especially suitable for control-plane communication such as:

- lifecycle commands
- status queries
- mode switching
- test fault injection

It is not intended for high-frequency sensor streams like lidar data.

---

## 4. `src/ipc_client.cpp`

This file implements the client-side helper used by the supervisor to talk to child processes.

You can think of it as a very small local RPC client.

---

### 4.1 Main responsibility
`IpcClient` is responsible for:

- opening a Unix socket
- connecting to a given socket path
- writing a command
- reading the response
- handling timeout behavior

This is used heavily by:

- `refresh_child_status_()`
- `apply_mode_ordered_()`
- `forward_child_command_()`
- `ping_or_throw_()`

inside `Supervisor`.

---

### 4.2 Typical usage
The supervisor does things like:

- `IpcClient::request(sock, "PING", timeout)`
- `IpcClient::request(sock, "GET_STATUS", timeout)`
- `IpcClient::request(sock, "SET_MODE AUTO", timeout)`

So `IpcClient` is the generic transport helper.

---

### 4.3 Conceptual flow
The flow of one `request()` call is roughly:

1. create socket with `AF_UNIX`
2. connect to socket path
3. send request text
4. wait for response until timeout
5. return response string

This is intentionally lightweight.

---

### 4.4 What this means architecturally
The supervisor does not embed protocol logic into socket code directly.

Instead:

- `IpcClient` handles transport
- `Supervisor` handles semantics

That separation is good design.

---

## 5. `src/control_server.cpp`

This file implements the supervisor-side socket server.

It is used for the control interface exposed to `lmctl`.

---

### 5.1 Main responsibility
The `ControlServer` is responsible for:

- creating and binding a Unix socket
- listening for incoming connections
- accepting client connections
- reading command text
- calling a handler callback
- sending back the response

It is a small local command server.

---

### 5.2 How it is used
The supervisor creates a `ControlServer` with a callback that points to:

- `Supervisor::handle_command_()`

That means the transport server itself does not know business logic.

It only knows:

- receive request text
- pass request to callback
- send reply back

This keeps transport and control logic separate.

---

### 5.3 Why this separation matters
This is a useful learning pattern:

- `ControlServer` = generic socket serving machinery
- `Supervisor` = domain-specific command dispatcher

This makes the code easier to evolve and test.

---

## 6. The child process contract

This repository assumes that a child process exposes a local control socket and supports a simple command protocol.

At minimum, a child should be able to respond to:

- `PING`
- `GET_STATUS`
- `SET_MODE <mode>`

That is the implicit contract between `supervisord` and a managed child.

---

### 6.1 `PING`
Purpose:
- liveness check
- basic IPC reachability check

Expected response:
- something starting with `OK`

If `PING` fails, the supervisor treats the child as unreachable.

---

### 6.2 `GET_STATUS`
Purpose:
- retrieve mode
- retrieve health
- retrieve detail
- optionally retrieve structured JSON health data

Expected response:
- either simple key-value text
- or JSON

This is how the supervisor gets health information beyond "the process exists".

---

### 6.3 `SET_MODE <mode>`
Purpose:
- request child to switch operating mode

Examples:
- `SET_MODE SAFE`
- `SET_MODE AUTO`
- `SET_MODE DIAG`

Expected response:
- something starting with `OK`

This is how the supervisor coordinates safe and ordered mode transitions.

---

## 7. `examples/dummy_process.cpp`

This is the simplest child process implementation in the repository.

It is meant for testing and learning.

If you want to understand the child side first, start here.

---

### 7.1 What it demonstrates
`dummy_process` shows how to build a minimal supervised process that:

- opens a control socket
- listens for requests
- answers commands
- tracks a simple internal mode/health/detail state
- supports test fault injection

It is essentially a reference implementation of the child control contract.

---

### 7.2 What state it usually maintains
A dummy process typically keeps a small amount of internal state, such as:

- current mode
- current health
- current detail string
- optional delay/fault flags

That state is returned through `GET_STATUS`.

---

### 7.3 Commands it typically supports
Besides the required control commands, `dummy_process` often supports extra test commands such as:

- `SET_HEALTH <OK|ERR|FAIL>`
- `SET_DETAIL <text>`
- `SET_DELAY_MS <n>`
- `CRASH`
- `EXIT <code>`
- `HANG`
- `RECOVER`

These make it easy to test supervisor behavior.

---

### 7.4 Why this file is important
This file teaches the minimum interface a managed process must implement.

It also makes it easy to test:

- startup waiting
- health degradation
- unexpected exit handling
- restart policy
- SAFE behavior
- watch mode output

---

## 8. Typical `dummy_process` control flow

A typical child process structure looks like this:

1. parse command-line arguments
2. determine socket path
3. create/remove socket file
4. bind and listen
5. accept incoming requests
6. parse command text
7. update internal state or return status
8. send response

This is effectively a tiny local command server.

---

## 9. Status format used by simple child processes

Simple child processes may return status text like:

```text
OK mode=SAFE health=OK detail=ready
```

This is the older key-value style status format.

The supervisor parses this using legacy key-value parsing logic.

---

### 9.1 Strengths of this simple format
- easy to implement
- easy to print
- easy to debug manually

### 9.2 Limitations
- awkward when values contain spaces
- limited structure
- harder to express nested health data
- less future-proof than JSON

That is why the repository also supports JSON child status.

---

## 10. `examples/planner_process.cpp`

This is a more advanced child example.

It is closer to how a real module would behave.

---

### 10.1 Why it exists
A real robot module is often more complex than a one-loop test process.

It may have:

- multiple threads
- internal worker loops
- internal failure/degradation conditions
- watchdog logic
- structured status output

`planner_process` demonstrates that kind of process.

---

### 10.2 What it shows
This file demonstrates:

- mode-aware child behavior
- internal worker thread management
- structured health state
- JSON status reporting
- a more realistic relationship between internal health and process-level health

It is a very useful step after understanding `dummy_process`.

---

## 11. `examples/thread_manager.cpp`

This file supports `planner_process` by managing thread-level health.

It is essentially a small internal watchdog and health aggregator.

---

### 11.1 Why thread-level health matters
A process may still be alive while one of its important threads is unhealthy.

Examples:

- planning thread is blocked
- status thread is late
- worker thread made no progress
- watchdog detects timeout

If the process only reported "I am alive", that would not be enough.

So `thread_manager` gives the child a richer internal health model.

---

### 11.2 Likely responsibilities
A thread manager usually supports:

- registering threads
- updating heartbeat timestamps
- updating progress timestamps
- storing thread states
- storing error messages
- detecting timeouts
- building a process-level summary

This is a typical design pattern in robotics and systems software.

---

### 11.3 How it connects to `planner_process`
`planner_process` uses thread manager state to build its `GET_STATUS` result.

So:

- thread manager tracks internal health
- planner process exposes that health through IPC
- supervisor consumes the summary and reacts

That creates a clean multi-layer health model:

1. thread-level health inside child
2. process-level health exposed by child
3. supervisor-level health interpretation across modules

---

## 12. JSON status reporting

This repository supports JSON child status for more advanced children.

That logic is tied to:

- `planner_process`
- `thread_manager`
- `src/child_status_json.cpp`

---

### 12.1 Why JSON is better here
JSON makes it easier to express:

- module name
- runtime state
- mode
- health
- detail
- degraded/failure flags
- thread-level status arrays

This is much more expressive than simple `key=value` text.

---

### 12.2 Example conceptual JSON
A child may conceptually return something like:

```json
{
  "module": "planner",
  "runtime_state": "Running",
  "mode": "AUTO",
  "health": "OK",
  "detail": "planner healthy",
  "process_failed": false,
  "process_degraded": false,
  "threads": [
    {
      "name": "planning",
      "state": "Running",
      "healthy": true
    }
  ]
}
```

The exact format is defined by the implementation, but structurally it is like this.

---

## 13. `src/child_status_json.cpp`

This file parses JSON returned by advanced child processes.

It allows the supervisor to understand structured child health.

---

### 13.1 Main responsibility
The parser converts raw JSON text into a structured summary object.

Typical extracted fields include:

- module
- runtime_state
- health
- process_failed
- process_degraded
- mode
- detail

The supervisor then uses those fields to derive its own high-level state.

---

### 13.2 Why parsing is separate
This is another example of good separation of concerns:

- child generates JSON
- parser extracts structured fields
- supervisor applies policy

This avoids mixing raw JSON parsing logic into the core supervisor control flow.

---

## 14. How supervisor interprets child JSON status

When the supervisor calls `GET_STATUS`, it receives a string.

Then it follows this pattern:

1. try JSON parsing first
2. if JSON parsing succeeds:
   - extract mode
   - extract health
   - inspect degraded/failure flags
   - derive `OK`, `ERR`, or `FAIL`
3. if JSON parsing fails:
   - fall back to legacy key-value parsing

This means the repository supports a gradual migration path:

- simple children can stay simple
- advanced children can report structured status

That is a practical design.

---

## 15. End-to-end IPC flow examples

This section ties the pieces together.

---

### 15.1 `lmctl status`
Flow:

1. operator runs `./lmctl status`
2. `lmctl` connects to supervisor control socket
3. `lmctl` sends `status`
4. `ControlServer` receives request
5. callback enters `Supervisor::handle_command_()`
6. `status_text()` builds the response
7. response is returned to `lmctl`
8. `lmctl` prints it

This path does not contact child processes directly at request time.
It uses supervisor-cached state built by the polling thread.

---

### 15.2 Supervisor polling `planner`
Flow:

1. poll thread chooses process `planner`
2. calls `refresh_child_status_("planner")`
3. `IpcClient` connects to `/tmp/robot/planner.sock`
4. sends `PING`
5. reads response
6. sends `GET_STATUS`
7. reads JSON response
8. `child_status_json.cpp` parses it
9. supervisor updates cached runtime state

This is the main health update path.

---

### 15.3 `lmctl child planner GET_STATUS`
Flow:

1. operator runs `./lmctl child planner GET_STATUS`
2. `lmctl` sends `child planner GET_STATUS` to supervisor
3. `handle_command_()` parses command
4. supervisor calls `forward_child_command_("planner", "GET_STATUS")`
5. `IpcClient` talks directly to planner control socket
6. planner returns raw status response
7. supervisor returns that raw response to `lmctl`
8. operator sees direct child output

This is useful for debugging the child protocol itself.

---

## 16. Why this design is useful for learning

This repository is a good teaching example because it cleanly demonstrates several layers:

### 16.1 Transport layer
Unix Domain Socket request-response

### 16.2 Control layer
Supervisor command dispatch

### 16.3 Child contract layer
`PING`, `GET_STATUS`, `SET_MODE`

### 16.4 Health model layer
Simple key-value status and structured JSON status

### 16.5 Internal health layer
Thread-level health inside `planner_process`

This layered design is realistic and educational.

---

## 17. What this repository does not do in IPC

It is important to understand what the IPC here is **not** intended for.

It is not for:

- high-frequency lidar streaming
- point cloud transport
- camera image transport
- trajectory streaming
- large shared-memory data paths

Those are data-plane problems.

This repository implements control-plane IPC.

That is why Unix Domain Socket request-response works well here.

---

## 18. Strengths of the child IPC design

### 18.1 Simple
The minimum contract is small and easy to implement.

### 18.2 Easy to debug
You can inspect socket paths and reason about text commands directly.

### 18.3 Extensible
Simple children can use text status.
Advanced children can use JSON status.

### 18.4 Compatible with safety supervision
The supervisor can use the same contract for:
- readiness checks
- health checks
- mode transitions

---

## 19. Limitations of the current child IPC design

### 19.1 Protocol is lightweight, not formal RPC
There is no full schema-validated RPC framework here.

### 19.2 Simple text parsing has limitations
Legacy key-value text is fragile for complex structured data.

### 19.3 One-request-one-response model is basic
This is not a streaming design.

That is acceptable because this IPC is for control, not for data transport.

---

## 20. Best way to study these files

Recommended study order:

1. `examples/dummy_process.cpp`
   - understand minimal child behavior

2. `src/ipc_client.cpp`
   - understand how supervisor sends commands

3. `src/control_server.cpp`
   - understand how supervisor receives commands

4. `examples/thread_manager.cpp`
   - understand internal child health tracking

5. `examples/planner_process.cpp`
   - understand realistic child-side status generation

6. `src/child_status_json.cpp`
   - understand how JSON status is parsed back into supervisor-friendly form

This order gives the clearest progression from simple to advanced.

---

## 21. One-sentence summary

This repository uses Unix Domain Socket request-response IPC to connect `lmctl`, `supervisord`, and child processes, with a minimal child control contract and support for both simple and structured health reporting.
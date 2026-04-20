# lightweight_supervisor Learning Roadmap

This document provides a practical study roadmap for learning the `lightweight_supervisor` repository.

It is designed for readers who want to move through these stages:

1. understand what the repository does
2. understand how the main parts fit together
3. run it locally
4. read the code in a useful order
5. make small changes
6. move from "can read it" to "can modify it"

This is a learning guide, not an architecture reference.

---

## 1. What you should learn from this repository

If you study this repository carefully, you can learn several valuable topics at once:

- how a local supervisor works
- how to manage child processes in C++
- how to use Unix Domain Socket for local IPC
- how dependency-aware startup works
- how status polling and failure handling work
- how a CLI tool talks to a local daemon
- how to structure a lightweight control-plane system

So do not think of this as "just one C++ project".

It is a compact systems-programming case study.

---

## 2. Best mindset before reading code

Before reading code, it helps to keep one mental model in mind:

### The repository has three layers

#### Layer 1: operator layer
- `lmctl`

This is how the user interacts with the system.

#### Layer 2: supervision layer
- `supervisord`

This is the lifecycle manager and control center.

#### Layer 3: managed modules
- `dummy_process`
- `planner_process`
- and configured modules in `supervisor_config.json`

These are the children being supervised.

If you remember this three-layer model, the repository becomes much easier to follow.

---

## 3. Recommended study order

This is the most important section.

Read in this order.

---

### Step 1: Read `README.md`
Goal:
- understand project purpose
- understand module names
- understand build and run commands
- understand the recommended dependency graph

Do not start with `supervisor.cpp`.
Start with the README so the code has context.

---

### Step 2: Read `docs/architecture.md`
Goal:
- understand why the modules are layered this way
- understand why `map_manager` is independent
- understand why `task_manager` is above `planner`
- understand why `controller` is safety-sensitive
- understand why `hmi` is not in the core motion chain

This helps you understand the architectural intent, not just the code.

---

### Step 3: Read `supervisor_config.json`
Goal:
- understand the configured system topology
- understand process names
- understand dependencies
- understand roles
- understand which modules are critical
- understand restart behavior

At this point, you should be able to answer:

- who depends on whom
- which processes are critical
- which role each process has

---

### Step 4: Read `examples/dummy_process.cpp`
Goal:
- understand the minimum child-process IPC contract
- understand `PING`
- understand `GET_STATUS`
- understand `SET_MODE`
- understand test fault injection commands

This is the easiest source file to understand first.

It teaches the smallest unit of the system.

---

### Step 5: Read `tools/lmctl.cpp`
Goal:
- understand how operator commands are built
- understand how `lmctl` talks to `supervisord`
- understand how `status`, `events`, and `watch` are implemented

This helps you understand the user-facing side.

---

### Step 6: Read `src/ipc_client.cpp`
Goal:
- understand how supervisor talks to child sockets
- understand the local request-response pattern
- understand timeout-based child communication

This explains the transport mechanism from supervisor to child.

---

### Step 7: Read `src/control_server.cpp`
Goal:
- understand how supervisor exposes its own control socket
- understand how `lmctl` reaches the supervisor

This explains the transport mechanism from CLI to supervisor.

---

### Step 8: Read `src/supervisor.cpp`
Goal:
- understand the real control logic
- understand lifecycle orchestration
- understand failure handling
- understand monitoring
- understand mode switching
- understand command dispatch

This is the most important file.

Recommended internal reading order:

1. constructor and destructor
2. `add_process`
3. `start_control_interface`
4. `run_background_threads`
5. `start_all`
6. `stop_all`
7. `set_mode_all`
8. `handle_command_`
9. `start_one_`
10. `stop_one_`
11. `restart_one_`
12. `wait_ready_or_throw_`
13. `refresh_child_status_`
14. `monitor_children_loop_`
15. `poll_status_loop_`
16. `apply_mode_ordered_`
17. `topo_order_`

Do not try to understand the whole file in one pass.

---

### Step 9: Read `examples/thread_manager.cpp`
Goal:
- understand thread-level health tracking inside a child process
- understand heartbeat/progress monitoring
- understand why process-level health may depend on thread health

This is the bridge from simple child behavior to realistic module behavior.

---

### Step 10: Read `examples/planner_process.cpp`
Goal:
- understand a more realistic child implementation
- understand JSON status generation
- understand how internal state becomes external supervisor-visible health

This is where the whole architecture starts to feel realistic.

---

### Step 11: Read `src/child_status_json.cpp`
Goal:
- understand how supervisor parses structured child status
- understand JSON support as an extension over older key-value status

This closes the loop between advanced child implementation and supervisor interpretation.

---

## 4. Best way to run the project while learning

Reading code without running it is much less effective.

Use the project actively while reading.

---

### 4.1 Build the project

```bash
mkdir -p build
cd build
cmake ..
make -j
```

---

### 4.2 Start the supervisor

```bash
./supervisord ../supervisor_config.json
```

Open another terminal for control commands.

---

### 4.3 Basic commands to try

#### Start all processes
```bash
./lmctl start
```

#### Show global status
```bash
./lmctl status
```

#### Change mode
```bash
./lmctl mode AUTO
```

#### Watch live status
```bash
./lmctl watch
```

#### Show recent events
```bash
./lmctl events
```

#### Inspect one process
```bash
./lmctl proc_status planner
```

These commands help make the code paths concrete.

---

## 5. Best commands for learning failure handling

To understand the supervisor well, you must see failures happen.

That is one reason `dummy_process` supports fault injection.

Try these:

### Degrade a process
```bash
./lmctl child planner SET_HEALTH ERR
```

### Crash a process
```bash
./lmctl child planner CRASH
```

### Force a nonzero exit
```bash
./lmctl child planner EXIT 2
```

### Simulate hang
```bash
./lmctl child planner HANG
```

### Recover
```bash
./lmctl child planner RECOVER
```

Then observe:

```bash
./lmctl status
./lmctl events
./lmctl watch
```

This teaches the supervisor better than static reading alone.

---

## 6. What to pay attention to while running

When you run commands, pay attention to these aspects.

### 6.1 Startup order
When you call `start`, check whether dependency order matches the config.

### 6.2 Reachability
Notice that processes are not treated as healthy just because they exist.
They must respond over IPC.

### 6.3 Mode transitions
When switching to `AUTO` or `SAFE`, remember the role-based order:
- sensors
- compute
- actuators
or the reverse for safe shutdown

### 6.4 Event log
Watch how the event log records:
- starts
- stops
- restarts
- failures
- SAFE transitions

### 6.5 Degraded vs exited
Learn the distinction between:
- process still alive but unhealthy/unreachable
- process exited

That distinction is central to supervision logic.

---

## 7. Questions to ask yourself while reading

A good study method is to repeatedly ask concrete questions.

Examples:

### About lifecycle
- What counts as "started" in this project?
- Why is IPC reachability part of readiness?
- Why does stop use process groups?

### About architecture
- Why is the system configuration-driven?
- Why is `map_manager` independent?
- Why is `controller` special?

### About concurrency
- Why are there separate monitor and poll threads?
- What state is protected by mutexes?
- Where are condition variables needed?

### About failure handling
- What happens if a critical process exits?
- What happens if a non-critical process becomes unhealthy?
- What causes the system to enter SAFE?

### About protocols
- Why is the protocol simple text plus JSON?
- Why does JSON status parsing exist?
- Why keep compatibility with older key-value status?

If you can answer these questions, you are no longer just reading code.
You are understanding the design.

---

## 8. A practical study schedule

If you want a simple plan, use this.

### Day 1
- read `README.md`
- read `docs/architecture.md`
- read `supervisor_config.json`

### Day 2
- build and run project
- try `lmctl start`, `status`, `watch`, `events`

### Day 3
- read `examples/dummy_process.cpp`
- read `tools/lmctl.cpp`

### Day 4
- read `src/ipc_client.cpp`
- read `src/control_server.cpp`

### Day 5
- read `src/supervisor.cpp` part 1:
  - registration
  - startup
  - stop
  - command handling

### Day 6
- read `src/supervisor.cpp` part 2:
  - polling
  - monitor thread
  - mode ordering
  - event logging

### Day 7
- read `examples/thread_manager.cpp`
- read `examples/planner_process.cpp`
- read `src/child_status_json.cpp`

This is a very manageable study path.

---

## 9. First small modifications to try

Once you can run the project, the best way to learn deeper is to make small changes.

Start with safe, visible modifications.

---

### 9.1 Add a new command to `dummy_process`
Example:
- add `SET_DETAIL warning_state`

This helps you understand child-side IPC parsing.

---

### 9.2 Add a new field to `GET_STATUS`
For example:
- include `uptime_ms`

This helps you understand:
- child status generation
- supervisor parsing behavior
- display formatting

---

### 9.3 Adjust watch output in `lmctl`
Examples:
- change colors
- change summary rules
- show fewer fields

This helps you understand operator tooling.

---

### 9.4 Add one more process to config
For example:
- add a new fake module

This helps you understand:
- config-driven design
- dependency graph handling
- startup behavior

---

## 10. First medium modifications to try

After small modifications, try these.

---

### 10.1 Convert one child status path to full JSON
If a simple child still uses basic key-value status, try changing it to JSON.

This helps you understand:
- child-side serialization
- supervisor-side parsing
- backward-compatibility behavior

---

### 10.2 Add a new supervisor command
For example:
- `summary`
- `critical_only`
- `reachable_only`

This helps you understand:
- `handle_command_`
- status rendering
- control-server integration

---

### 10.3 Add one more event type
For example:
- explicit logging on successful mode transitions

This helps you understand:
- event buffer
- operator observability
- internal state transitions

---

## 11. How to move from "read" to "understand"

Many people can read code line by line but still do not really understand it.

To move beyond that, do these things:

### 11.1 Draw the process graph
Write down:
- each process
- each dependency
- each role

### 11.2 Draw the command path
Example:
- `lmctl status`
- control socket
- `handle_command_`
- `status_text`

### 11.3 Draw the health path
Example:
- poll thread
- `PING`
- `GET_STATUS`
- parse
- runtime update
- watch output

### 11.4 Draw the crash path
Example:
- child exits
- monitor thread sees exit
- runtime becomes `Exited`
- event logged
- restart or SAFE

If you can draw these flows from memory, you understand the repository.

---

## 12. Common mistakes while studying this repository

### 12.1 Starting with `supervisor.cpp` too early
Without architecture context, that file feels harder than it is.

### 12.2 Ignoring the config file
The config explains the runtime topology.
Without it, many supervisor decisions feel abstract.

### 12.3 Ignoring the example child processes
The child side explains what the supervisor expects.

### 12.4 Reading without running
This repository becomes much clearer once you observe:
- starts
- status
- events
- crashes
- restarts

### 12.5 Treating it as a data bus
This project is a control-plane supervisor, not a sensor-data transport system.

---

## 13. What "mastering this repository" looks like

You can say you really understand the repository if you can answer these confidently:

- How does startup order work?
- What makes a child "ready"?
- How does the supervisor detect crashes?
- How does the supervisor detect degraded health?
- How are mode transitions ordered?
- Why are process groups used for stopping?
- How do `lmctl`, `supervisord`, and a child talk to each other?
- Why does the system separate supervisor logic from child logic?
- How would you add a new managed process?
- How would you add a new status field end to end?

If you can answer those, you are beyond beginner level on this codebase.

---

## 14. Suggested next personal projects

After learning this repository, good extension exercises include:

### Project 1
Add a new child module with JSON status.

### Project 2
Turn the text child protocol into a consistent JSON-RPC command layer.

### Project 3
Add a simple local `busd` process for data pub-sub, while keeping the current supervisor as the command-plane manager.

### Project 4
Add better status classification:
- warning
- degraded
- failed
- unreachable

### Project 5
Add persistent log export instead of only in-memory recent events.

These projects are a natural next step.

---

## 15. One-sentence summary

The best way to learn `lightweight_supervisor` is to read the repository from architecture to config to simple child to IPC to supervisor core, while running the system and actively injecting failures to observe how the supervision logic behaves.
# C++ System Programming Notes for lightweight_supervisor

This document explains the main systems-programming concepts used in `lightweight_supervisor`.

The goal is to help readers connect the code with the underlying operating system mechanisms.

Topics covered include:

- processes
- `fork`
- `execvp`
- `waitpid`
- process groups
- signals
- Unix Domain Socket
- threads
- mutex and condition variables
- timeouts and polling
- why these mechanisms are used in this repository

This document is learning-oriented and closely tied to the repository design.

---

## 1. Why systems programming matters for this repository

`lightweight_supervisor` is not just ordinary C++ business logic.

It directly uses operating system facilities to manage local processes.

That means understanding this repository requires understanding concepts such as:

- how processes are created
- how processes are monitored
- how signals are sent
- how IPC works
- how threads coordinate
- how timeouts and waits are implemented

This repository is a very good example of practical Unix-style systems programming.

---

## 2. Process basics

A process is an operating system execution unit with:

- its own virtual address space
- its own file descriptors
- its own PID
- its own signal state
- its own threads

In this repository:

- `supervisord` is one process
- each child module is another process
- `lmctl` is another process

The supervisor manages other processes from the outside.

---

## 3. `fork`

`fork()` is the classic Unix system call for creating a new process.

When `fork()` is called:

- the current process is duplicated
- the parent gets the child's PID
- the child gets return value `0`

After `fork()`, both parent and child continue executing from the same code location.

---

### 3.1 Why `fork()` is used here
The supervisor uses `fork()` to create child processes.

That is the normal first step in launching a new managed process.

Typical pattern:

1. supervisor calls `fork()`
2. child branch prepares execution
3. child branch calls `execvp()`
4. parent branch records child PID and supervises it

This is the standard Unix process-launch model.

---

### 3.2 Parent and child branches
After `fork()`:

- parent branch:
  - stores PID
  - waits for readiness
  - tracks runtime state

- child branch:
  - sets up process group
  - builds argv
  - replaces itself with target executable via `execvp()`

This split is central to the repository's startup flow.

---

## 4. `execvp`

`execvp()` replaces the current process image with a new program.

Important point:

> `fork()` creates a new process  
> `execvp()` turns that process into the target program

So in the child branch, after `fork()`, the code calls `execvp()`.

If `execvp()` succeeds:
- the old child code is gone
- the new program starts running

If `execvp()` fails:
- the child usually exits with an error code

---

### 4.1 Why `execvp()` is used
The supervisor config contains `argv`, such as:

```text
./dummy_process /tmp/robot/lidar.sock
```

The child branch uses that argv to launch the configured binary.

This makes the supervisor configuration-driven.

---

### 4.2 Why `_exit()` is used on failure
If `execvp()` fails in the child process, the code typically calls `_exit(127)`.

Why `_exit()` instead of `exit()`?

Because after `fork()`, using `exit()` can flush inherited buffered state in ways you do not want.

In child-after-fork error paths, `_exit()` is the safer low-level choice.

This is standard Unix practice.

---

## 5. `waitpid`

`waitpid()` is used by a parent process to observe changes in child process state.

Common use:
- detect when a child exits
- retrieve exit status
- reap zombie processes

If a child exits and the parent never waits for it, it becomes a zombie.

---

### 5.1 Why supervisor uses `waitpid`
The monitor thread uses `waitpid(..., WNOHANG)` to check whether child processes have exited.

This is how the supervisor learns:

- process stopped normally
- process crashed
- process exited unexpectedly

It is also how the supervisor avoids zombie processes.

---

### 5.2 What `WNOHANG` means
`WNOHANG` means:

- do not block if the child has not exited yet

This is important because the monitor thread must keep looping over many processes.

Without `WNOHANG`, it could block on one child and stop monitoring others.

---

### 5.3 Exit status interpretation
After `waitpid()`, code often checks macros such as:

- `WIFEXITED(status)`
- `WEXITSTATUS(status)`
- `WIFSIGNALED(status)`
- `WTERMSIG(status)`

These tell you:

- did the process exit normally
- what was its exit code
- was it terminated by a signal

This data is used to update supervisor runtime state and event logs.

---

## 6. PID and process groups

A PID identifies one process.

A process group ID identifies a related group of processes.

This matters because a managed child might itself create subprocesses.

If you only signal the main PID, helper subprocesses may remain alive.

---

### 6.1 `setpgid`
`setpgid(pid, pgid)` places a process into a process group.

A common pattern is:

- child calls `setpgid(0, 0)`
- parent later also ensures `setpgid(pid, pid)`

This makes the child the leader of its own process group.

---

### 6.2 Why process groups are used here
The supervisor stops a process by signaling its process group, not just a single PID.

This is important for robust cleanup.

If the child process spawned descendants, group signaling helps stop them too.

This is a strong design choice in a supervisor.

---

## 7. Signals

Signals are asynchronous notifications delivered by the operating system.

Examples:

- `SIGTERM`
- `SIGKILL`
- `SIGINT`

In this repository, signals are mainly used to stop processes.

---

### 7.1 Graceful stop
A process may first receive a configurable graceful signal, often something like:

- `SIGTERM`

This gives the process a chance to:
- flush state
- shut down threads
- remove sockets
- exit cleanly

---

### 7.2 Forced kill
If graceful shutdown times out, the supervisor may send:

- `SIGKILL`

`SIGKILL` cannot be ignored or caught.

It is the last-resort cleanup mechanism.

---

### 7.3 Why negative PGID is used in `kill`
If code does:

```cpp
kill(-pgid, signal)
```

the negative value means:

- send the signal to the whole process group

This is how the supervisor stops a whole subtree more reliably.

---

## 8. Unix Domain Socket

Unix Domain Socket is a local IPC mechanism on Unix-like systems.

Instead of IP + port, it uses a local filesystem path such as:

```text
/tmp/robot/planner.sock
```

This repository uses `AF_UNIX` sockets for both:

- `lmctl` <-> `supervisord`
- `supervisord` <-> child process

---

### 8.1 Why not TCP
Because this repository is focused on one local machine.

UDS is simpler and more suitable for local control-plane communication.

Advantages:

- no network ports
- local-only by default
- less configuration
- easy to inspect and clean up
- good for command/response IPC

---

### 8.2 `AF_UNIX`
`AF_UNIX` is the socket address family used for Unix Domain Socket.

If you see:

```cpp
socket(AF_UNIX, SOCK_STREAM, 0)
```

that means:
- local Unix socket
- stream-oriented communication

---

### 8.3 `SOCK_STREAM`
`SOCK_STREAM` means a reliable byte-stream style socket.

It behaves similarly to a local version of TCP stream sockets, but without network transport.

This is a good fit for:

- request
- response
- request
- response

as used in this repository.

---

## 9. Socket server basics

A socket server typically does:

1. `socket()`
2. `bind()`
3. `listen()`
4. `accept()`
5. `read()`
6. `write()`

This pattern appears in:

- `ControlServer`
- child process control socket implementations

This is the standard local command server flow.

---

## 10. Threads

A thread is an execution path inside a process.

Threads in the same process share memory.

In this repository, the supervisor uses multiple threads for different responsibilities.

Typical threads include:

- main thread
- monitor thread
- poll thread

`planner_process` may also use multiple internal threads.

---

### 10.1 Why threads are used in supervisor
The supervisor needs to do several things concurrently:

- respond to control commands
- monitor process exits
- poll child health periodically

If this were all done in one blocking loop, responsiveness would be worse and the code would be harder to structure.

Threads let responsibilities be separated.

---

## 11. `std::mutex`

A mutex protects shared state from concurrent access.

In this repository, multiple threads may access:

- process runtime state
- manager state
- event buffer
- control flags

So a mutex is required to avoid races and inconsistent views.

---

### 11.1 Why it matters
Without a mutex, you could have situations like:

- poll thread updates runtime while status command reads it
- monitor thread changes state while stop logic runs
- event buffer modified by two threads simultaneously

That would cause data races and undefined behavior.

---

## 12. `std::condition_variable`

A condition variable allows one thread to wait until some condition becomes true.

It is often used together with a mutex.

In this repository, condition variables are useful for:

- waiting for startup readiness
- waiting for stop completion
- coordinating state transitions

---

### 12.1 Example in supervisor logic
When stopping a process, the supervisor may:

- signal the process
- then wait until runtime state becomes `Stopped` or `Exited`

A condition variable is a natural fit for this pattern.

Without it, the code would need clumsy busy-wait loops.

---

## 13. Polling and sleep intervals

The repository uses periodic loops that:

- do some work
- sleep briefly
- repeat

Examples:

- poll child status every fixed interval
- monitor child exit state periodically

This is a common systems-programming pattern when event-driven infrastructure is kept intentionally simple.

---

### 13.1 Why polling is acceptable here
This project is a lightweight local supervisor.

It does not need a highly complex event-driven framework.

Periodic polling is acceptable because:

- process count is limited
- command volume is low
- status polling frequency is moderate
- code stays simple and understandable

---

## 14. Timeouts

Timeouts are everywhere in a supervisor.

Examples:

- startup timeout
- stop timeout
- ping timeout
- status timeout

Timeouts are essential because external processes may:

- hang
- block
- fail to respond
- partially initialize
- ignore graceful stop

A supervisor cannot wait forever.

---

### 14.1 Startup timeout
After launching a child, the supervisor waits for it to become reachable.

If it never responds to control IPC within the timeout, startup fails.

This prevents the system from treating a hung startup as success.

---

### 14.2 Stop timeout
After sending a graceful stop signal, the supervisor waits for exit.

If that takes too long, it may escalate to `SIGKILL`.

This prevents shutdown from hanging forever.

---

### 14.3 IPC timeout
When sending `PING` or `GET_STATUS`, the supervisor uses request timeouts.

This prevents a stuck child socket operation from blocking the whole supervisor indefinitely.

---

## 15. Reachability vs existence

This repository makes an important distinction.

A process may:

- exist at the OS level
- but still be unreachable over IPC

The supervisor treats readiness and liveness more strictly than just "PID exists".

This is an important systems design idea.

---

### 15.1 Why PID alone is not enough
A process might:

- be alive
- but still not have created its control socket
- or be stuck before its IPC server loop starts
- or have an unresponsive control handler

So the repository checks:

- process existence
- IPC reachability
- status response

This is much better than a naive launcher.

---

## 16. Event logging

The supervisor keeps an in-memory event log.

This is not a kernel-level mechanism, but it is an important systems feature.

It helps operators understand runtime behavior such as:

- startup order
- exits
- restarts
- SAFE transitions
- polling failures

A supervisor without event history is much harder to debug.

---

## 17. Defensive destruction and cleanup

System code must assume failures happen.

This repository shows defensive cleanup patterns such as:

- best-effort SAFE entry
- best-effort stop of all processes
- joining threads during shutdown
- stopping control interface even if earlier cleanup failed

This is important because partial shutdown failures are common in systems code.

---

## 18. Why this repository is a good systems-programming example

This codebase teaches several practical Unix patterns in one place:

### 18.1 Process lifecycle management
- `fork`
- `execvp`
- `waitpid`

### 18.2 Signal-based shutdown
- graceful signal
- forced kill
- process groups

### 18.3 Local IPC
- Unix Domain Socket
- request-response design

### 18.4 Concurrent coordination
- threads
- mutex
- condition variable

### 18.5 Runtime resilience
- timeouts
- polling
- restart policy
- SAFE fallback

This makes it a strong educational example.

---

## 19. What this repository intentionally avoids

It is also useful to understand what is not here.

This repository does not try to be:

- a distributed service mesh
- a high-frequency data bus
- a shared-memory transport framework
- a full RPC schema platform
- a multi-machine orchestrator

It intentionally focuses on a narrower problem:

> local control-plane supervision of robot processes

That focus is why the design stays relatively clear.

---

## 20. Suggested reading order for systems concepts

If you want to connect the code with OS concepts step by step, study in this order:

1. Unix processes and PID basics
2. `fork`
3. `execvp`
4. `waitpid`
5. signals and process groups
6. Unix Domain Socket
7. threads
8. mutex and condition variable
9. polling and timeout patterns

Then reread:

- `src/supervisor.cpp`
- `src/ipc_client.cpp`
- `src/control_server.cpp`
- `examples/dummy_process.cpp`

After that, the repository will feel much more natural.

---

## 21. One-sentence summary

`lightweight_supervisor` is a practical example of Unix-style C++ systems programming that combines process creation, signal-based control, local socket IPC, multithreaded coordination, and timeout-driven supervision into a single lightweight runtime manager.
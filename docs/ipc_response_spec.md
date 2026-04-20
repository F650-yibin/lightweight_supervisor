# Final IPC Response Specification for lightweight_supervisor

This document defines the **practical final IPC response specification** for the current `lightweight_supervisor` repository.

It is intentionally designed to:

- match the current implementation style
- stay fully compatible with existing supervisor logic
- remain easy to implement in simple child processes
- support both legacy text status and newer JSON status

This spec is for **control-plane IPC only**.

---

## 1. Scope

This specification applies to request-response IPC between:

- `lmctl` and `supervisord`
- `supervisord` and child processes

Typical commands include:

- `PING`
- `GET_STATUS`
- `SET_MODE <mode>`
- `start`
- `stop`
- `restart`
- `proc_status`
- `events`
- `child <proc> <raw_command...>`

This specification does **not** apply to data streaming or pub-sub traffic.

---

## 2. Core compatibility rule

For the current repository, every IPC response must follow these rules:

1. response must be non-empty
2. response must end with newline on the wire
3. response must begin with either:
   - `OK`
   - `ERR`

This is the most important rule.

The current supervisor implementation already relies on this behavior.

---

## 3. Final response forms

The repository should standardize on exactly two response families:

### Success
```text
OK ...
```

### Failure
```text
ERR detail=...
```

This means:

- all successful commands must start with `OK`
- all failures should start with `ERR`
- all failures should include `detail`

---

## 4. Text response format

The default text format is:

```text
OK [key=value ...]
ERR detail=reason
```

Examples:

```text
OK
OK pong
OK mode=SAFE
OK mode=AUTO health=OK detail=ready
ERR detail=unknown_command
ERR detail=unknown_mode
ERR detail=not_ready
```

This is the official text format for the repository.

---

## 5. Command-specific final rules

---

### 5.1 `PING`

#### Required success response
```text
OK pong
```

#### Allowed failure response
```text
ERR detail=not_ready
```

#### Notes
- `pong` is required by convention
- caller mainly checks the `OK` prefix
- this response should be fast and minimal

---

### 5.2 `SET_MODE <mode>`

#### Required success response
```text
OK mode=<MODE>
```

Examples:

```text
OK mode=SAFE
OK mode=AUTO
OK mode=DIAG
```

#### Required failure response
```text
ERR detail=<reason>
```

Recommended reasons:

- `unknown_mode`
- `mode_transition_rejected`
- `not_ready`
- `internal_error`

#### Notes
Returning the applied mode is required by this final spec because it is more explicit than plain `OK` and still easy to implement.

---

### 5.3 `GET_STATUS`

This command has two allowed final formats:

1. legacy text format
2. JSON status format

---

#### 5.3.1 Legacy text format

Required text fields:

- `mode`
- `health`
- `detail`

Required form:

```text
OK mode=<MODE> health=<HEALTH> detail=<DETAIL>
```

Examples:

```text
OK mode=SAFE health=OK detail=ready
OK mode=AUTO health=ERR detail=planner_slow
OK mode=DIAG health=FAIL detail=watchdog_timeout
```

#### Field requirements

##### `mode`
Required.

Allowed values are typically:

- `SAFE`
- `IDLE`
- `TELEOP`
- `AUTO`
- `DIAG`
- `INIT`
- `UNKNOWN`

##### `health`
Required.

Allowed values:

- `OK`
- `ERR`
- `FAIL`
- `UNKNOWN`

##### `detail`
Required.

Must be a single token in legacy text mode.

Use underscore instead of spaces.

Good:
```text
detail=planner_ready
```

Bad:
```text
detail=planner ready
```

---

#### 5.3.2 JSON status format

Advanced child processes may return JSON instead of legacy key-value text.

This is already compatible with the current repository direction.

Recommended JSON fields:

- `module`
- `runtime_state`
- `mode`
- `health`
- `detail`
- `process_failed`
- `process_degraded`

Example:

```json
{
  "module": "planner",
  "runtime_state": "Running",
  "mode": "AUTO",
  "health": "OK",
  "detail": "planner ready",
  "process_failed": false,
  "process_degraded": false
}
```

#### Final rule
- simple children may use text format
- advanced children should prefer JSON format
- supervisor must continue to support both

---

### 5.4 Unknown command

Required response:

```text
ERR detail=unknown_command
```

This should be the default fallback for all unrecognized commands.

---

### 5.5 Generic setter/debug commands

Examples:

- `SET_HEALTH`
- `SET_DETAIL`
- `SET_DELAY_MS`
- `RECOVER`

#### Required success response
```text
OK
```

#### Preferred success response
```text
OK detail=applied
```

#### Required failure response
```text
ERR detail=<reason>
```

Examples:

```text
ERR detail=invalid_argument
ERR detail=unknown_command
```

---

## 6. Final required field vocabulary

To reduce drift across modules, this repository should standardize the following response keys.

---

### 6.1 `detail`
Required for all `ERR` responses.  
Required for text `GET_STATUS`.

Purpose:
- short human-readable reason
- short status summary

Examples:

- `detail=ready`
- `detail=unknown_mode`
- `detail=planner_slow`
- `detail=watchdog_timeout`
- `detail=internal_error`

---

### 6.2 `mode`
Required for:
- `SET_MODE` success response
- text `GET_STATUS`

Examples:

- `mode=SAFE`
- `mode=AUTO`

---

### 6.3 `health`
Required for text `GET_STATUS`.

Allowed values:

- `OK`
- `ERR`
- `FAIL`
- `UNKNOWN`

Interpretation:

- `OK`: healthy
- `ERR`: degraded but still alive
- `FAIL`: failed or severe fault
- `UNKNOWN`: no reliable status yet

---

## 7. Final status policy for current repository

The repository should treat child status like this:

### Text child
Must return:

```text
OK mode=<MODE> health=<HEALTH> detail=<DETAIL>
```

### JSON child
Should return a structured JSON object with at least:

- `mode`
- `health`
- `detail`

and preferably also:

- `runtime_state`
- `process_failed`
- `process_degraded`

This is the final repository policy.

---

## 8. Final parsing policy

The supervisor should continue using this policy:

1. if `GET_STATUS` looks like JSON and parses successfully:
   - use JSON fields
2. otherwise:
   - parse legacy key-value text
3. if parsing fails:
   - mark child unreachable or degraded as appropriate

This preserves compatibility and supports gradual migration.

---

## 9. Final examples

---

### 9.1 `PING`
```text
OK pong
```

---

### 9.2 `SET_MODE SAFE`
```text
OK mode=SAFE
```

---

### 9.3 `SET_MODE AUTO`
```text
OK mode=AUTO
```

---

### 9.4 Invalid mode
```text
ERR detail=unknown_mode
```

---

### 9.5 Text `GET_STATUS`
```text
OK mode=SAFE health=OK detail=ready
```

---

### 9.6 Degraded text `GET_STATUS`
```text
OK mode=AUTO health=ERR detail=planner_slow
```

---

### 9.7 Failed text `GET_STATUS`
```text
OK mode=DIAG health=FAIL detail=watchdog_timeout
```

---

### 9.8 JSON `GET_STATUS`
```json
{
  "module": "planner",
  "runtime_state": "Running",
  "mode": "AUTO",
  "health": "OK",
  "detail": "planner ready",
  "process_failed": false,
  "process_degraded": false
}
```

---

### 9.9 Unknown command
```text
ERR detail=unknown_command
```

---

## 10. Rules for child implementers

If you are implementing a new child process for this repository, follow these rules.

### Minimum required commands
Your child must support:

- `PING`
- `GET_STATUS`
- `SET_MODE <mode>`

### Required responses
- `PING` -> `OK pong`
- `SET_MODE <mode>` -> `OK mode=<mode>` or `ERR detail=<reason>`
- `GET_STATUS` -> text status or JSON status
- unknown command -> `ERR detail=unknown_command`

### Strong recommendation
If your child is simple:
- use text `GET_STATUS`

If your child has richer health state:
- use JSON `GET_STATUS`

---

## 11. Rules for supervisor-side assumptions

The supervisor may safely assume:

- success responses begin with `OK`
- failure responses begin with `ERR`
- `PING` success means reachable
- `SET_MODE` success means mode accepted
- `GET_STATUS` text contains `mode`, `health`, `detail`
- `GET_STATUS` JSON may provide richer status

These assumptions define the stable contract.

---

## 12. What is intentionally not standardized yet

To keep the repository simple, the following are **not** required yet:

- request IDs
- numeric error codes
- full JSON-RPC envelope
- schema versioning
- nested structured text fields
- machine-readable command IDs in text mode

These can be added later if the control protocol evolves.

---

## 13. Final repository standard

The final standard for the current repository is:

1. all replies must begin with `OK` or `ERR`
2. all failures must include `detail`
3. `PING` must return `OK pong`
4. `SET_MODE` success must return `OK mode=<MODE>`
5. text `GET_STATUS` must return:
   - `mode`
   - `health`
   - `detail`
6. advanced children may return JSON for `GET_STATUS`
7. supervisor must support both text and JSON status

This is the final practical contract for `lightweight_supervisor`.

---

## 14. One-sentence summary

For the current `lightweight_supervisor` repository, the final IPC response standard is: every reply must start with `OK` or `ERR`, failures must include `detail`, `PING` returns `OK pong`, `SET_MODE` returns `OK mode=<MODE>`, and `GET_STATUS` returns either text with `mode/health/detail` or structured JSON.
# M0.3b-B1-P3 live diagnostic retrieval architecture

Status: host-only architecture. No helper, listener, unit, package, hook change,
or device payload is implemented. Physical execution remains unauthorized.

## Requirement and non-negotiable evidence

B1 must prove that the DATA encoder hook both receives callbacks and returns
through the original stock handler while the stock application remains usable.
Hook installation is not forwarding evidence.

The existing B0.2 snapshot is the authoritative counter source. Its reader
requires the v2 magic, exact 72-byte length, stable repeated reads, counter
invariants, recorded PID, exact `/usr/bin/MPC Sample` executable identity, and
matching Linux process-start token. Its formatted output is at most 384 bytes.
A valid B1 observation additionally requires:

- constructor report fields for exact firmware/process, DATA-only selection,
  successful installation, forwarding readiness, and snapshot readiness;
- `callback_invocations > 0`;
- `successful_forwards > 0` and not greater than invocations;
- delta-class totals equal to invocations; and
- the same PID/start token from arming through the post-interaction read.

The workflow is load once, confirm the armed process, interact only with DATA,
read once without restarting, export only the bounded report, then return to a
stock-only boot.

## Established lifecycle facts

### Confirmed by repository source

- The stock application accepts the SD autorun and launches its shell.
- The generated shell verifies the stock executable prefix, copies the preload
  library to `/run`, creates an `ac50.service` drop-in under `/run/systemd`,
  reloads systemd, and ends with `systemctl restart ac50`.
- No command follows the restart. No current package starts a helper, worker,
  timer, unit, listener, or post-interaction export.
- The actual `ac50.service` unit and `ExecStart` process chain are absent from
  the repository. A3's PID/PPID observations do not reconstruct its cgroup or
  systemd kill policy.
- A4/B0 writers can identify one writable exFAT SD mount and publish bounded,
  no-clobber reports. This is constructor-time output, not a later trigger.
- B0.2 reads only the shared file and `/proc` identity. It has no `ptrace`,
  signal, process-memory access, command interface, or network code.
- The pinned SDK supplies callbacks/tasks inside the stock process, but no
  supported external command runner, post-interaction export callback, shutdown
  hook, or safe unload/quiescence protocol.
- The updater HTTP server has no evidenced diagnostic handler. P2.1 finds the
  remote-object client apparently unused by the updater, and the physical root
  object request returned 404.

### Confirmed physically

- A2--A4 prove autorun execution, preload inheritance into multiple contexts,
  direct SD publication, exact stock-process recognition, all 63 firmware
  fingerprints, and stock-only reboot recovery on one 1.3.0.12 unit.
- USB CDC-NCM and the updater HTTP service are reachable while stock firmware
  runs. They do not expose `/run`, `/proc`, or commands.

### Source-supported generic systemd behavior, not yet device fact

A process launched in a separate systemd unit normally has a lifecycle separate
from `ac50.service`; a child left in the `ac50` unit is normally subject to that
unit's stop/kill policy. Upstream systemd documents `KillMode=control-group` as
the default and warns against allowing processes to escape unit lifecycle
management: [systemd.kill](https://www.freedesktop.org/software/systemd/man/latest/systemd.kill.html).
The device's unit configuration, systemd version,
available sandbox directives, and behavior of a new transient `/run` unit have
not been observed. Therefore a forked/background autorun child cannot be
assumed to survive, while an independent unit is a design hypothesis requiring
explicit physical validation.

### Missing evidence

- Exact `ac50.service` unit, cgroup membership, user/group, and restart/kill
  behavior.
- Whether a separately named runtime-only service unit can start before the
  restart, remain alive across it, and access the new process's snapshot.
- The USB network interface/address readiness order and firewall policy.
- Which existing UID can read the mode-0600 snapshot and `/proc` identity.
- Availability/version of systemd hardening and syscall-filter features.

No existing IPC, callback, filesystem event, stock export, or service fills
these gaps today.

## Candidate comparison

| Candidate | Trigger and transfer | Same PID? | Evidence/risk | Assessment |
|---|---|---|---|---|
| A. Existing one-shot export | An already-supported command runs B0.2 once and writes the validated SD report | Yes if invoked externally | No post-start command mechanism exists. Autorun continuation and passive SD copying are unproven. | Preferred in principle; unavailable |
| B. Delayed transient helper | Independent helper survives the restart, waits a fixed deadline or trigger, runs B0.2 once, writes SD, exits | Plausible | A forked child may be killed with `ac50`; an independent unit is new. Fixed delay cannot safely expose pre-interaction armed state, and SD visibility/hotplug behavior is unresolved. | Simpler than networking but operationally incomplete |
| C. USB-network diagnostic relay | Independent, short-lived component exposes only arming state and one validated snapshot operation on the confirmed USB address | Plausible | Requires a new listener/unit and privilege review, but solves both safe arming confirmation and operator-controlled post-interaction timing without callback I/O or SD removal. | Recommended future design, conditional on lifecycle proof |
| D. Existing stock interface | Reuse updater/object/storage/diagnostic function | Unknown | Updater is version/upload only; object API is apparently unused and subtree is 404; storage is the wrong namespace. | No suitable interface established |
| E. Defer B1 | No device action | N/A | Preserves all current guarantees but yields no physical forwarding evidence. | Required until C's gates pass |

### Simpler alternatives

- **Fixed delayed read:** one sleep and one SD write is small, but the operator
  cannot safely verify installation state before interaction or choose the exact
  read time. A delay expiring before the UI is ready produces ambiguous failure.
- **SD sentinel:** an operator-created trigger would require a demonstrated safe
  concurrent host/device storage contract. A block device exported to a host
  must not be assumed safe for simultaneous device writes.
- **Background autorun child:** process ancestry and unit kill policy are
  unknown. Shell backgrounding is not a lifecycle boundary.
- **Stock callbacks/signals:** callback I/O, an additional hook, or an
  undocumented signal handler would increase in-process risk and is rejected.
- **Existing HTTP/object API:** it cannot invoke B0.2 on current evidence.

These findings make a narrow, temporary network relay more complex than a delay
but materially safer to operate: it can report “armed” before interaction and
accept exactly one explicit retrieval after interaction.

## Recommended design: one-shot USB diagnostic relay

This is a future Candidate C design combined with Candidate B's independent
runtime-only lifecycle. It does not extend or modify the stock AZ0X web server.

```text
reviewed SD autorun
    ├── stages DATA-only preload + existing B0.2 reader in /run
    ├── stages a runtime-only diagnostic unit in /run/systemd/system
    ├── starts diagnostic relay unit (independent of ac50.service)
    └── restarts ac50.service once
                         │
                         ▼
new /usr/bin/MPC Sample process ──DATA callback──► 72-byte /run snapshot
            │                                         │
            └── bounded constructor report            │
                         │                             │
                         ▼                             ▼
              one-shot diagnostic relay ──validates with existing reader
                         │
             USB-only fixed response (≤384-byte body)
                         │
                         ▼
                 relay exits; stock PID continues
```

### Components

1. **Existing DATA-only preload:** unchanged B0/B0.2 callback and snapshot
   protocol. It remains process-lifetime and performs atomics only in callbacks.
2. **Existing reader library logic:** reused without weakening any identity,
   stability, or counter check.
3. **New relay executable:** future separately reviewed artifact. It has no
   command execution, file selection, upload, directory listing, proxy, static
   file serving, or general RPC.
4. **New runtime-only service unit:** stored only under `/run`, no enablement,
   restart policy, persistent configuration, or device-disk write.

### Startup and process ownership

The autorun must start the relay as a **separate systemd service unit before** invoking
the existing `ac50` restart. Merely forking or backgrounding from the autorun is
forbidden. The relay unit owns exactly one process, has `Restart=no`, a hard
runtime deadline, and is not part of `ac50.service`'s cgroup.

The relay begins in `WAITING_FOR_PROCESS`. It watches `/run` for a bounded
period using a filesystem notification plus a monotonic deadline; it does not
busy-poll. It accepts exactly one snapshot candidate whose PID is also named by
the bounded constructor report. It then validates:

- expected report experiment/patch identifiers;
- `firmware_compatible=true` and 63 checked words;
- expected report category and SD eligibility;
- `selected_hook=DATA_HOOK`;
- required site prepared;
- installation succeeded and forwarding ready;
- snapshot ready and report PID present; and
- the snapshot's full B0.2 PID/executable/start-token checks.

Unexpected, duplicate, malformed, stale, inaccessible, or inconsistent report
and snapshot files fail closed. No interaction is permitted unless the relay
reports `ARMED`.

### Network binding and protocol

After arming, the relay binds one reviewed unprivileged TCP port on the exact
CDC-NCM device address `192.168.155.1`, never wildcard, loopback, Wi-Fi, or all
interfaces. The exact port is selected only after passive device service and
firewall evidence rules out collision. Failure to bind is a clean no-go.

The protocol is intentionally smaller than a management API:

- `GET /v1/status` returns fixed-schema state: `WAITING`, `ARMED`, `FAILED`, or
  `EXPIRED`, plus the expected PID/start token only when armed.
- `POST /v1/snapshot` requires an empty body, is accepted once only in `ARMED`,
  runs the existing reader synchronously outside the stock process, and returns
  its maximum-384-byte text body.
- Every other method/path, any body, repeated trigger, malformed request, or
  response-overflow attempt is rejected and never reaches a filesystem path.

Request headers and total request size are fixed and small; parsing has a short
read deadline. Responses have a fixed content type and `Content-Length`, close
the connection, and include no directory, environment, firmware bytes, logs,
or error internals. The component serves no `/run` or `/proc` path; those are
private validation inputs.

A per-run capability token should be included only if it can be delivered to
the host without expanding the channel. USB-address binding plus a single
physical cable limits exposure but is not authentication. Token delivery is an
open design detail; a static token provides little security and is rejected.

### Execution sequence

1. Future approved package inspection verifies hashes, exact firmware guard,
   DATA-only ELF allowlist, relay allowlist, transient unit, and no persistence.
2. Autorun stages artifacts in `/run`, starts the independent relay unit, and
   performs the single approved `ac50` restart.
3. The new stock process creates the snapshot and constructor report, installs
   only DATA, and continues normal startup.
4. The relay validates the report and snapshot identity, then exposes `ARMED`.
5. Operator checks `GET /v1/status`; any state other than `ARMED` is a stop.
6. Operator confirms normal stock UI/audio/DATA behavior, turns DATA through a
   small bounded sequence in both directions, then stops interaction.
7. Operator sends the one allowed `POST /v1/snapshot` with an empty body.
8. Relay invokes unchanged reader logic, returns the bounded report, closes the
   listener, and exits regardless of success or failure.
9. Operator requires nonzero invocations and successful forwards with valid
   invariants and unchanged PID/start token.
10. Normal shutdown, removal of test SD, and stock-only reboot remove `/run`
    artifacts. Full stock regression follows.

### Bounds and timeouts

Exact values require host state-machine tests and device startup measurements,
but the approved design must fix them at build time:

- one relay process and one listening socket;
- one simultaneous connection;
- one status request at a time and one lifetime snapshot trigger;
- request header/body bounds substantially below 4 KiB; snapshot request body
  exactly zero;
- formatted diagnostic body no greater than the existing 384-byte bound;
- short per-connection read/write deadlines;
- bounded process-discovery/arming interval;
- bounded overall runtime sufficient for one controlled interaction, followed
  by unconditional exit; and
- no restart, retry loop, dynamic allocation after initialization where it can
  be avoided, or unbounded log/output storage.

The relay is not real-time code and never runs in an audio/DATA callback.

### Privileges and hardening

Prefer the same non-root UID that owns the snapshot if device evidence confirms
it can read both snapshot and required `/proc` entries. Otherwise a root relay
is a material risk requiring explicit approval. A high port avoids bind
capabilities. Subject to the device's actual systemd support, the unit should
deny privilege escalation, devices, writable system paths, executable memory,
new namespaces, and unrelated address families/syscalls. It needs read-only
access only to the exact constructor report, one snapshot, and two `/proc`
identity files, plus AF_INET on the exact USB address.

The relay must not inherit `LD_PRELOAD`; its unit explicitly clears that
environment so the diagnostic library is loaded only into the intended stock
service chain.

### Failure modes

| Failure | Required behavior |
|---|---|
| Relay is killed by `ac50` restart | No interaction; lifecycle gate fails |
| No/late snapshot or constructor report | Expire and exit; no interaction |
| Installation failure or indeterminate status | `FAILED`, no snapshot trigger; controlled stock-only recovery |
| Wrong/changed PID, executable, or start token | Reject and exit; never report counters |
| Malformed/unstable counters | Return bounded generic failure, exit, preserve stock process |
| Bind/interface/firewall failure | No interaction; exit without fallback to wildcard or another interface |
| Timeout or host disconnect | Close and exit at hard deadline; no restart/retry daemon |
| SD absent after startup | Network report remains possible; rollback is still normal stock-only reboot |
| Report shows invocations but no forwards | B1 failure; stop and stock-only recovery |

There is no live hook removal. Any indeterminate branch publication retains its
mapped trampoline and uses process restart/stock-only reboot as already defined.

## Why this design is selected

Candidate A/D would be safer but no supporting device mechanism exists.
Candidate B's fixed delay avoids a listener but cannot provide an unambiguous
pre-interaction arm gate or operator-controlled read point; SD triggering adds
unproven concurrent-storage risk. Candidate C uses a physically confirmed
transport and can be strictly one-shot, bounded, USB-only, and independent of
the stock process. It adds attack surface, so it is selected only as the best
**future architecture**, not as permission to implement or deploy it.

Until the independent-unit lifecycle, UID, bind scope, and hardening support are
physically established, Candidate E remains the operational choice.

## Minimum physical observations before implementation approval

A separate read-only lifecycle-discovery authorization must establish:

1. redacted `ac50.service` unit properties relevant to user/group, cgroup,
   `KillMode`, `Type`, restart, and process chain;
2. whether a harmless independent runtime-only `/run` service survives one
   `ac50` restart without changing stock behavior;
3. systemd version and supported hardening directives;
4. exact CDC-NCM device address/interface readiness and ability to bind only
   `192.168.155.1` without collision;
5. firewall/routing exposure of that bind beyond the physical USB link;
6. snapshot/report ownership and the least UID able to read required files; and
7. stock PID/start-token continuity while the updater HTTP service and CDC-NCM
   interface are used.

These observations must not deploy the DATA hook or diagnostic relay. A generic
Linux expectation is not acceptance evidence.

## Incremental implementation plan

### P3.1 — Host protocol and state-machine model

- **Scope:** model `WAITING → ARMED → REPORTED/FAILED/EXPIRED`, constructor
  report parsing, one-shot request grammar, bounds, deadlines, and failure
  mapping entirely on host fixtures.
- **Likely files:** a new isolated host-only crate or module under a future
  `device/m03b-b1/`, plus tests; no B0 or SDK source changes.
- **Tests:** malformed/duplicate reports, wrong PID, every install status,
  request fragmentation/overflow, wrong methods/paths, repeated trigger,
  timeout transitions, response size, and reader error mapping.
- **Static checks:** dependency/license review, no command execution, no path
  parameters, no general HTTP framework if a fixed parser suffices.
- **Device dependency:** none.
- **Acceptance:** deterministic bounded model; existing reader reused intact;
  no deployable package or listener started by tests.
- **Authorization:** architecture/host implementation approval only.

### P3.2 — Read-only lifecycle discovery

- **Scope:** collect only the seven device facts above using a separately
  reviewed harmless procedure; no hook.
- **Files:** documentation/evidence only.
- **Tests/static checks:** procedure allowlist, redaction, stop conditions.
- **Device dependency:** physical unit and explicit authorization.
- **Acceptance:** independent-unit survival, UID, bind, exposure, and systemd
  contracts are observed rather than assumed.
- **Rollback:** normal shutdown/reboot; no persistent configuration.

### P3.3 — Cross-built relay and runtime-unit review

- **Scope:** implement the already host-tested one-shot relay for AArch64 and a
  runtime-only unit template, without combining it with the hook package.
- **Likely files:** isolated `device/m03b-b1/` sources/scripts and provenance.
- **Tests:** host backends for filesystem notification, socket, time, reader,
  and unit lifecycle; all P3.1 cases against production parser/state machine.
- **Static checks:** ELF imports/exports/dependencies, no shell/exec, no dynamic
  endpoint/path, no W+X, exact response strings, bind address/port, timeout and
  unit policy inspection.
- **Device dependency:** none for build; P3.2 must already pass.
- **Acceptance:** reproducible inspected artifacts, but no autorun package.
- **Authorization:** separate implementation approval; no physical run.

### P3.4 — Zero-hook relay lifecycle test

- **Scope:** package the relay with the already validated A4 zero-hook probe to
  verify independent survival, arming/status, one-shot response, timeout, exit,
  and stock-only recovery without DATA mutation.
- **Tests:** pre/post stock regression and exact PID/start-token continuity.
- **Device dependency:** physical unit.
- **Acceptance:** one unit/firmware observation with bounded response and no
  persistence; does not authorize B1.
- **Rollback:** shutdown, card removal, stock-only reboot.
- **Authorization:** explicit physical authorization.

### P3.5 — Combined B1 package review and physical run

- **Scope:** only after P3.4, combine unchanged DATA-only hook/snapshot with the
  reviewed relay and conduct the original bounded encoder procedure.
- **Checks:** full package hashes, firmware/ELF guards, initial arm state,
  nonzero invocations/forwards, stock behavior, timeout/exit, and recovery.
- **Acceptance:** forwarding evidence from the same PID/start token plus normal
  stock regression.
- **Rollback:** existing process-restart and stock-only boot boundary.
- **Authorization:** new explicit B1 physical-test approval.

Architecture approval does not imply approval of P3.3 implementation, P3.4
deployment, or P3.5 hook execution.

## Go/no-go

**No-go for B1 physical testing.** The recommended relay is sufficiently
specific to guide P3.1, but it depends on unverified device lifecycle and
privilege facts. The next safe engineering milestone is host-only P3.1; the
next device milestone, if separately authorized, is zero-hook lifecycle
discovery—not a DATA-hook run.

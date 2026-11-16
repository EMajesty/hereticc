# M0.3b-B1-P1 device-access discovery

Status: host-only source and evidence review. No device was accessed. No hook,
package, reader payload, service, or deployment artifact was built or run.

## Executive conclusion

The reviewed evidence does **not** establish a supported, non-restarting path
that can retrieve B1 counters after DATA encoder interaction while the same
stock application process remains alive.

The strongest known components do not yet form an end-to-end path:

- A4 physically confirmed that a guarded preload constructor can write a
  bounded report directly to the SD data partition, but the constructor runs at
  process startup, before the proposed interaction.
- The existing autorun shell ends by restarting `ac50.service`. Its source does
  not prove that the shell survives that restart, and the shell is launched by
  the application whose service it restarts.
- B0.2 contains a bounded external reader with the necessary snapshot and live
  process checks, but no reviewed evidence establishes how to invoke it on the
  device after interaction without restarting `ac50`.
- No reviewed repository, pinned-SDK, or A2--A4 physical evidence establishes
  host access to `/run`, a supported read-only command channel, or a stock
  operation that copies an arbitrary bounded file from `/run` to SD.

This is a **no-go for B1 physical testing**, not proof that the device lacks
every possible mechanism. The minimum missing fact is an observed, supported
command or file-access operation that is available after interaction, can read
the live process's `/run` snapshot and `/proc` identity, emits only the bounded
report, and does not restart or replace the stock PID.

## Scope and evidence rules

This review covers the repository's pinned SDK at commit
`e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39`, the generated A4 autorun, the A4
and B0 sources, and the recorded A2--A4 observations. It does not treat a
generic Linux facility as device evidence. In particular, the SDK's SSH and
HTTP documentation describes the separate virtual-lab environment, not a
physical MPC interface.

Evidence is classified as follows.

### Confirmed by reviewed source

- The generated A4 autorun verifies the first 4096 bytes of
  `/usr/bin/MPC Sample`, copies one library to `/run`, creates a transient
  `ac50.service` drop-in under `/run/systemd/system`, reloads systemd, and ends
  with `systemctl restart ac50`.
- The autorun has no command after the restart, retry, resident worker, polling
  loop, listener, or post-interaction retrieval step.
- The generated source does not include the device's actual `ac50.service`
  `ExecStart` or process chain. Environment inheritance alone does not establish
  shell survival or a later command opportunity.
- A4's constructor can write a bounded report to `/run` and independently to a
  uniquely identified, writable exFAT SD partition. The SD writer is
  no-clobbering, bounded, descriptor-relative, and does not remount media.
- B0.2 creates a process-specific 72-byte shared file under `/run`. Its reader
  uses bounded repeated reads, requires snapshot v2 and exact length, validates
  counter consistency, and checks PID, exact `/usr/bin/MPC Sample` executable
  identity, and Linux process start time. It does not use `ptrace`, process
  memory access, signals, listeners, or another firmware hook.
- The B0.2 reader is host-side only. No device reader is packaged, and no
  invocation or transfer mechanism is implemented.
- The pinned SDK's documented physical installation path is the same transient
  autorun/`LD_PRELOAD` restart path. Its SSH and panel HTTP instructions are for
  an emulator/virtual lab and are not evidence of services on the physical MPC.

### Confirmed by physical A2--A4 evidence

- On one MPC Sample running firmware 1.3.0.12, A2 proved constructor execution,
  bounded direct SD reporting, normal observed stock UI/pads/audio, and normal
  card-free recovery.
- A3 observed multiple preload process contexts and a PID/PPID chain. This
  confirms that the preload can reach several processes; it does not reveal the
  complete systemd unit or establish a surviving autorun shell.
- A4 reached exact `/usr/bin/MPC Sample`, matched all 63 firmware words, wrote
  its zero-hook report, left observed controls/audio normal, and recovered on a
  stock-only reboot.
- No A2--A4 observation tested a post-interaction command, access to `/run` from
  a computer, USB networking, a network service, or a stock diagnostic export.

### Plausible but unverified capabilities

- A pre-existing vendor USB protocol might expose a bounded diagnostic or
  command operation.
- A stock network feature might include a supported file or diagnostic
  operation without exposing a general shell.
- A stock UI diagnostic/export action might copy approved diagnostic data to
  removable media.
- An autorun process might, on the actual unit, be independent enough to remain
  alive across the service restart or receive a later stock-driven action.
- The B0.2 reader might run with sufficient permission in some already
  available command context.

None of these possibilities is supported by current evidence and none is a B1
recommendation.

### Unsupported assumptions

- USB attachment exposes the root filesystem or `/run`.
- USB mass-storage mode, if present, exports anything other than explicitly
  selected storage.
- A USB MIDI, audio, control-surface, update, or other vendor protocol can run
  commands or read arbitrary files.
- SSH, Telnet, ADB, SFTP, serial shell, or another network service exists or is
  enabled on the physical device.
- Root or ordinary Linux shell access is available merely because the autorun
  script appears privileged.
- The autorun shell survives `systemctl restart ac50`, or commands appended
  after the restart would execute safely.
- A stock project/sample/export workflow can select `/run` or arbitrary source
  files.
- Another restart can retrieve the counters. The snapshot is process-bound;
  replacing the stock process destroys the required live-PID evidence.

## Access-mechanism comparison

The ordering below is both the requested investigation priority and the current
feasibility/safety ranking. “Unknown” is not treated as a capability.

| Rank | Candidate | Supporting evidence | Required privilege | Survives service restart | Available after interaction | Preserves stock PID | Can read `/run` | Bounded transfer | Risks and missing evidence | Assessment |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | Existing USB connectivity | Physical product connectivity is plausible, but reviewed evidence identifies no host-facing file or command interface for diagnostics. The internal control-surface protocol and emulator interfaces are not such evidence. | Unknown; vendor protocol or mounted-volume access | Unknown | Unknown | Unknown | Not established | Not established | Need computer-side interface enumeration and official/device UI evidence of each interface's purpose. Mass storage must not be equated with root access. | Safest discovery target; currently unavailable |
| 2 | Existing stock network connectivity | No reviewed source or physical observation identifies a supported command/file interface on the physical MPC. SDK SSH/HTTP references are emulator-only. | Unknown | Unknown | Unknown | Unknown | Not established | Not established | Need stock UI/manual evidence that networking exists, followed only by passive computer-side observation. Do not enable services or scan beyond an operator-confirmed device address without new authorization. | No candidate established |
| 3 | Existing autorun lifecycle | Source and A2--A4 prove startup command execution and one restart. Constructor reporting proves SD writes from loaded processes. | Autorun can perform privileged operations; exact identity is not recorded | The transient files survive for the boot, but shell survival is unproven | No later trigger is known | Autorun's required restart does not preserve the pre-restart PID; a second restart would destroy B1 evidence | Autorun can write `/run` before/during startup; later access is unproven | Startup constructor output is bounded; post-interaction output is absent | App-owned shell may die during restart; adding a worker, loop, service, signal path, or callback I/O would expand scope and risk. | Proven startup path, unsuitable for retrieval |
| 4 | Existing stock diagnostic/export operation | A4 proves NEUROGENIC constructor-to-SD publication, not a stock diagnostic export. No reviewed stock operation accepts arbitrary `/run` input. | Unknown stock application authority | Unknown | Unknown | Unknown | Not established | Not established | Need a documented stock UI operation and evidence of its exact source/destination and process effect. Projects, samples, settings, and firmware must remain untouched. | Potentially safe, no evidence of existence |
| 5 | External B0.2 snapshot reader | Reader behavior is host-tested and implements the required identity, version, stability, and consistency checks. | Read access to the mode-0600 snapshot and relevant `/proc/<pid>` entries; likely the creating user or equivalent | The reader need not survive a restart, but its target cannot: a restart invalidates the observation | Yes, if a separate invocation channel exists | Yes, if invoked without restart | Yes, narrowly | Yes; formatted report is capped at 384 bytes | Invocation context, on-device ABI/runtime, privileges, and output transfer are all unconfirmed. Cross-building or packaging it is outside P1. | Best reader, missing transport/execution path |

USB ranks first only as a read-only discovery subject, not because USB access is
known. The external reader ranks last as a complete mechanism because it cannot
bootstrap its own execution, even though it is the preferred data validator
once a separate command path exists.

## Minimum missing device facts

Any viable mechanism must establish all of these facts on the target firmware:

1. **Interface identity:** What already-supported physical or stock UI interface
   is present, and what does the vendor intend it to expose?
2. **Operation:** Can that interface execute one read-only command or retrieve
   one explicitly selected bounded diagnostic file after encoder interaction?
3. **Process effect:** Does using it leave the same `/usr/bin/MPC Sample` PID and
   process-start token alive from interaction through snapshot validation?
4. **Read authority:** Can its existing principal open the mode-0600 snapshot
   and read `/proc/<pid>/exe` plus `/proc/<pid>/stat` without changing
   permissions or configuration?
5. **Namespace:** Can it reach the device's live `/run`, rather than an exported
   SD volume, host-side proxy, container, or different mount namespace?
6. **Bounded egress:** Can it return only the formatted report (maximum 384
   bytes), without exposing arbitrary firmware, memory, projects, samples,
   identifiers, or logs?
7. **Lifecycle:** Is it available after DATA interaction without a service
   restart, application exit, project load/save, or another persistent worker?
8. **Support and reversibility:** Is it already enabled and documented or
   visibly stock-supported, with no new daemon, credential, listener,
   persistent setting, or device-disk write?

Until one candidate satisfies every item, the reader's technical readiness does
not make B1 observable.

## Read-only discovery plan

These checks discover interfaces only. They do not install, invoke, or package
the B1 hook or reader.

### Computer-only checks requiring no MPC changes

1. **Review official host documentation and installed vendor software.**
   Purpose: identify vendor-supported Sample connectivity, storage, backup,
   update, or diagnostic workflows. Expected evidence: a versioned official
   description naming the interface and its accessible objects. Stop if the
   workflow requests firmware update, recovery mode, device configuration, or
   credentials. State change: none when limited to documentation/software
   inspection. Stock interruption: none.
2. **Inventory computer interfaces during an ordinary stock connection.**
   Purpose: record only OS-reported USB device class, interface descriptors,
   mounted volume names/filesystems, and whether a network interface appears.
   Expected evidence: class/interface descriptors sufficient to distinguish
   storage, MIDI/audio, networking, and vendor-specific interfaces; no device
   identifier should be retained in the repository. Stop if the computer asks
   to initialize, repair, format, update, pair, or install device firmware.
   State change: normal host enumeration only; do not mount read-write merely
   for this check. Stock interruption: none expected; stop on any UI/audio
   interruption or device restart.
3. **Observe only already-mounted supported storage.** Purpose: determine
   whether the stock connection exports removable media and whether that view
   is limited to user storage. Expected evidence: mount source/type and a
   top-level directory inventory, with names redacted as needed. Absence of
   `/run` is expected and does not justify probing hidden paths. Stop before any
   write, filesystem repair, project/sample open, or raw-device access. State
   change: none; use read-only observation. Stock interruption: none expected.

These checks may establish that an interface exists; they cannot by themselves
prove permission to read the device root filesystem or execute a command.

### Stock-device observations using existing documented interfaces

1. **Record visible connectivity modes and settings without changing them.**
   Purpose: learn whether stock firmware 1.3.0.12 presents a documented USB
   storage, computer, network, support, or diagnostic mode. Expected evidence:
   exact user-visible labels and help text, without unique identifiers. Stop if
   entry would disconnect audio/control, restart the application, change a
   persistent setting, or enter update/recovery mode. State change: menu
   navigation only. Stock interruption: none intended.
2. **Observe normal supported computer connection behavior.** Purpose: correlate
   the device's visible mode with the computer-only enumeration. Expected
   evidence: whether the stock UI remains responsive and whether it announces
   storage handoff, application restart, or exclusive computer control. Stop on
   restart, unrequested write, missing UI/audio, or a request to save/close a
   project. State change: ordinary cable connection only. Stock interruption:
   not expected; any observed interruption disqualifies the mode for B1 pending
   separate review.
3. **Inspect documented stock support/export menus without executing an export.**
   Purpose: determine whether a bounded diagnostic export exists and whether
   its source/destination are stated. Expected evidence: visible operation name,
   scope, destination, and warnings. Stop before confirmation, project/sample
   mutation, settings export, firmware operation, or restart. State change:
   navigation only. Stock interruption: none intended.

These observations must use an ordinary stock-only boot. They must not use an
autorun card or attempt access to `/run`.

### Checks requiring new authorization

1. **Execute a confirmed stock diagnostic export.** Purpose: verify its exact
   source scope, output bound, and PID effect. Expected evidence: documented
   output plus before/after stock PID and start-token continuity from an already
   authorized observation method. Stop on broader data collection, project or
   settings writes, restart, or undocumented prompts. State change: creates the
   documented export on supported removable storage. Stock interruption:
   prohibited. This requires review of data content and destination first.
2. **Exercise a confirmed vendor read-only command/file operation.** Purpose:
   prove it can read a harmless transient test object and preserve the stock
   process. Expected evidence: bounded content and unchanged PID/start token.
   Stop on authentication changes, shell exposure, arbitrary-root browsing,
   service enablement, device write, or restart. State change: none intended.
   Stock interruption: prohibited. Exact commands cannot be specified until the
   interface and vendor contract are evidenced.
3. **Run a separately built on-device B0.2 reader.** Purpose: validate the B1
   snapshot after interaction. Expected evidence: the bounded formatted report
   with all current identity and consistency checks. Stop on reader ABI or
   privilege mismatch, unstable counters, wrong identity, service restart, or
   any output beyond the allowlist. State change: transient execution and
   bounded output only. Stock interruption: prohibited. This is later B1 work,
   not authorized by P1, and requires package/ELF/runtime review.

No speculative root-shell command is part of this plan. Port scanning,
credential guessing, service activation, debug-mode entry, raw USB fuzzing, and
vendor-protocol mutation are excluded.

## Safety constraints

- Do not enable SSH, Telnet, ADB, SFTP, a serial shell, or any additional
  listener or network service.
- Do not add a resident helper, daemon, timer, polling loop, background worker,
  signal handler, callback I/O, or persistent configuration.
- Do not append a retrieval operation to the current autorun based on an
  assumption that its shell survives the restart.
- Do not use a second `ac50` restart for retrieval; the same PID/start token is
  part of the evidence.
- Do not broaden USB storage or a vendor protocol into assumed root access.
- Do not modify projects, samples, settings, firmware, partitions, the stock
  executable, or stock export data.
- Preserve the v2 snapshot layout and its PID, executable, process-start-time,
  exact-size, version, stable-read, and counter-consistency checks.
- Transfer only the bounded formatted report. Do not retain device identifiers,
  raw logs, process memory, firmware bytes, or unrelated files.
- Continue to require both `callback_invocations > 0` and
  `successful_forwards > 0`; installation success alone is not forwarding
  evidence.

## Fallback assessment

1. **Defer B1 physical testing — recommended now.** This preserves the current
   safety and observability requirements and avoids a hook run whose successful
   forwarding cannot be demonstrated. The cost is schedule delay and no new
   physical DATA-hook evidence.
2. **Future bounded post-interaction diagnostic export.** A separately
   authorized design could invoke the existing validator and publish only its
   bounded output through a proven lifecycle point. This offers the clearest
   evidence, but any new trigger or device payload requires its own privilege,
   process-lifetime, output, package, and recovery review. It must not perform
   callback I/O or become resident.
3. **More intrusive diagnostic mechanism.** A new shell/service, listener,
   additional hook, process-memory reader, or persistent agent could make
   retrieval technically easier but materially increases attack surface,
   lifecycle uncertainty, and recovery risk. It requires a new architecture and
   safety decision and is not an implicit fallback for B1.

## Recommendation and go/no-go

**No-go:** do not package or physically execute B1.

The recommended next action is the computer-only and stock-UI discovery above,
starting with official interface documentation and passive USB enumeration on a
stock-only boot. If that finds a supported, already-enabled read-only mechanism,
record its exact contract and seek a separate authorization for one harmless,
bounded continuity check. If it does not, defer B1 physical testing or open a
new design review for a bounded post-interaction export. Do not weaken the
requirement to observe callback invocation and successful forwarding.

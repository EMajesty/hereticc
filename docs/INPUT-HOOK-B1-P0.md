# M0.3b-B1-P0 device access and test preparation

Status: host-only design and identity hardening. **The checklist in this file
is not authorized for execution.** No B1 package exists.

## Device-access feasibility

Repository evidence confirms only the A4 SD autorun contract. The stock
application launches the autorun shell; that shell verifies the executable,
copies a preload library into `/run`, creates a transient systemd drop-in, and
ends by restarting `ac50.service` once. The generated source neither describes
the service's full process chain nor proves that the shell survives the restart.
A4 physical evidence proves direct constructor-to-SD reporting, not interactive
device access.

| Option | Evidence and permissions | Process effect | Retrieval assessment |
|---|---|---|---|
| Existing USB/network access | No repository or reviewed SDK evidence exposes `/run`; SSH, ADB, SFTP, serial, root shell, and network service availability are unknown. | Unknown. | Not established; do not enable an undocumented interface. |
| Existing command execution | Autorun command execution is confirmed, apparently privileged, but occurs before and triggers the reviewed restart. No post-interaction command channel is confirmed. | Existing path restarts `ac50`; PID and counters are lost on another restart. | Unsuitable without new device evidence. |
| Read-only host transfer | No confirmed USB filesystem or protocol maps the device's tmpfs to a host. | Unknown. | Preferred in principle, unavailable in current evidence. |
| External snapshot helper | Host implementation can validate the fixed file and live process without `ptrace`; execution context and permissions on-device are unknown. | None if independently invoked; mechanism to invoke it is missing. | Best technical reader, but not yet a device path. |
| Passive SD copy | No already-running, reviewed component is known to copy a file on request after interaction. The autorun shell cannot be assumed alive. | A new worker/daemon would add resident behavior. | Not established; no worker, polling loop, signal, callback I/O, or daemon is proposed. |

The recommended retrieval mechanism remains a read-only external invocation of
the B0.2 snapshot reader while the same stock PID is alive, followed by copying
only its bounded formatted output to the verified SD results directory. This is
a design preference, not a confirmed device capability.

## Snapshot identity and consistency

B1-P0 versions the shared layout from v1/64 bytes to v2/72 bytes. The eight new
bytes hold Linux `/proc/<pid>/stat` field 22: process start time in clock ticks
since boot. Creation reads `/proc/self/stat`; the reader requires all of:

- valid v2 magic, version, exact length, and counter invariants;
- recorded PID still present;
- `/proc/<pid>/exe` exactly `/usr/bin/MPC Sample`; and
- the live process start token exactly matching the recorded token.

The file is now `/run/hereticc-m03b-b1p0-counters-<pid>.bin`. Old layouts,
truncated metadata, stale files after PID reuse, unexpected executables, and a
process disappearing during validation fail closed. The parser uses the final
`) ` delimiter so spaces and parentheses in the process name do not shift the
field index. The filename alone is never trusted.

Two identical consecutive reads, with eight bounded attempts, are adequate for
this diagnostic only when the operator stops turning the encoder before the
snapshot. A read may otherwise observe a legitimate intermediate callback
state: invocation and delta-class increments precede the original call, while
successful-forward increments follow its normal return. Invariants reject torn
or impossible totals but do not create a multi-counter atomic transaction.
That limitation is acceptable for proving that at least one callback and
successful forward occurred. A stronger future protocol could add an even/odd
sequence counter around updates, but it would add two callback atomics and is
not necessary for this bounded experiment.

## Reversible B1 package design

This section specifies a future artifact; it does not create one.

1. Pin SDK commit `e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39`, apply the reviewed patch series, build AArch64 with the locked dependency set, inspect the ELF, and publish SHA-256 hashes for every package member.
2. Reuse the reviewed A4 autorun generator and exact 1.3.0.12 executable SHA-256 guard. Require exact `/usr/bin/MPC Sample` identity and all 63 firmware words before preparing anything.
3. Allow only `DATA_HOOK`; reject paint, audio, ALSA, MIDI, note, surface, button, CC, lighting, UI, sequencer, and sample paths through build features and ELF inspection.
4. Package only the guarded autorun metadata/script, one identified preload library, checksums, and—only if invocation is separately established—one non-resident snapshot reader. No service, network listener, installer, or persistent configuration belongs in the package.
5. Use one controlled `ac50.service` restart to load the library. Never restart merely to retrieve counters. The constructor initial report distinguishes gate/preparation/installation status. `installation_succeeded=true` proves only branch publication; a later snapshot with `callback_invocations>0` and `successful_forwards>0` is required for forwarding evidence.
6. If the shared file cannot be created, retain local counters, report `counter_snapshot_ready=false`, and do not claim forwarding observability. If the reader cannot access or validate it, report retrieval failure without changing the live process.
7. If publication is indeterminate, keep the trampoline and library mapped, perform no interaction, and use a controlled process restart or stock-only reboot. Do not unload or retry in-process. A transient drop-in reduces persistence but does not eliminate patching, restart, cache, ABI, or rollback risk.
8. Write only bounded reports to `/run` and the already-validated unique writable exFAT SD result directory. Preserve projects, samples, settings, firmware, partitions, and the stock executable.
9. Normal restoration is shutdown, removal of the test SD card, and stock-only boot. Verify the transient `/run` drop-in is gone by observed stock behavior; do not modify persistent system configuration.

Expected future report namespaces should be distinct and no-clobbering:
`m03b-b1-init-00..01.txt` for constructor status and
`m03b-b1-events-00..01.txt` for a validated post-interaction snapshot. Exact
names and package contents require review before implementation.

## Physical-test checklist — NOT AUTHORIZED FOR EXECUTION

- [ ] Use stable power; prepare a spare test SD card with no other autorun package.
- [ ] Back up projects and samples through supported computer-side workflows.
- [ ] Confirm firmware 1.3.0.12 and record the reviewed package SHA-256 values.
- [ ] Boot stock without the test card; baseline display, audio, pads, DATA turn/push, knobs, fader, buttons, transport, sample loading, and project load/save.
- [ ] Inspect the package allowlist, autorun script, transient paths, ELF report, and recovery instructions before copying anything.
- [ ] Insert the approved card once and observe no more than one controlled stock-service restart. Stop on a loop, missing UI/audio, or unexpected storage activity.
- [ ] Read the constructor report. Stop unless exact executable, 63/63 fingerprints, DATA-only preparation, installation success, forwarding readiness, and snapshot readiness are reported.
- [ ] Turn only the DATA encoder through a small bounded sequence in both directions, then stop interaction before reading counters. Do not test pads, transport, other controls, projects, or audio features beyond stock regression observation.
- [ ] Invoke the approved read-only retrieval path without restarting `ac50`; verify PID, exact executable, start token, format version, and counter invariants. This step currently has no established device mechanism.
- [ ] Require invocations and successful forwards to increase consistently with stock encoder behavior. Installation success with zero invocations is an observation failure; invocations without successful forwards is a forwarding failure and immediate stop condition.
- [ ] Shut down normally, remove the test SD, boot stock-only, and repeat the full baseline. Use stock-only reboot as the recovery boundary for any uncertainty.
- [ ] Collect only bounded constructor/event reports, package checksums, observed stock behavior, and recovery outcome. Do not collect memory, firmware bytes, identifiers, projects, or samples.

## Go/no-go

No-go for B1 hardware authorization today. The source and identity protocol are
ready for planning, but at least one actual device fact is missing: a supported,
non-restarting way to invoke a read-only helper (or expose `/run`) with permission
to validate the live stock process and transfer only the bounded result. A
separate, read-only access-discovery task should establish that fact without
enabling undocumented services before a hook package is authorized.

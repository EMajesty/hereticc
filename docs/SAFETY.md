# Safety

HERETICC is unofficial research. No reviewed public project establishes a verified recovery path for every device boot failure. Reversible deployment reduces risk; it does not make device modification risk-free.

## Repository safety

Never commit:

- Akai/inMusic firmware binaries, updater images, extracted firmware code, root filesystems, partition images, or factory content;
- control-surface challenge keys, signing/encryption keys, serial numbers, MAC addresses, or other device identifiers;
- copyrighted firmware fonts, artwork, samples, or other assets;
- logs, captures, hashes, or metadata that have not been reviewed for device/user information.

Keep private research inputs outside the repository. Record only the minimum non-copyrightable facts and independently authored interfaces required for compatibility. Do not copy unlicensed code; in particular, the reviewed `mpc-sample-lab` snapshot has no reuse license.

## Development rules

- Prefer host tests and an emulator before physical-device work, but never label those as hardware tests.
- Treat every firmware pointer, address, object layout, and thread-affinity assumption as version-specific and unsafe.
- Verify the complete target firmware fingerprint before installing any in-memory hook; fail closed on mismatch.
- Derive the expected fingerprint count from the pinned SDK table. At SDK v0.6.0 commit `e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39`, the implementation checks 63 instruction words despite older documentation saying 59.
- Before any hook publication, require every member of the explicitly requested hook group to validate and prepare. A partial group is failure, not degraded success.
- Treat the SDK's Sample-page paint-vtable rewrite as a separate hook outside its 22-branch transaction. It must be explicitly authorized, preflighted, checked, and recoverable; never install it implicitly for a compatibility probe.
- After the first stock-page permission change or instruction/data write, a failed rollback is an indeterminate process state. Keep all possibly referenced code mapped and restart the stock service/process; do not unload the extension or claim stock is unmodified.
- Bound all queues and buffers. Audio callbacks must not allocate, block, perform file I/O, draw, log synchronously, or take contended locks.
- Separate UI/project mutation from real-time scheduling with validated snapshots and explicit ownership.
- Restore intercepted controls, repaint state, audio hooks, and native pad lighting on every normal/error exit path.
- Do not attempt live hook removal without a proven callback-quiescence protocol. The pinned SDK has none; process-lifetime hooks and controlled process restart are safer than `dlclose` or trampoline unmapping.
- Never experiment with undocumented control-surface service/update commands.

## Backup before any future device test

Device testing is outside the current task. Before a future approved test:

1. record the exact hardware and firmware version without publishing unique identifiers;
2. save the current project and shut down normally;
3. use a spare, verified SD card and keep a computer-side copy of its contents;
4. back up projects and samples through normal user-visible storage paths;
5. ensure the SD card contains no conflicting `autorun` package;
6. prepare a stock boot card/no-card procedure and a time-bounded test plan;
7. verify package hashes and inspect every script/binary to be run;
8. keep power stable and do not remove media during writes or startup.

Do not treat a raw partition backup as a proven recovery method. This project will not implement firmware flashing, partition modification, control-surface firmware updates, bootloader changes, or restore tooling.

## Deployment constraints

The only acceptable initial deployment model is SD-card based and reversible:

- copy an explicitly reviewed package to SD;
- execute from transient storage such as `/run` where required;
- make no persistent device-disk or partition changes;
- verify the stock application before any hook or patch;
- on mismatch or partial failure, leave/restart stock unmodified;
- removal of the package/card plus reboot must return to stock.

An SD autorun may execute as root and can still damage the device if wrong. “Runs from SD” is not itself a safety guarantee.

The M0.3b-A package's `autorun.json` sets `askQuestion` to false. Treat copying
its `autorun` directory to a card and inserting/booting that card as physical
execution, not package inspection. The script performs one guarded transient
`ac50` restart; repeated restarts are a stop condition. Inspect the extracted
script and checksums before any separately approved run.

M0.3b-A2 keeps post-restart autorun work prohibited. Instead, the probe
constructor attempts one bounded write under `hereticc-results` only after
matching `/dev/mmcblk1p1`, exFAT, `rw`, a safe direct `/media` child, and exFAT
filesystem magic. It uses exclusive partial creation, no-replace finalization,
and file/directory sync. Missing or partial output is failure. This is host-tested
source behavior, not physical-device evidence.

The owner-run A4 test subsequently verified the exact stock executable and all
63 firmware words on one firmware 1.3.0.12 unit; controls, audio, and stock-only
reboot remained normal. This validates the zero-hook probe only. B0 introduces
the first stock-text mutation path but is host-only: it emits no SD package and
must not be deployed. Any later B1 authorization must explicitly accept the
temporary RWX text-page interval, instruction-cache and cross-core uncertainty,
non-atomic rollback, inferred DATA ABI, and process-lifetime trampoline policy.

B0.1 adds only saturating aggregate counters and constructor-time reporting.
The callback performs no I/O. The guarded SD writer has isolated
`m03b-b01-init-*` slots and runs only for exact expected-process identity.
Post-interaction counter retrieval remains unimplemented because no reviewed
safe lifecycle point exists; shutdown destructors, workers, signals, callback
I/O, and another hook are not accepted as implicit fallbacks.

## Data integrity

- HERETICC project data should initially be an SD sidecar, not an undocumented modification of stock project files.
- Write to a new temporary file, validate it, flush it, then rename over the destination where supported.
- Keep a previous known-good generation and include format version/checksum information.
- Never overwrite source audio for slicing, offsets, reverse, or rendering without an explicit export action.
- Detect missing/remapped samples and ask for resolution; do not guess by firmware pointer or pad position.
- Treat project bytes as untrusted. Apply encoded-size, collection, identifier, event-total, and decoded-allocation limits before allocating; reject malformed/truncated data without partial project state or panics.
- Keep decode policy limits reviewable and configurable for host tools. A raised limit requires memory-impact review and does not make stock project formats supported.

## Test evidence

Every test report must state environment: host unit test, emulator, or physical device. Physical claims include firmware version, build revision, procedure, and observed result. A test on one unit/firmware does not establish general compatibility.

Stop testing and return to stock if there are boot loops, persistent crashes, unexpected writes, corrupted projects, audio instability, overheating, loss of controls, or inability to restore the stock service. Preserve logs only after redacting identifiers.

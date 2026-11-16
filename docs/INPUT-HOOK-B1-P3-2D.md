# M0.3b-B1-P3.2d pre-restart collector implementation

Status: host-tested library model and inert review tooling. P3.2e subsequently
adds the Linux acquisition boundary and replaces the hard-link publication
model; see [P3.2e](INPUT-HOOK-B1-P3-2E.md). No deployable package exists and no
MPC was contacted.

## Outcome and language choice

The collector core is `neurogenic-b1-collector-model`, a dependency-free Rust
library. Rust was selected over a POSIX shell implementation because the
selected-fact parser, report bounds, identity cross-checks, deadline state, and
no-clobber publication can be typed and fixture-tested without shell expansion
or a chain of unverified utilities. The repository already pins Rust and its
cross-build environment. The crate has no binary target, so this milestone does
not establish device executability.

A shell collector would have a smaller compiled-artifact footprint, but would
spawn several utilities, inherit their parsing/localization behavior, require a
separate timeout facility for each command, and enlarge quoting and output-bound
risk. It was not implemented.

## Architecture

```text
explicit bounded byte sources (host fixtures in P3.2d)
                         |
                         v
service/process/run/network/socket normalizers
                         |
                         v
PID + executable + start-token + cgroup consistency
                         |
                         v
ordered ASCII selected-fact report (<=4096 bytes)
                         |
                         v
validated exact mount -> create-new fixed final -> sync
```

The library contains no `main`, process spawning, shell, filesystem traversal,
network API, service API, firmware access, hook/preload logic, unsafe code, or
external dependency. Device-specific acquisition remains outside the core.

## Implemented observation adapters

`ObservationInputs` accepts explicitly supplied, individually bounded byte
records. The parsers support only:

- selected service properties: systemd version, `Type`, `KillMode`, `Restart`,
  `MainPID`, and `ControlGroup`;
- exact executable text, two bounded `/proc/<MainPID>/stat` observations,
  selected UID/GID status lines, and one cgroup record;
- a selected `/run` mount row, `RUN_MODE`, and runtime-unit-directory mode;
- normalized expected-address, expected-route, and explicit link-state lines;
- normalized TCP listener rows, retaining only exact USB-address and wildcard
  ports.

The service MainPID must be nonzero. Only that PID is accepted. Exact
`/usr/bin/MPC Sample`, PID, start token, and service/process cgroup must agree.
The final stat observation must match the initial observation. Contradictions
fail closed; absence of optional run/network/socket evidence yields a valid
`partial` report.

These are parsing adapters, not device acquisition adapters. Availability and
exact output formats of `systemctl show`, `systemd --version`, `ip`, `ss`, and
any timeout wrapper remain unverified. A future adapter should prefer direct
bounded virtual-file reads for the known PID and isolate every necessary
utility. It must not substitute process enumeration or broader commands.

## Bounds and deadlines

- Each observation source is limited to 16 KiB.
- Reports contain at most 32 fields and 128 ASCII bytes per value.
- Total report size is at most 4096 bytes.
- Fields are emitted from a `BTreeMap`, giving deterministic order.
- The clock and one absolute monotonic deadline are injected. Expiry is
  inclusive (`now >= deadline`) and checked before collection, after service
  parsing, after process validation, after every optional adapter, and after
  formatting.
- No retries, waits, threads, child processes, or asynchronous work exist.

Because P3.2d has no blocking device I/O adapter, its deadline is enforceable
for every implemented operation. A future adapter must enforce the same
absolute deadline inside each blocking read or child operation. The library
cannot interrupt a future adapter that violates that contract; this is a
physical-package gate, not a claimed property.

Critical errors return a typed failure. `format_failure_report()` can produce a
fixed bounded `collection_status=failed` envelope without raw error detail.

## Publication semantics

`ValidatedDestination` accepts an exact existing directory only when separately
supplied mount evidence says:

- source is exactly `/dev/mmcblk1p1`;
- filesystem is exactly `exfat`;
- the mount is read/write; and
- the evidence path exactly matches the requested directory.

The final path is fixed as `b1-p3-2c-report.txt`; callers cannot supply a file
name. P3.2e removes the original partial-file/hard-link model because exFAT does
not support hard links. The revised model exclusively creates the fixed final
name, synchronizes it and the directory, never overwrites, and leaves any
interrupted incomplete final non-retryable. It does not claim atomic visibility;
see P3.2e for the remaining descriptor-relative path gate.

This host abstraction does not yet reproduce A4's `/proc/mounts` record count,
safe `/media/<name>` syntax, descriptor-relative traversal, or exFAT magic
check. Those checks are mandatory future adapter work before physical review.

## Report and analyzer classification

The report remains `neurogenic-b1-p3.2c-v1`. The offline analyzer now reports
three independent facts:

1. `schema_validity`: version, required fields, types, bounds, duplicates,
   unknown fields, and pre-restart semantics;
2. `collection_completeness`: the producer's `complete`, `partial`, or `failed`
   status, plus a boolean `baseline_complete`; and
3. `evidence_consistency`: PID, cgroup, USB interface/route, and phase/restart
   agreement.

A valid partial report retains selected facts but cannot satisfy a gate that
requires completed collection. A complete, valid, consistent collection can
populate systemd, process, `/run`, USB, and PID/start-token gates without legacy
category duplication. If legacy evidence is also supplied, confirmed
disagreements are listed in `cross_source_conflicts` and affected gates become
`conflicting`; the favorable value is never selected silently.

## Inert manifest and static inspection

`tools/b1_p3_2d_manifest.py` hashes only explicitly named, non-executable files
under one declared root and emits deterministic JSON to standard output. It
records firmware `1.3.0.12`, the established stock-prefix SHA-256, schema,
source/build provenance, safety declarations, and unverified assumptions. It
rejects executable files, duplicate/external paths, oversized inputs,
device-recognized autorun names, shared objects, ELF/images/binaries, ZIPs, and
firmware-update names. It never creates a package or directory tree.

Example host review only:

```sh
python3 tools/b1_p3_2d_manifest.py \
  --root . \
  --file crates/neurogenic-b1-collector-model/Cargo.toml \
  --file crates/neurogenic-b1-collector-model/src/lib.rs
```

`tools/b1_p3_2d_safety.py` checks crate structure and executable targets,
dependencies, process/network/unsafe APIs, traversal APIs, and prohibited
service, updater, preload, and firmware-operation text. Behavioral tests also
exercise the only exposed execution paths, so safety does not rest solely on a
substring scan. The current crate passes. The checker explicitly leaves future
device adapter availability, timeout enforcement, AArch64 artifacts, mount
identity, and runtime privilege behavior unverified.

## Failure-injection coverage

Host tests cover missing/zero service PID, wrong executable, changed PID/start
token, malformed stat/status, unavailable evidence, invalid UTF-8, oversized
sources/report fields, missing network, wrong prefix, contradictory route,
deadline expiry, deterministic complete/partial/failure output, mount mismatch,
symlink/non-directory destination, existing result/partial files, oversize
publication, and no-clobber preservation. Manifest and static-safety tests cover
determinism, redaction of absolute paths, executable/deployable artifacts,
duplicates, external/oversized inputs, dependencies, binary targets, process
spawning, network listeners, unsafe/process-memory APIs, broad traversal, and
service operations.

Permission denial and missing utilities are represented at the current adapter
boundary as explicit source-unavailable outcomes: critical process/service
sources fail, while optional sources make the report partial. P3.2e replaces
the original cleanup expectation: injected pre-sync failures leave a fixed,
non-retryable incomplete final file so the implementation never relies on
unsupported exFAT hard-link or rename-no-replace behavior. Actual device
write/sync behavior remains physically unverified.

## Future integration and authorization boundary

Before a physical diagnostic can be proposed, a separate milestone must:

1. implement exact, bounded AArch64 acquisition adapters and independently
   enforce the absolute deadline for every file/utility operation;
2. prove required utility/output availability or use narrower direct reads;
3. port the full A4 mount identity, safe path, `O_NOFOLLOW`, and exFAT magic
   checks without importing preload/restart behavior;
4. add a reviewed fixed invocation wrapper and firmware-prefix guard;
5. cross-build and inspect the binary, its imports, manifest, and inert package
   inventory; and
6. separately authorize creation of an autorun package and then one physical
   zero-hook run.

No device-recognized autorun entry point should exist before those gates pass.
P3.2d does not authorize packaging, deployment, service restart, P3.2b, relay
deployment, or DATA-hook testing.

**B1 physical DATA-hook testing remains no-go.**

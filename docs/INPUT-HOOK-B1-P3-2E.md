# M0.3b-B1-P3.2e collector hardening and Linux acquisition adapter

Status: host-tested source libraries and AArch64 relocatable objects only. No
executable collector, autorun entry point, SD package, device interaction,
service change, preload, hook, or listener was produced.

P3.2f subsequently adds the fixed integration executable, descriptor-relative
publication, firmware guard, process-group containment, and linked ELF review;
see [P3.2f](INPUT-HOOK-B1-P3-2F.md). This P3.2e text remains the historical
record of the earlier blockers.

## Outcome

P3.2e adds a separate `neurogenic-b1-collector-linux` acquisition boundary,
hardens network normalization, and replaces the exFAT-incompatible hard-link
publication model. The result is useful for host review, but is **not physically
ready**. Three safety-critical guarantees remain incomplete: descriptor-relative
SD publication and live filesystem-magic acquisition, interruption of a blocked
direct read, and descendant-proof termination of external utilities.

## Components

```text
fixed read-only commands + exact virtual-file paths
                         |
                         v
bounded Linux acquisition adapter
                         |
                         v
selected-fact collector and consistency checks
                         |
                         v
bounded deterministic report (<=4096 bytes)
                         |
                         v
validated SD identity -> exclusive fixed-name write -> sync
```

The collector model remains independent of Linux acquisition. The Linux crate
has no binary target and cannot be invoked by the MPC. It has no socket API,
filesystem traversal, shell, service mutation, process enumeration, signal or
debug operation, firmware operation, preload, or hook code.

## Linux acquisition boundary

Only these absolute commands and fixed arguments exist in source:

```text
/usr/bin/systemctl show ac50.service --no-pager --property=Type,KillMode,Restart,MainPID,ControlGroup
/usr/bin/systemd --version
/usr/sbin/ip -o -4 addr show
/usr/sbin/ip -4 route show table main
/usr/sbin/ip -o link show
/usr/bin/ss -H -ltn
```

The runner invokes no shell, clears the environment, uses `/` as its working
directory, closes stdin, and bounds captured stdout and stderr independently to
16 KiB. `MainPID` must be nonzero and comes only from the selected service
query. The adapter reads exactly `/proc/<MainPID>/{stat,status,cgroup,exe}` and
re-reads `stat` after optional acquisition. The collector core requires exact
`/usr/bin/MPC Sample`, matching PID/start token, credentials, and matching
service/process cgroup. It never searches for a substitute process. Executable
path is process identity, not firmware compatibility; the separately reviewed
firmware fingerprint remains an invocation gate.

Direct reads are limited to 16 KiB. Missing optional `/run`, network, or socket
evidence produces a partial report; missing or contradictory service/process
identity fails. Command availability and the absolute device paths are not
physically established.

## Network normalization

Address, route, and link output are acquired independently and normalized only
after all three complete. Only interfaces associated with the expected
`192.168.155.1` observation or exact `192.168.155.0/28` route are retained;
unrelated interfaces and addresses are discarded. The collector core then
requires exactly `192.168.155.1/28`, exactly `192.168.155.0/28`, the same
interface, and explicit `UP` link evidence. Identical duplicates are harmless;
contradictions fail closed; missing link evidence is partial. Tests permute the
input observations and cover duplicates, unrelated records, malformed input,
wrong prefixes, and contradictory routes.

## Deadline guarantees and limitations

One absolute monotonic deadline is passed through acquisition and collection.
Every direct operation checks it before and after, and command supervision
polls it while the selected child runs. A timed-out child is killed and reaped.
Output is drained concurrently so a full pipe does not deadlock the selected
child.

These are not yet hard device guarantees:

- standard-library file open/read/read-link/metadata calls cannot be interrupted
  once blocked; post-operation checking only rejects a late result;
- killing and reaping the selected child does not prove that an unexpected
  descendant holding a pipe or other resource is terminated; and
- the output-drain threads can therefore wait past the deadline if an
  unexpected descendant retains a stream.

The fixed read-only utilities are not expected to daemonize, but that is not a
sufficient physical safety proof. A future candidate must either isolate and
terminate the complete process group with a reviewed Linux mechanism or remove
the external-command source. Direct-read sources that lack a reliable blocking
bound must likewise be isolated or explicitly rejected from the physical
candidate.

## exFAT publication

Hard links were removed. Publication uses one fixed final name,
`b1-p3-2c-report.txt`, opened with exclusive creation. It never renames,
overwrites, appends, selects an alternate slot, follows a final symlink, or
retries. A pre-existing reserved `.partial` marker also blocks publication.
Mode `0600` is requested but is only effective where the filesystem/mount
supports Unix permission semantics; it is not claimed as an exFAT control.

The report is written once, the file is synchronized, and then the directory is
synchronized. This is conservative but not atomic publication: power loss or a
write/sync failure may leave an incomplete file at the final name. That file is
intentionally non-retryable, and the offline schema/consistency checks must
reject it. This avoids the unsupported assumptions that hard links work on
exFAT or that ordinary rename is no-replace.

Host validation stores the destination device/inode and rechecks it immediately
before opening the result. It requires separately supplied evidence for exactly
`/dev/mmcblk1p1`, writable `exfat`, a safe unique `/media/<component>` mount,
and exFAT magic `0x2011bab0`. However, the production adapter does not acquire
the live magic or use descriptor-relative open-at operations. A path-swap race
therefore remains and publication is not approved for device use.

## Static safety review

The safety checker permits process execution only when the fixed-command runner
markers are present and command fields are private. It rejects shells,
arbitrary public command construction, external dependencies, sockets,
unsafe/process-memory APIs, traversal, service restart/reload/install,
`LD_PRELOAD`, systemd-run, hooks, firmware operations, executable targets, and
background service behavior. Behavioral tests verify the precise command list
and that proc access is confined to the selected MainPID.

Static inspection cannot prove device command availability, descendant
behavior, blocking-syscall bounds, runtime privileges, mount identity, or ABI
compatibility. Those remain explicit gates.

## Cross-build and object inspection

The repository's pinned Rust 1.97 toolchain and installed
`aarch64-unknown-linux-gnu` standard library successfully cross-compiled both
libraries in release mode. The output is an `.rlib`, not a deployable program.
Extracted code-generation objects are ELF64 little-endian AArch64 relocatable
objects. They have no interpreter or program segments because they are not
linked executables. Consequently there is no meaningful dynamic-loader,
shared-library, segment, or final imported-symbol assessment yet. No executable
artifact was created, and ABI compatibility with firmware 1.3.0.12 remains
unproved.

## Physical readiness and next milestone

P3.2e is complete as a host-only hardening milestone. A separately reviewed
P3.2f should, without packaging or device access:

1. implement descriptor-relative, no-follow mount/result opening and live
   exFAT magic/device/inode revalidation without an unsafe path race;
2. resolve hard interruption and complete child-tree cleanup, or eliminate the
   affected acquisition sources;
3. prove or replace the exact fixed utility paths and output contracts;
4. add a non-autorun executable integration target only after those contracts
   pass review, then cross-link and inspect its loader, dependencies, symbols,
   segments, size, and reproducibility; and
5. retain the separate firmware fingerprint gate and inert manifest review.

Creation of a device-recognized autorun package and one zero-hook physical run
would still require later, explicit authorization. P3.2b, relay deployment, and
B1 DATA-hook testing remain unauthorized.

**Go/no-go: no-go for physical SD execution and no-go for B1 physical hook
testing.**

## Host validation record

Executed on 2026-11-16:

- `python3 -m unittest discover -s tests -v`: 36 passed.
- `cargo fmt --all -- --check`: passed.
- `cargo clippy --workspace --all-targets -- -D warnings`: passed.
- `cargo test --workspace`: 87 passed, 0 failed; documentation targets ran 0
  tests.
- `git diff --check`: passed.
- `device/m03b-a/scripts/check-patches.sh`: passed; reproducible patch diff
  SHA-256 `2be00914fc0f9838e1a1804021853d8cb8635f6ddd91ce272bf446cfa0c2b7bd`.
- `device/m03b-b0/scripts/check-patches.sh`: passed; patch series SHA-256
  `f21f7d6398d43209281b5945ce620eb1ef2639f80d4fbbafe9e8f998e5f03479`.
- pinned Rust 1.97 release cross-build for both libraries targeting
  `aarch64-unknown-linux-gnu`: passed. `file` and `readelf` confirmed ELF64
  little-endian AArch64 relocatable objects; no linked executable exists to
  inspect for interpreter, dependencies, or program segments.

The structural safety inspector passed both collector crates and explicitly
reports command availability, complete descendant/timeout behavior, invocation,
AArch64 executable contents, physical mount identity, and runtime privileges as
unverified. No device-facing source, SDK patch, preload ABI, snapshot format,
startup script, network listener, or deployable artifact changed.

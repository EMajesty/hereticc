# M0.3b-B1-P3.2f physical-readiness engineering

Status: host-complete, linked and statically inspected. At P3.2f completion no
autorun package existed. The later host-only P3.2g package is documented
separately; nothing was executed on an MPC and physical execution remains
unauthorized.

## Executive decision

The zero-hook collector is now a fixed-mode AArch64 executable candidate with a
firmware-prefix guard, bounded selected observations, process-group containment,
descriptor-relative exFAT publication, and one fixed output file. It contains no
preload, hook, service control, listener, daemon, persistent configuration, or
device-internal write path.

It is **not yet authorized for physical execution**. The executable loader and
fixed utility paths remain unverified on the device, the autorun entry point has
not been designed into or reviewed as a package, and ordinary Linux `/proc`,
`/run`, and exFAT syscalls are operationally bounded rather than forcibly
interruptible. These unknowns are best resolved by one separately authorized,
fail-inert zero-hook run after package review; another general host-only design
milestone would not resolve them.

## Final architecture

```text
stock SD autorun (future, separately reviewed)
                  |
                  v
4096-byte firmware prefix SHA-256 guard
                  |
                  v
one 10-second monotonic lifetime
                  |
                  v
fixed systemd/ip commands + exact MainPID /proc and /run reads
                  |
                  v
identity/consistency normalization -> report <=4096 bytes
                  |
                  v
retained exFAT directory fd -> exclusive fixed-name write -> fsync -> exit
```

`p3_2f_collector` has no command-line options, configurable paths, shell, or
generic command interface. It exits silently with failure when any critical
gate fails. It publishes only a successfully normalized report.

## Acquisition simplification

| Source | First-run status | Method |
|---|---|---|
| `ac50.service` properties and MainPID | essential | fixed `/usr/bin/systemctl show ac50.service --no-pager --property=Type,KillMode,Restart,MainPID,ControlGroup` |
| systemd version | essential | fixed `/usr/bin/systemd --version` |
| exact process identity/start token/credentials/cgroup | essential | only `/proc/<MainPID>/{exe,stat,status,cgroup}` |
| `/run` mount and modes | essential | bounded `/proc/mounts` plus metadata for `/run` and `/run/systemd/system` |
| USB address, route, link state | useful optional | fixed `/usr/sbin/ip` address/route/link queries |
| listening sockets | removed | no `ss`, socket enumeration, or listener evidence in the candidate |

Missing optional USB utilities produce a valid partial report. Malformed or
contradictory USB evidence fails. Deadline, containment, oversized output, and
malformed UTF-8 are terminal and cannot be downgraded to optional absence.

The executable path must be exactly `/usr/bin/MPC Sample`; it is never used as a
firmware-compatibility substitute. MainPID must be nonzero, and only that PID is
read. PID/start token and cgroup are rechecked for consistency. There is no
process search, fallback candidate, signal, attach, or memory access involving
the stock process.

## Deadline and child containment

One absolute ten-second monotonic deadline covers guard, mount selection,
acquisition, normalization, and publication. All stages receive the same
deadline. Expiry and supervision failures are terminal.

Each fixed utility receives an empty environment, `/` working directory, closed
stdin, separate 16 KiB stdout/stderr limits, and a new process group. Two drain
threads prevent pipe-capacity deadlock. Timeout or output overflow sends
`SIGKILL` to the negative child process-group ID, reaps the direct child, and
joins both drains. The group is also killed after normal direct-child exit so a
same-group descendant cannot retain a pipe. Host tests deliberately create such
a descendant and verify bounded completion.

This contains descendants that remain in the created group. A malicious child
could deliberately create a new session/group; the fixed, read-only utilities
are not expected to do so, but their actual device implementations are unknown.
No claim of adversarial sandboxing is made.

Direct `open`, `read`, `read_link`, metadata, `fstatfs`, report write, and
`fsync` calls check the deadline before and after their containing stage but
cannot be asynchronously interrupted by safe Rust. Exact `/proc` and `/run`
reads are small kernel virtual files and are operationally bounded. Removable
exFAT I/O can stall on media failure. This is a Linux/filesystem failure model,
not a mathematically hard deadline; no persistent watchdog was added.

## Firmware identity guard

Before acquisition, the executable reads exactly the first 4096 bytes of
`/usr/bin/MPC Sample`, computes SHA-256 locally, and requires
`44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f`,
the established firmware 1.3.0.12 prefix. Short reads, mismatch, I/O error, and
deadline expiry fail inertly before report publication. The SHA-256
implementation is host-tested against the standard `abc` vector.

The future autorun wrapper must independently retain the same exact firmware
and executable gate. Path identity alone remains insufficient.

## Descriptor-relative SD publication

The isolated `sd` module is the only allowed unsafe/`libc` boundary. It:

1. parses at most 16 KiB of mount data;
2. requires one and only one `/dev/mmcblk1p1` record;
3. requires `exfat`, `rw`, not `ro`, and `/media/<safe-component>`;
4. opens the mount with `O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`;
5. requires live `fstatfs` magic `0x2011bab0`;
6. retains the directory descriptor and its device/inode;
7. immediately before publication, reparses mounts, reopens the named mount,
   and requires matching device/inode and filesystem magic;
8. creates only `b1-p3-2c-report.txt` relative to the retained descriptor using
   `O_CREAT|O_EXCL|O_NOFOLLOW`;
9. writes at most 4096 bytes, syncs the file, and syncs the directory.

There is no overwrite, append, alternate slot, path traversal, rename, or
automatic retry. Unix mode 0600 is requested but is not claimed as an exFAT
access-control guarantee. Publication is intentionally non-atomic: interruption
may leave a truncated final report. The fixed name then blocks all retries, and
the offline analyzer rejects an incomplete schema.

Host tests cover unique mount selection, read-only/wrong/ambiguous mounts,
symlink rejection, path replacement, existing output, exclusive creation, and
injected file/directory-sync-stage interruption. Short-write, write-zero,
filesystem-full, oversize, deterministic output, and incomplete-report behavior
remain covered by the collector model and offline analyzer tests.

The only new dependency is the already established Rust `libc` crate, resolved
exactly to 0.2.191 in `Cargo.lock` (dual MIT/Apache-2.0). It supplies reviewed
Linux ABI declarations only; all unsafe calls remain in `sd.rs`.

## Linked AArch64 ELF inspection

The pinned Rust 1.97 toolchain linked the fixed executable through Zig 0.17.1
for `aarch64-linux-gnu.2.17`. Two identical release builds produced:

```text
SHA-256: 58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c
Size:    540528 bytes
Type:    ELF64 AArch64 PIE (ET_DYN), stripped
Entry:   0x30490
Loader:  /lib/ld-linux-aarch64.so.1
Needed:  libc.so.6, libpthread.so.0, libdl.so.2
Max required symbol version: GLIBC_2.17
```

LOAD segments are R, R+E, or RW; none is writable and executable. GNU stack is
RW and non-executable; GNU RELRO and immediate binding are enabled. Imports
include the expected process-spawn, pipe/thread, `kill`, `openat`, `fstatfs`,
and `fsync` runtime paths. No `bind`, `listen`, `accept`, `connect`, `ptrace`,
`process_vm_writev`, or `pwrite` import exists. `mprotect`, `socketpair`, and
`execvp` are Rust standard-library runtime/process-spawn imports; source and
behavioral inspection establishes no network listener or executable-memory
operation in the collector path.

A4 physically loaded an AArch64 Rust artifact requiring the same three shared
libraries and symbols through GLIBC 2.30, which supports the libc-version
inference. It does not prove that the executable loader exists at the linked
path or that a standalone PIE starts in the autorun environment.

## Physical-readiness matrix

| Category | Rating | Basis / gate |
|---|---|---|
| Firmware identity guard | `confirmed-host` | Exact 4096-byte SHA-256 implementation and mismatch tests; established fingerprint, not rerun physically here |
| Autorun invocation assumptions | `unknown` | Stock autorun is physically established, but this fixed executable has no reviewed device-recognized wrapper/package |
| AArch64 ABI compatibility | `inferred` | AArch64 PIE, GLIBC 2.17 maximum; A4 proves related Rust/glibc runtime, not standalone loader path |
| Essential command availability | `unknown` | Exact `systemctl`, `systemd`, and `ip` paths/outputs have not been observed on-device |
| Essential process evidence access | `unknown` | Exact access is host-tested; autorun UID/permissions are unmeasured |
| Deadline behavior | `inferred` | State/child deadlines confirmed-host; kernel and exFAT syscalls are operationally bounded only |
| Child cleanup | `confirmed-host` | Group kill, direct reap, drain join, descendant-pipe fixture; escaping groups are outside the fixed-command threat model |
| SD mount identity | `confirmed-host` | Descriptor/magic/revalidation tests; A4 physically confirmed expected SD identity behavior |
| SD publication | `confirmed-host` | Exclusive descriptor-relative tests; exact new executable path not physically run |
| Report validity | `confirmed-host` | Bounded deterministic model and offline analyzer tests |
| Stock service isolation | `confirmed-host` | No service-control path, preload, hook, stock signal, or internal write; physical coexistence still unrun |
| Rollback | `inferred` | SD-only future invocation and one SD result; no device persistence, but final package does not yet exist |
| Physical test procedure | `blocked` | Requires separate package review and explicit physical authorization |

`confirmed-host` never means hardware-tested. No new `confirmed-physical` result
was produced in P3.2f.

## Proposed separately authorized single run

1. Confirm MPC Sample VID:PID `09e8:205c`, firmware display `1.3.0.12`, normal
   stock UI/audio/pads, and a stock-only reboot before testing.
2. Review a future inert manifest, source-to-binary hash, exact firmware guard,
   fixed invocation wrapper, file inventory, permissions, and absence of A4
   preload/drop-in/restart content.
3. Prepare one dedicated exFAT SD with an empty fixed report slot and a
   device-recognized entry point only after explicit package authorization.
4. Insert once and boot once. Do not interact with DATA hooks; none exist.
5. Stop immediately for firmware mismatch, UI/audio/control degradation,
   repeated autorun, service interruption, unexpected files, or a run exceeding
   the reviewed observation window.
6. Power down normally, remove the SD, and inspect only the fixed report. Reject
   absent, truncated, oversized, duplicate, or schema-invalid output.
7. Reboot without the test SD and verify stock UI/audio/pads and absence of
   persistent changes. Preserve hashes and the bounded report only after
   redaction review.

If the binary fails to start or a fixed utility is absent, record only that
bounded outcome and revise the adapter; do not add a shell, broaden commands, or
retry automatically.

## Go/no-go and next action

P3.2f host engineering is complete. The candidate is suitable for **separate
authorization to construct and review one zero-hook autorun package**, followed
by a distinct authorization for one physical run. It is not currently approved
for execution because that package and authorization do not exist.

The smallest next step was package construction/review with no functional code
expansion, then the single controlled zero-hook run above. A further generic
host-only milestone would not resolve loader, command-path, privilege, or
filesystem behavior.

P3.2b lifecycle restart testing, relay deployment, and B1 DATA-hook testing
remain no-go and unauthorized.

## Validation record

Executed on 2026-11-16:

- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v`: 36
  passed.
- `cargo fmt --all -- --check`: passed.
- `cargo clippy --workspace --all-targets -- -D warnings`: passed using the
  already pinned offline cargo cache because public network access was
  unavailable.
- `cargo test --workspace`: 98 passed, 0 failed; documentation targets ran 0
  tests, using the same offline cache.
- `git diff --check`: passed.
- `device/m03b-a/scripts/check-patches.sh`: passed; reproducible patch diff
  SHA-256 `2be00914fc0f9838e1a1804021853d8cb8635f6ddd91ce272bf446cfa0c2b7bd`.
- `device/m03b-b0/scripts/check-patches.sh`: passed; patch series SHA-256
  `f21f7d6398d43209281b5945ce620eb1ef2639f80d4fbbafe9e8f998e5f03479`.
- The source safety inspector passed the model, Linux FFI/acquisition, and fixed
  executable path. It permits only the reviewed `libc` dependency and confines
  unsafe code to `sd.rs`.
- Two pinned Rust 1.97/Zig 0.17.1 release links targeting
  `aarch64-linux-gnu.2.17`, with build IDs disabled, produced identical hashes.
  `file`, `readelf`, `sha256sum`, and symbol/string inspection produced the ELF
  findings above.

The first plain cargo attempt tried to update the crates.io index and failed
because DNS/network access is restricted. No dependency was downloaded: libc
0.2.191 was already present in the repository's established A4 cargo cache and
was used with `--offline` for the reported checks.

No A4/B0/B0.2 source, SDK patch, preload ABI, snapshot protocol, startup script,
autorun package, hook, listener, device service, or firmware asset changed.

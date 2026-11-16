# B1-P3.2g zero-hook SD diagnostic package

Status: host-complete and package-inspected. Physical execution is unauthorized.

## Executive conclusion

P3.2g constructs one deterministic, device-recognized `autorun/` directory for
the firmware 1.3.0.12 zero-hook collector. Its format is source-grounded in the
pinned SDK generator used by A4: `autorun.json` selects `autorun.sh`, and
`autorun.check` is SHA-256 over JSON, shell, and the fixed SDK UUID. The wrapper
checks the exact first 4096 bytes of `/usr/bin/MPC Sample`, invokes the fixed
P3.2f executable once, returns its status, and performs no service operation.

The package is ready for source and artifact review, not physical use. Loader,
glibc, shell-utility, autorun privilege, and physical mount behavior remain
unverified. A separately authorized single physical run is the smallest useful
next step. B1 DATA-hook work, relay deployment, and restart experiments remain
no-go.

## Exact structure and integrity

```text
autorun/
  autorun.check                 0644  A4 recognition checksum
  autorun.json                  0644  one fixed script entry
  autorun.sh                    0755  reviewed one-shot wrapper
  p3_2f_collector               0755  fixed AArch64 PIE
  PACKAGE-MANIFEST.json         0644  provenance and payload hashes
  SHA256SUMS                    0644  all files except itself
```

`SHA256SUMS` cannot recursively authenticate itself. It authenticates the other
five package files; the inspector reports a separate deterministic digest over
all six files. The generated `repro-a` package had:

| File | SHA-256 |
|---|---|
| `autorun.check` | `eec78fc186c4ea6c219bf43fcdd4804f413a478b694fbf42b0e2b1515de7d8a9` |
| `autorun.json` | `591725af84e1606de0c459d610bd650a8ebf178c3c0a3a8d3521b92df48deaf4` |
| `autorun.sh` | `c246de35286fa324d328bd618ef14613329b98079b7809616811aa7301bc6d1e` |
| `p3_2f_collector` | `58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c` |
| `PACKAGE-MANIFEST.json` | `0dbb9d5bafbee44292f6bc494f56109a5c206d75014dc004971ab1f00d88f33e` |
| `SHA256SUMS` | `af76e05fb0e321f318f96128a529ff5faf5c0aeeaaf92ee07bff6b18ace12914` |

The inspector's ordered-content package digest was
`1c232a1d52890c219996f5163b9c881a920a6169e63f3945ca32e6eea7fede0a`.
Generated directories remain ignored under `target/b1-p3-2g/`.

## Wrapper behavior

The wrapper stays within the established POSIX shell environment but hardens
A4's original pipeline. A first prefix pass reports the `dd` and `wc` statuses
through a separate inherited descriptor and requires exactly 4096 bytes. A
second pass independently reports the `dd` and `sha256sum` statuses and requires
the exact canonical hash output. It needs no `pipefail`, temporary file, or
background job. It rejects arguments, fixes `PATH`, clears shell startup and
dynamic-loader environment variables, resolves its directory, and requires
`/media/<component>/autorun`. It requires regular, non-symlink stock and
collector files. The firmware digest is exactly
`44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f`.
There is one foreground `exec`, so the wrapper leaves no child and the collector
status becomes the autorun status. There is no loop, retry, copy, signal, or
post-collector operation. The collector independently repeats the same firmware
guard before acquisition.

This wrapper does not impose an external kill timeout. The collector owns a
ten-second monotonic deadline, but P3.2f already records that some direct Linux
and removable-filesystem syscalls are operationally, not forcibly, bounded.
Adding an unverified `timeout` utility or background watchdog would enlarge the
execution surface and was rejected.

## Generator and inspector

Generate only below the ignored build area:

```sh
python3 tools/b1_p3_2g_package.py \
  --repo . \
  --collector target/aarch64-unknown-linux-gnu/release/p3_2f_collector \
  --output target/b1-p3-2g/repro-a \
  --source-revision fcb5a8de451f090657de2bc36a40f2a388232b9b
python3 tools/b1_p3_2g_inspect.py --repo . target/b1-p3-2g/repro-a
```

The generator accepts no command or device destination, pins the firmware and
collector hashes, refuses existing output and paths outside
`target/b1-p3-2g/`, and specifically rejects `/media`, `/mnt`, `/run/media`, and
`/dev`. It writes fixed names and modes with deterministic JSON and no timestamp
or random identifier. Two independently generated trees compared byte-for-byte
equal.

The inspector rejects symlinks, special or extra files, mode changes, metadata
changes, checksum inconsistencies, an unreviewed wrapper or collector, preload
activation, service control, firmware update, memory modification, a listener,
background syntax, and deviations from the fixed one-call control flow. It
checks structure as well as prohibited operations.

## Complete execution path

| Stage | Classification | Result |
|---|---|---|
| Stock recognition of `autorun.json`/`.check` | passive observation by stock UI | A4 format reused; physical recognition of this package untested |
| Wrapper and collector startup | transient execution | foreground only |
| Two exact firmware-prefix guards | passive observation | mismatch is terminal before acquisition/publication |
| Fixed systemd, `/proc`, `/run`, and USB facts | passive observation | bounded sources selected in P3.2f |
| Report creation | persistent SD write | one exclusive fixed file, at most 4096 bytes |
| Exit | transient execution | no service operation or continuation |

No internal-device write, preload, drop-in, daemon reload, service restart,
hook, listener, or firmware operation appears in the path.

## Host simulation and failure analysis

Tests execute a fixture copy of the actual wrapper with only the fixed device
path/hash substituted for temporary fixtures. They cover success, collector
status propagation, missing stock executable, wrong firmware, missing
collector, arguments, interruption, and repeated invocation. The real wrapper
has one invocation site; a simulated second run reaches the fixture collector
once and fails terminally on its no-clobber marker.

| Failure | Behavior |
|---|---|
| Loader or required library missing | kernel/loader rejects collector; wrapper returns failure; no fallback |
| `dd`, `sha256sum`, `cut`, or `dirname` missing | guard/directory resolution fails inertly; no report |
| Collector cannot start | wrapper returns failure; no fallback installation |
| `systemctl` missing or output differs | acquisition fails or becomes partial according to P3.2f's exact source contract |
| MainPID zero, `/proc` denied, identity changes | collector fails; no valid report is published |
| SD identity differs, report exists, SD full | publication fails closed; no alternate name or retry |
| Interrupted or blocked SD write | a truncated exclusive final file may remain and is rejected offline |
| Collector exceeds expected window | internal deadline handles interruptible stages; an uninterruptible syscall remains a stop/recovery condition |
| Wrapper invoked twice | each invocation calls at most once; existing fixed report makes publication fail, with no retry |

An absent report proves neither that autorun was recognized nor that the
collector started. UI observation and package/report preservation are required.

## Linked executable and runtime assumptions

The reviewed artifact is a 540528-byte AArch64 PIE, entry `0x30490`, requesting
`/lib/ld-linux-aarch64.so.1` and `libc.so.6`, `libpthread.so.0`, and `libdl.so.2`.
Its maximum required symbol version is GLIBC 2.17 and it has no writable and
executable load segment. Host rebuilds were byte-identical. A4 is related
runtime evidence, not proof that this standalone PIE starts from autorun. No
loader or libc is bundled or installed; incompatibility fails inertly.

## Proposed separately authorized operator procedure

1. Confirm physical model VID:PID `09e8:205c`, displayed firmware `1.3.0.12`,
   stock-only boot, and normal UI, audio, pads, and controls.
2. On the host, rebuild and inspect the artifact; compare every hash above and
   verify the dedicated SD contains only the reviewed `autorun/` tree.
3. Confirm the SD has no prior `b1-p3-2c-report.txt`. Insert it only under a
   separately approved procedure and allow the one automatic autorun invocation.
   With `askQuestion:false`, no confirmation prompt is expected.
4. Observe for no more than the reviewed window plus a conservative operator
   margin. Stop for UI/audio disruption, repeated autorun behavior, restart indication,
   unexpected file, hang, or any service interruption. Do not retry.
5. Shut down normally, remove the SD, preserve it read-only, and validate the
   package and report offline. Redact identifiers and unrelated addresses.
6. Boot once without the test SD and repeat normal stock UI/audio/pad/control
   checks. Preserve the original package, report (including incomplete output),
   hashes, observations, and validation result.

This procedure is proposed only; it has not been authorized or executed.

## Host validation results

- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v`:
  44 passed, including 8 package/wrapper tests and failed/short/malformed
  firmware-prefix coverage.
- `cargo fmt --all -- --check`: passed with pinned Rust 1.97.0.
- `cargo clippy --locked --offline --workspace --all-targets -- -D warnings`:
  passed.
- `cargo test --locked --offline --workspace`: 98 passed, 0 failed; binary and
  documentation targets contained 0 tests.
- Both collector source safety inspections passed. No listener, arbitrary
  process execution, unrestricted traversal, or unsafe code outside the
  reviewed Linux SD/containment boundary was found.
- Both SDK patch-integrity checks passed: A4 reproducible diff
  `2be00914fc0f9838e1a1804021853d8cb8635f6ddd91ce272bf446cfa0c2b7bd`;
  B0 patch series
  `f21f7d6398d43209281b5945ce620eb1ef2639f80d4fbbafe9e8f998e5f03479`.
- The pinned AArch64 release build completed offline and reproduced collector
  SHA-256 `58c0...c49c`. The linker emitted only its known deprecated optimization
  setting warning. `file` and `readelf` confirmed the architecture, interpreter,
  dependencies, GLIBC 2.17 ceiling, and absence of a writable/executable load
  segment.
- Two package generations compared equal with `diff -r`; the strict inspector
  returned `pass` with no violations.
- `git diff --check`: passed.

An initial cross-build command accidentally resolved the environment's Rust
1.99 binary rather than the pinned rustup proxy and failed because that sysroot
lacked the AArch64 standard library. No artifact was produced by that attempt.
Prepending the pinned rustup proxy selected Rust 1.97.0 and the documented build
then passed reproducibly.

## Go/no-go

**Host package construction: go and complete. Physical execution: no-go pending
explicit authorization. B1 physical hook testing: no-go.** The smallest next
action is owner review of this exact source/package and, if accepted, separate
authorization for one zero-hook run. No further general architecture milestone
is recommended before that decision.

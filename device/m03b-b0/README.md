# M0.3b-B0.2 post-interaction counter snapshot

This isolated, non-deployable stage evaluates one transparent encoder-dispatch
hook. It is not part of the host workspace and does not alter the A4 probe.
Physical execution and SD packaging are explicitly outside B0.

`DATA_HOOK` was selected because its reviewed ABI is one `unsafe extern "C"
fn(usize, i32)` returning unit, it has no paired-hook dependency, and transparent
forwarding needs no firmware-owned payload decoding. `NOTE_ON`/`NOTE_OFF` are an
inseparable lifecycle pair; `CC`, `BUTTONS`, and `SURFACE` carry more layout or
raw-payload assumptions.

The pinned A4 probe patches are applied first, followed by the B0 patch that
exposes only the SDK-owned DATA offset through a selective API. Preparation
allocates and fills relay/trampoline pages but does not modify stock text. The
original trampoline is published before `publish`; `publish` is the first phase
that attempts a stock instruction write. If `touched()` remains false after a
failed publication, the engine has read back the original word before releasing
its private pages. This proves no SDK branch word remains under the engine's
single-installer assumptions, but does not prove the final RX `mprotect`
succeeded. Rollback is not atomic and B0.1 does not claim otherwise.

The callback records saturating invocation, successful-forward, and
positive/negative/zero-delta counters in one fixed 72-byte `MAP_SHARED` file
created exclusively as `/run/hereticc-m03b-b1p0-counters-<pid>.bin`. B1-P0
versions the layout to 72 bytes and records the process start token from
`/proc/<pid>/stat` to reject stale files after PID reuse. It retains
no firmware pointer or raw event data, and performs no I/O or locking.
The constructor writes a bounded initial report to `/run` and to two isolated
`m03b-b01-init-*` slots using A4's exact exFAT mount and no-clobber policy.
An opt-in `b02-snapshot` reader takes that file and an optional synthetic
`/proc` root, requires its recorded PID to resolve exactly to
`/usr/bin/MPC Sample`, takes bounded repeated fixed-size reads, validates counter
invariants, and prints a bounded address-free report. The reader is a host-side
component only: it is not cross-packaged or installed. How it could be invoked
after interaction on the device without restarting `ac50` remains unverified.
The access assessment and non-executable B1 design are in
[`docs/INPUT-HOOK-B1-P0.md`](../../docs/INPUT-HOOK-B1-P0.md).

Run locally:

```sh
device/m03b-b0/scripts/check-patches.sh
device/m03b-b0/scripts/build.sh
```

The resulting AArch64 ELF necessarily imports executable-page and patching
machinery. That is incompatible with A4's zero-mutation ELF profile, so B0 uses
a separate allowlist while continuing to reject ALSA exports, paint hooks,
unrelated input hooks, dynamic exports, and writable/executable ELF sections.
No package or autorun script is produced.

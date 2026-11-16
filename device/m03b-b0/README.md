# M0.3b-B0 selective DATA-hook experiment

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
original trampoline is published before `commit`; `commit` is the first phase
that attempts a stock instruction write. A failed commit is reported as clean
only when the SDK says no branch remains live. Rollback is not atomic and B0
does not claim otherwise.

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

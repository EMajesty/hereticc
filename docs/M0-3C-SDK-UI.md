# M0.3c SDK UI and input prototype

Status: implemented, host-tested, cross-built, and package-inspected. It has not
run on an MPC Sample. Physical execution requires separate owner authorization.

## Implemented scope

`crates/neurogenic-device` is a device-only Rust `cdylib` using
`mpc-sample-sdk` v0.7.1 at exact commit
`9487cbba3648afb29d8e3107e31a7ac930985220`. The SDK remains MIT licensed; its
notice is preserved in `THIRD-PARTY-LICENSES.md`. `neurogenic-core` is unchanged
and has no SDK dependency.

The mod draws a functional 320x240 overlay only when the SDK reports the stock
Sample page. It contains a title, selected track/step, sixteen step cells,
active cells, cursor, control hints, and an explicit memory-only notice. State
is fixed-capacity: one open flag, track and step indices, sixteen `u16` patterns,
button and pad ownership bits, and one repaint flag. No firmware pointer is
retained.

The platform-independent reducer owns all state transitions and can be tested
on the host. The SDK callback layer only maps events, asks the reducer for a
forward/consume decision, paints with the callback-local `Graphics`, and asks a
currently visible Sample page to repaint.

## Controls

| Control | Closed | Open |
|---|---|---|
| SHIFT + DATA push | DATA press opens; DATA pair is consumed; SHIFT forwards | DATA press closes; DATA pair is consumed; SHIFT forwards |
| DATA turn | forwards | moves the step by the signed delta, wrapping 1–16; consumes nonzero deltas |
| Pads 1–16 | forward to stock | select tracks 1–16; complete owned press/release pairs are consumed |
| PLUS | forwards | toggles the selected step; complete owned press/release pair is consumed |
| Every other input | forwards | forwards |

SHIFT + DATA is the SDK's documented example chord. PLUS is the SDK's named
`event::button::PLUS` (`0x36`). Neither mapping is claimed hardware-validated
for NEUROGENIC yet. Consuming pads while open intentionally prevents their stock
sample triggers. The prototype does not change stock samples, patterns,
projects, settings, or saved data.

If page ownership is lost, the overlay closes. Already consumed presses remain
in a bounded draining set until their matching releases arrive, so stock cannot
receive a release whose press it never received. New input forwards. The mod
does not change pad lights; restoration therefore needs no lighting write. A
normal chord close requests an unoverlaid stock repaint.

## SDK integration and hook risk

The dependency supplies the loader, 1.3.0.12 firmware table, all hooks, paint
vtable integration, drawing, forwarding, cross-linker, and autorun generator.
NEUROGENIC adds none of those mechanisms.

The v0.7.1 constructor finds the main image, verifies its complete code-word
table, then publishes the image base. It prepares all 22 SDK branch sites,
seals relay/trampoline pages, commits branch words, and separately changes the
Sample-page paint vtable. The public SDK does not selectively install only the
four facilities this mod uses. A preparation may be absent as long as at least
one site prepared; a failed commit attempts rollback. A rollback failure can
leave forwarding branches live, and the paint-vtable change is outside the
branch transaction. There is no live unload. Recovery is process restart and,
for stock-only recovery, boot without the autorun SD.

The generated wrapper copies the `.so` to `/run`, writes a transient
`/run/systemd/system/ac50.service.d/neurogenic-m03c-ui.conf`, runs
`systemctl daemon-reload`, and restarts `ac50`. This interrupts the stock
application. The wrapper and drop-in are runtime-only, but removing the SD does
not undo an already active process; reboot without the SD does.

The stripped AArch64 shared object is 383,800 bytes, requires `libc.so.6`,
`libpthread.so.0`, and `libdl.so.2`, and has maximum imported version
`GLIBC_2.30`. It has an `.init_array`, non-executable stack, separate RX and RW
load segments, and no writable/executable load segment. The full SDK links its
audio interposition and an approximately 8.3 MiB zero-filled audio scratch BSS
even though this mod returns no audio hooks. No network listener or service API
is implemented by the mod.

## Build and package

Run:

```sh
device/m03c/scripts/build-package.sh
```

The script verifies/checks out the exact SDK revision under ignored
`target/m03c/sdk`, reuses the established Rust 1.97 AArch64 toolchain, calls the
upstream SDK's `build-mod.sh`, and runs the repository's strict package-content
inspector. It does not mount or copy to removable media.

Current host artifact:

```text
target/m03c/build/neurogenic-m03c-ui.zip
```

| Artifact | SHA-256 |
|---|---|
| unstripped ELF | `a98d76c50090d8593b1f7a9585e38504dc3cc17c8c5b2d85fe1de75440666812` |
| stripped/package `.so` | `f30c2321abd8212031f9d5e150af2eae7e9183883930139d3ad4576651262842` |
| current ZIP | `3fc3baee9a183fec0886ec79ed05c63ec8d6ecf0a70d30f993c69418b7fc69fd` |

The ZIP contains exactly:

| Path | SHA-256 |
|---|---|
| `autorun/autorun.check` | `6c5a527e1e45466d82afaf2ae3788213a76199721caa024844ed0a569fd06b32` |
| `autorun/autorun.json` | `91007809b667e8b15460fa30f883bee8774bb2a5a7029c6f0be4c7e30f30bdf5` |
| `autorun/autorun.sh` | `83695c369233cb8357faf7f1ee16107994895b5b3959f7d2c028e6f2a413fc73` |
| `autorun/neurogenic-m03c-ui.so` | `f30c2321abd8212031f9d5e150af2eae7e9183883930139d3ad4576651262842` |

Two builds produced identical ELF and `.so` bytes. The upstream SDK uses
ordinary `zip`, whose entry timestamps made the two ZIP byte streams differ.
Package content and `autorun.check` were identical. Treat the listed ZIP digest
as identifying this host artifact, not as a reproducible-build claim.

The inspector verifies exact inventory, no symlinks, SDK autorun checksum,
packaged/library byte equality, exact firmware prefix hash, one fixed preload,
and the expected runtime drop-in/restart sequence. It rejects unexpected
service, network, or persistent-storage operations in the wrapper.

## Host coverage

Reducer tests cover opening/closing/re-entry, cursor wrap and full signed
deltas, track selection, one-toggle-per-press, owned releases, unowned and
invalid input forwarding, unexpected page loss, draining after abort, and
bounded repaint state. Workspace formatting, Clippy, Rust/Python tests, SDK
patch-integrity checks, package inspection, AArch64 build, and whitespace checks
must pass before physical use.

## Proposed one-run hardware procedure

This procedure is review material, not authorization.

1. On stock firmware 1.3.0.12, boot without the test SD. Confirm normal Sample
   page, audio, all pads, DATA turn/push, SHIFT, and PLUS. Save current work.
2. On the host, rebuild or use the exact ZIP above. Run
   `device/m03c/scripts/inspect-package.py ZIP SO` and verify its digest against
   the selected artifact. Inspect that the archive contains only the four paths
   listed above.
3. On a dedicated, already prepared SD, remove any old `autorun/`; unzip the
   package so `autorun/autorun.json` is at the SD root level. Verify the four
   file hashes, safely eject, and do not merge autorun packages.
4. Insert the SD and boot once. No prompt is expected. The SDK wrapper restarts
   `ac50`, so a startup interruption is expected. Stop if boot/project loading
   does not settle, the app restarts repeatedly, audio is abnormal, or controls
   are unresponsive.
5. On the Sample page, press and hold SHIFT, press DATA once, release DATA, then
   release SHIFT. Confirm the NEUROGENIC header and grid appear.
6. Turn DATA through both wrap boundaries. Press several pads and confirm the
   track number changes without sounding those pads. Press/release PLUS and
   confirm only the selected cell changes.
7. Close with the same SHIFT + DATA sequence. Confirm the stock Sample page is
   fully repainted; confirm pads, PLUS, DATA, SHIFT, audio, and ordinary stock UI
   behavior are restored.
8. Shut down normally. Remove the SD and boot stock-only. Repeat the baseline
   UI/audio/pad/DATA checks.

Stop immediately for a boot loop, repeated service restart, blank/corrupt
display, stuck or unmatched input, unintended sample playback while the page is
open, loss of audio, project mutation, failure to close, or incomplete stock
restoration. Power down normally if possible, remove the SD, and perform the
stock-only recovery boot. Do not retry unchanged after a safety regression.

## Known limitations

- Host tests do not validate physical hook installation, drawing, button codes,
  repaint behavior, page-loss timing, or control coexistence.
- A page change that produces no SDK callback cannot be observed immediately;
  the next button, pad, encoder, or paint callback performs the visibility
  check and closes the overlay.
- The UI state is volatile and has no playback, MIDI, parameter locks, sample
  operations, persistence, animation, or generalized HAL.
- SDK activation restarts the stock application and modifies its live process.
  Earlier zero-hook results do not prove this package safe.
- The SDK's broad all-site installation and non-atomic failure boundaries remain
  the principal physical risk. This package is a candidate for one controlled
  test, not hardware-validated functionality.

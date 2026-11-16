# M0.3d stock sample triggering prototype

Status: implemented, host-tested, cross-built, and package-inspected. M0.3c is
physically validated on one MPC Sample running stock firmware 1.3.0.12; the new
M0.3d playback path has not run on hardware.

## Playback path

The device crate remains pinned to MIT-licensed `mpc-sample-sdk` v0.7.1 commit
`9487cbba3648afb29d8e3107e31a7ac930985220`. While the overlay is open, a pad
press selects that pad/track and calls the SDK's unsafe
`midi::send_note_on(input, source, pad, properties, velocity)`. Its matching
release calls `midi::send_note_off(input, source, pad)`. Both calls occur only
inside the same kind of live SDK pad callback that supplied the firmware
objects and on its UI path. The reducer retains only a zero-based `u8` pad; it
never retains `input`, `properties`, or another firmware pointer.

`NoteEvent::replay_as_pad` is deliberately not used: it rewrites an existing
incoming MIDI event and does not synthesize an audition from a button callback.
The physical pad callback is the only current supported callback that supplies
all objects required by `send_note_on`.

The consumed pair addresses a pad/source rather than a voice handle. The SDK
returns only whether the note-on hook exists and returns no voice identity from
the stock engine. M0.3d therefore permits one audition pad at a time. Repeated
presses do not retrigger until release; another pad pair while held is consumed
without dispatch. This establishes neither independent polyphony nor
scheduler-grade timing.

If the callback lacks required objects or the SDK note sites are unavailable,
the event forwards unchanged to stock and no audition ownership is recorded.
If an owned release unexpectedly lacks explicit-dispatch context, that matching
physical release forwards to stock as the safe note-off fallback.

## Controls and restoration

M0.3c controls remain unchanged except that pads now audition:

| Control | Closed | Open |
|---|---|---|
| SHIFT + DATA | open overlay | close overlay |
| DATA turn | stock | move step cursor |
| Pads 1–16 | stock | select stock pad/track and hold one native audition |
| PLUS | stock | toggle selected step in volatile memory |
| Other input | stock | stock |

The overlay labels the selected stock pad and `NOTE ON` state. A close request
while a note is held is consumed and deferred; releasing the pad first sends
the matching note-off, then closes and repaints stock. Unexpected Sample-page
loss closes immediately but keeps only the bounded pad ownership necessary to
drain the later release. No sample assignment, slice, pattern, project, effect,
or setting is written, and no pad lighting is changed.

There is no documented SDK call that can construct a note-off without a live
input object. If hardware or firmware loses the physical release entirely,
M0.3d cannot prove recovery from a stuck stock voice. That is a physical stop
condition; close normally if possible, shut down, remove the SD, and perform a
stock-only recovery boot.

## Host coverage

The fixed-capacity reducer tests cover the M0.3c behavior plus one note
lifecycle, repeated presses, rapid retrigger after release, suppression of an
overlapping second pad, pad switching after release, deferred close, unexpected
page loss, unsupported callback context, release fallback, and unowned input
forwarding. Firmware calls remain a thin callback adapter and are not executed
by host tests.

Build and inspect with:

```sh
device/m03d/scripts/build-package.sh
```

Current host artifact:

```text
target/m03d/build/neurogenic-m03d-trigger.zip
```

| Artifact | SHA-256 |
|---|---|
| unstripped ELF | `2e7ba9245a35d218664d474f7fd4a278b323c5ba3476a150696e471a56101d0d` |
| stripped/package `.so` | `de696122926699fd83464d82e354823b848eca82608e13445acff62924caafe3` |
| ZIP | `cab41853abf7a5c5b3bb4ae525fabc25befeeab39b68c3d3d40c4441f1b44c2a` |

The strict inspector verifies the exact four-file SDK package, exact firmware
prefix guard, packaged library identity, transient `/run` preload/drop-in, and
expected SDK restart sequence. It rejects unexpected services, network tools,
and persistent paths. The known-good M0.3c package remains separately at
`target/m03c/build/neurogenic-m03c-ui.zip` when retained locally.

## One-run hardware procedure

Physical execution requires owner authorization.

1. Boot firmware 1.3.0.12 without the test SD and verify stock UI, audio,
   pads, transport, DATA, SHIFT, and PLUS. Save work.
2. Inspect the ZIP with `device/m03d/scripts/inspect-package.py ZIP SO`, verify
   the hashes above, and place only its `autorun/` on a dedicated SD. Keep the
   validated M0.3c package available for rollback; do not merge autoruns.
3. Boot once. The SDK wrapper restarts `ac50`; wait for the stock application
   to settle. Stop for a restart loop, abnormal audio, display corruption, or
   unresponsive controls.
4. With transport stopped, open using SHIFT + DATA. Press and release one pad.
   Confirm one native sound starts and releases and the overlay changes from
   `NOTE ON` to ready.
5. Test repeated complete presses, rapid press/release cycles, and an attempted
   second pad while the first is held. Then release and switch pads. Do not
   infer independent voices from these tests.
6. Hold a pad and request close. Confirm the overlay asks for release, then
   release and verify it closes without a stuck note. Reopen and repeat while
   stock transport is running; stop for any pattern/project mutation or
   unexpected transport interaction.
7. Close normally and verify all stock pads, controls, UI, transport, and audio.
   Shut down, remove the SD, boot stock-only, and repeat the baseline checks.

Stop for an unmatched/stuck note, extra trigger, missing release, crash/restart,
loss of stock audio/control, project mutation, or incomplete restoration. Do
not retry unchanged after a safety regression.

## Decision gate

M0.3d is host-complete but playback is not physically validated. A passing run
would establish bounded interactive stock-pad trigger/release only. Background
sequencing needs a later, separately reviewed callback/timing design because
the current safe call requires live pad-callback objects.

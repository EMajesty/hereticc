# M0.3e stable UI baseline and stock sequencer integration

## Executive result

M0.3e removes the M0.3d audition, capability display, deferred-close state, and
32-entry press ledger. The device callbacks and reducer again follow the
physically validated M0.3c model: while the overlay is open, physical pads
1-16 select tracks and their owned press/release pairs are consumed; they do
not trigger stock samples. SHIFT+DATA toggles the page, DATA moves the cursor,
PLUS toggles the selected step, and all unowned controls forward.

This is a host-tested restoration candidate, not a claim that M0.3e has passed
hardware testing. M0.3c remains the known-good rollback package.

M0.3d.3 failed physical acceptance on stock firmware 1.3.0.12: ownership could
remain stale after release, deferred close was unreliable, pads could become
temporarily unresponsive, bank A differed from B-D, and status repaint flicker
persisted. Stock-only recovery was normal. The one-shot samples continuing
after release were expected playback and are not classified as held input.

## Restored interaction model

The restored state is one open flag, selected track and step, sixteen `u16`
step bitsets, one 16-bit physical-pad draining mask, two owned button bits, and
one repaint bit. It has no audition state, firmware input/source identity,
properties pointer, hook-capability bits, or delayed close.

| Control | Closed | Open |
|---|---|---|
| SHIFT+DATA | open; consume DATA pair | close immediately; consume DATA pair |
| DATA turn | forward | move cursor and consume nonzero delta |
| PLUS | forward | toggle selected step; consume pair |
| Physical pads 1-16 | forward | select track; consume pair; no sample trigger |
| Bank buttons and all other input | forward | forward |

The SDK exposes a physical pad index but no documented A-D bank state. M0.3e
therefore models sixteen NEUROGENIC tracks only and does not claim 64 stock-pad
identities. Bank controls remain stock-owned.

## Pattern-to-core bridge

`neurogenic-core::pattern_from_step_bits` translates one track's `u16` grid
into the existing semantic `Pattern` representation. It uses the core's 960
PPQ resolution, sixteen quarter-beat steps (240 ticks each), stable event IDs,
one-step durations, and `SoundSource::TrackDefault`. The existing `Scheduler`,
not a new device scheduler, then produces deterministic `NoteOn`/`NoteOff`
events. A host integration test edits steps 1 and 5 through the UI reducer and
verifies scheduled note-ons at ticks 0 and 960.

This bridge performs no firmware access. A future device adapter must resolve
each semantic track/default sound into the stock event representation.

## Upstream stock-sequencer findings

Inspected revisions:

- `mpc-sample-sdk` v0.7.1,
  `9487cbba3648afb29d8e3107e31a7ac930985220`, MIT;
- `mpc-minimod`, `18d41fd805beace4fed623653a250c46a4fa2243`,
  used as source/research evidence only;
- `am-mod`, `6cdc67276f43cd39f64871cb7e36782481244c65`, MIT.

### SDK

The pinned SDK has physical pad forwarding, callback-local MIDI event replay,
transport-independent timing helpers, samples, and drawing. It has no public
stock sequence, sequence-event-list, or event insertion API. `NoteEvent` can
rewrite an already-arriving MIDI event and restore its bytes; it is not a way
to create scheduled events. See the pinned
[MIDI implementation](https://github.com/ioma8/mpc-sample-sdk/blob/9487cbba3648afb29d8e3107e31a7ac930985220/src/midi.rs).

### MINIMOD

MINIMOD confirms that physical-pad recording occurs above the note dispatcher
in the stock pad-grid observer path. External MIDI and rewritten note dispatch
enter below that observer and are not captured into stock sequences. Its
incoming-event replay therefore cannot turn NEUROGENIC's autonomous schedule
into stock sequencer events. See the pinned
[recording findings](https://github.com/ioma8/mpc-minimod/blob/18d41fd805beace4fed623653a250c46a4fa2243/docs/HISTORY.md#6-sequence-recording-not-implemented).

### Am MOD

Am MOD contains the most relevant implementation. Its pinned 1.3.0.12 code
models the native musical event as a 56-byte value: signed 64-bit tick at offset
0, tag word at offset 8, and a 40-byte inline payload at offset 16. Tag 3 is a
note event; event construction uses the stock constructor. The event carries
960-PPQ timing/duration, normalized velocity, a stock note/pad routing value,
and packed modifier fields used for tune/slice and other per-hit data.

Its Step implementation obtains the currently live Seq controller, program,
sequence event list, length, and stock sound mapping on the UI thread. Adds,
changes, removals, and grouped changes go through native command/result paths,
use firmware-owned containers/copying, and participate in stock Undo. The stock
transport then plays the event list. It validates page/controller/event-list
lifetime and bounds before each operation.

This is strong source and emulator/test evidence for the integration point,
and Am MOD release history reports working stock Step/Seq behavior. It is not a
stable public ABI: the offsets, native types, constructors, command addresses,
controller ownership, and destruction notifications are firmware-specific.
NEUROGENIC copies none of this code in M0.3e. The primary reference is Am MOD's
pinned [Step implementation](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/sd-mod-session/src/step_native.c).

## Recommended integration point

The shortest credible playback route is a small upstream-compatible SDK module
for **UI-thread stock sequence commands**, based on the Am MOD evidence:

1. acquire and validate the current Seq controller, active event list, program,
   sequence length, and stock sound mapping for firmware 1.3.0.12;
2. accept a bounded SDK-owned semantic note value (tick, duration, stock pad,
   velocity, and later explicit modifier data), never a raw firmware pointer;
3. construct the native tag-3 event with the stock constructor;
4. add/remove through the stock command/Undo boundary, not direct container or
   project-memory writes;
5. reject stale page/controller generations and perform all native calls on the
   owning UI thread;
6. return a bounded success/failure result and retain no firmware pointers.

This should be proposed as a narrowly reviewed SDK extension with 1.3.0.12
guards and emulator fixtures. Direct dependency on such an SDK API is preferred
once accepted. Until then, Am MOD is a licensed reference, not code to paste
into NEUROGENIC.

Alternatives are weaker:

- physical pad or `send_note_on` dispatch is immediate, inherits the failed
  ownership path, and is not scheduled by the stock sequencer;
- incoming MIDI replay requires an incoming event and is not recorded;
- direct event-list mutation bypasses native ownership/Undo and is rejected;
- a replacement sampler violates the stock-engine goal.

## Smallest next playback experiment

The next milestone should implement the bounded SDK sequence-command adapter,
not another pad-input experiment. Its first physical test should use a fresh,
unsaved, empty test project/sequence and one stock pad:

1. map one NEUROGENIC active step to one native tag-3 event through the reviewed
   SDK adapter;
2. insert it into the live empty sequence through the stock Undo command;
3. use the normal stock transport to play it;
4. verify timing and that ordinary stock UI/audio remain functional;
5. remove it using the matching stock command or native Undo;
6. exit without saving, reboot stock-only, and verify recovery.

Acceptance requires positive confirmation that playback came from the inserted
sequence event, command failure is inert, event removal succeeds, no unrelated
project data changes, and page/controller loss fails closed. Expansion to a
whole 16-step pattern comes only after this single-event lifecycle passes.

## Build and hardware procedure

Build with:

```sh
device/m03e/scripts/build-package.sh
```

The generated package is `target/m03e/build/neurogenic-m03e-ui.zip`; artifacts
are ignored and are not committed. The strict inspector requires the exact SDK
four-file autorun layout, firmware prefix guard, packaged-library equality, and
the SDK's known runtime-only preload/restart wrapper. It rejects unexpected
network, device-disk, or persistent-service operations.

The inspected host build is:

```text
ELF  be065974e1711912f9e8347c82dabe8c21b584454873c279e6fe672662b69f0a
SO   64c9d52a10a135b214fc0fd32bb3e2e6ae5ae6d27f60f2ba7a0d808420da99aa
ZIP  25e0ba9869a583933480c82b3bf452728127b11372f57d93a23138011a89d541
```

The AArch64 shared object is 383,880 bytes, requires at most `GLIBC_2.30`
(physical device evidence reports 2.39), has separate RX/RW segments and a
non-executable stack, and depends only on the same libc/pthread/dl runtime as
the physically validated M0.3c SDK mod.

For a separately authorized UI baseline test: confirm stock firmware 1.3.0.12
and normal UI/audio/pads, retain the M0.3c rollback package, inspect and copy
only the M0.3e `autorun/`, boot once, allow the SDK stock-app restart to settle,
open with SHIFT+DATA, exercise DATA/PLUS/pad selection and bank buttons, close,
and verify complete stock restoration. Pads must remain silent while the page
owns them. Stop on stuck input, sample playback from an owned pad, flicker,
failure to close, restart loop, project mutation, or abnormal stock behavior.
Shut down normally, remove the SD, and verify a stock-only recovery boot.

M0.3e contains no stock sequence write or playback implementation. That work
requires the next separately reviewed integration milestone.

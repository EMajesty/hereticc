# M0.3d.1 pad ownership and native playback diagnosis

Status: implemented, host-tested, cross-built, and package-inspected. Physical
execution requires separate owner authorization.

## Physical M0.3d result

On one MPC Sample running stock firmware 1.3.0.12, M0.3d rendered normally and
retained DATA and PLUS behavior, but pads neither changed the selected track nor
entered the held-note/deferred-close states. Pads—including a second held
pad—played normally. Stock-only recovery was normal.

This does **not** validate native dispatch. It demonstrates that M0.3d returned
`Flow::Forward` before its reducer acquired the press, so the ordinary stock pad
path can fully explain the sound.

## Source-level cause and SDK semantics

M0.3c always passed valid pad numbers to the reducer while open. M0.3d added one
combined `callback_supported` prerequisite:

```text
event.input != 0 && event.properties != 0 && midi::notes_ready()
```

Failure of any term returned `Flow::Forward` before selection or ownership.
That regression incorrectly coupled UI ownership to playback capability.

At pinned SDK v0.7.1 commit
`9487cbba3648afb29d8e3107e31a7ac930985220`, the press `on_pad` callback is the
SDK's installed note-on hook. `midi::notes_ready()` is stronger: it reports that
both the note-on and note-off displaced sites were prepared. The physical result
does not identify which combined term failed; a zero properties object or an
unavailable paired note site remain possible.

`send_note_on` directly invokes the note-on displaced trampoline, not the hooked
entry, so calling it inside `on_pad` does not recursively re-enter the mod.
`send_note_off` similarly invokes its displaced trampoline. The SDK documents
this pair for controlled internal selection and requires callback-live objects
on the owning UI thread. Thus explicit dispatch followed by `Flow::Consume` is
source-supported when the input/properties objects exist and both note sites
are ready. It returns no voice handle or audible-success result.

## Corrected behavior

Pad ownership and native capability are independent:

- Closed overlay: every pad event forwards unchanged.
- Open overlay: every valid press selects its track and the physical pair is
  consumed, restoring M0.3c behavior.
- Paired hooks/input/properties available: the first owned press requests one
  explicit native note-on; its release requests the matching note-off.
- Capability unavailable: the pair remains owned but no explicit dispatch is
  attempted. The stock sound is suppressed, so silence is accurately labeled
  unavailable rather than misrepresented as NEUROGENIC playback.
- While one native audition is held, other pad pairs are consumed without a
  trigger. Close remains deferred until the active release.
- If readiness changes between decision and `send_note_on`, the reducer clears
  the audition and displays unavailable; it never labels the call audible.

No firmware pointer is retained. The reducer stores only pad indices, ownership
bits, and three capability bits.

## Visible diagnostic

The existing overlay shows one of:

```text
PAD CB: none
PAD CB: owned; native unavailable
PAD CB: native dispatch attempted
```

It appends `Hn In Pn`, where `H` is paired note-site readiness, `I` is a nonzero
live input object, and `P` is a nonzero properties object for the latest pad
callback. These are presence bits only; addresses and firmware data are never
shown. On release, `P0` is expected because the SDK's `PadEvent` release has no
properties argument. “Attempted” is not an audibility claim.

## Host tests and package

Reducer coverage includes selection independent of dispatch capability,
capability failure, normal owned pairs, one-voice overlap suppression, deferred
close, page loss, repeated presses, changed readiness, bounded capability
display state, and closed-overlay forwarding.

Build and inspect:

```sh
device/m03d1/scripts/build-package.sh
```

Artifact:

```text
target/m03d1/build/neurogenic-m03d1-pad.zip
```

| Artifact | SHA-256 |
|---|---|
| unstripped ELF | `600cb8f98df7b4d5d947041aacc8dd9b2b000ca8c6371b93ce22fad6a9fce3b8` |
| stripped/package `.so` | `66129f91c65b70948c34557ffbb9fa90a20e6bb45279d72f2a02cfd372afd042` |
| ZIP | `29382336400fdd1165418f15e8d0b98769b96287ab380d6babffb6baf7319519` |

The M0.3c and failed M0.3d packages remain separate rollback/evidence artifacts.

## Concise physical procedure

1. Boot stock firmware 1.3.0.12 without the SD and confirm UI, audio, pads,
   DATA, PLUS, and transport. Save work.
2. Verify the three hashes above and strict-inspect the distinct M0.3d.1 ZIP.
   Install only its `autorun/` on the dedicated SD.
3. Boot once and wait for the SDK-driven stock application restart to settle.
   Open with SHIFT+DATA.
4. Press pad 1 once. Record the selected track and the complete status line
   before release. If it says unavailable, confirm the pad is silent and record
   `H/I/P`; do not interpret silence as another failure.
5. If it says native dispatch attempted, confirm whether sound occurs, hold the
   first pad while pressing a second, then release. Verify one matching release
   and test deferred close. “Attempted” alone is not success.
6. Close and verify stock UI, pads, controls, transport, and audio. Shut down,
   remove the SD, boot stock-only, and repeat baseline checks.

Stop for a stuck/extra note, an open-page pad that plays while status says
unavailable, missing selection, crash/restart, project mutation, audio/control
loss, or incomplete restoration. Do not begin background sequencing. The next
decision follows the observed capability bits and audible behavior; no new
diagnostic infrastructure is needed.

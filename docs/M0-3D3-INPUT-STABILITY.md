# M0.3d.3 input ownership, pad banks, and restoration

## Result and scope

M0.3d.3 is a host-tested correction to the physically exercised M0.3d.2
prototype. It keeps SDK-native forwarding of the first owned physical pad pair,
but replaces the single-bit-per-pad ownership mask with a fixed 32-entry edge
ledger. It does not add synthetic triggering, sequencing, a service, or another
hook.

The M0.3d.2 run on stock firmware 1.3.0.12 established that the first pad was
selected and forwarded, a second held pad was suppressed, and the callback
reported `H1 I1 P0`. It also exposed unreliable deferred close, intermittent
temporary pad unresponsiveness, bank-agnostic numbering, and display flicker.
All samples in that run were one-shots, so audio continuing after physical
release and overlapping a later sample is expected stock playback—not evidence
of a missing release event.

## Source findings

- SDK v0.7.1 revision `9487cbba3648afb29d8e3107e31a7ac930985220`
  defines only `PadAction::Press` and `PadAction::Release`. Pressure/aftertouch
  arrives through the separate `SurfaceEvent` hook and is untouched here.
- The pad hooks pass the firmware's `pad`, `input`, and `source` values through.
  The public API documents a pad index but exposes no pad-bank query or logical
  A-D pad identity.
- The SDK calls the displaced stock handler only for `Flow::Forward`; therefore
  the matching release must make the same forward/consume decision as its
  press.
- MINIMOD revision `18d41fd805beace4fed623653a250c46a4fa2243`
  documents and handles a physical press/release source-ID mismatch. Its
  implementation was used as behavioral evidence only; no code was copied.
- SDK paint interception calls stock paint before the mod overlay. A forwarded
  pad already gives stock a chance to repaint, so requesting another repaint
  before forwarding is redundant and can race the stock paint.
- The previous callback checked Sample-page visibility before processing every
  event. That allowed the page-loss path to clear ownership before an explicit
  SHIFT+DATA close request was evaluated.

## Corrections

The reducer records every accepted physical press in a preallocated ledger:
sequence, physical pad 0-15, firmware input identity, source, and whether its
press was forwarded. Release matching prefers an exact pad/input/source match,
then falls back to pad/input for the observed source mismatch. The oldest
matching press wins. This gives repeated and suppressed presses independent
release bookkeeping and fails closed if the bounded ledger is full.

The first eligible press is forwarded and becomes the sole held audition.
Further presses are consumed and recorded; their releases are also consumed.
The active press release is forwarded. An unmatched release never clears the
active owner. Deferred close completes only when that active owned release is
received. Losing the page preserves outstanding entries so later releases can
drain with their original decisions.

Explicit SHIFT+DATA close is processed without a preceding visibility check,
and pad edges no longer run a page poll before their ownership decision.
Encoder and other button callbacks retain the existing immediate page-loss
check, while a non-Sample paint still closes and restores the overlay. This
does not claim that every stock page transition has been physically
characterized.

Forwarded pad presses no longer request an additional pre-forward repaint;
stock forwarding supplies that repaint opportunity. Consumed capability/status
changes still request one. The display says `INPUT HELD`, not `NOTE ON`, and no
longer treats an audible one-shot tail as held-input state.

## Pad-bank behavior

Bank buttons remain completely stock-owned and forward unchanged. Because the
pinned SDK exposes only the physical pad index, NEUROGENIC deliberately labels
and tracks `PHYS PAD 01-16`; it does not claim 64 banked stock assignments.
Changing banks while a pad is held does not alter the stored physical
pad/input identity or the release decision. The selected NEUROGENIC track stays
0-15. Establishing logical A-D identity would require an SDK-supported bank
state API and is outside this correction.

## Host coverage

Reducer tests cover paired edges, source mismatch, repeated presses, exact
source preference, two-pad suppression in both release orders, unmatched
release isolation, bank-button forwarding during a hold, deferred close, page
loss, capability failure, rapid reuse, and repaint policy. The package uses the
unchanged pinned SDK build and firmware fingerprint guard.

The inspectable package is generated (not committed) at
`target/m03d3/build/neurogenic-m03d3-input.zip`. The final inspected build has:

```text
ELF     9c911b17a84e9d47c0f88da501c43bf0799c080b6455e09f06f4c34cb7bcfc11
SO      0787e1d59716dc463ec5797f6e82dc9fb895130ad60247e6cb13d03e841063bc
ZIP     cc773b4960c0998a0c11b9dd0270658f3db9b81661ef99c52d29845f247fd27f
```

`device/m03d3/scripts/inspect-package.py` confirmed the exact four-file SDK
autorun inventory, packaged-library equality, firmware guard, and absence of
unexpected service/network/persistent-storage operations. The SDK generator's
ZIP metadata is time-dependent, so rebuilding can change the ZIP hash while
the ELF/SO remains byte-identical; operators must use the hashes emitted by the
specific reviewed build rather than assuming ZIP reproducibility.

## Physical test procedure

1. Preserve the known-good M0.3c package and confirm stock firmware 1.3.0.12,
   UI, audio, pads, DATA, and bank buttons before inserting the test SD.
2. Inspect and copy only the generated M0.3d.3 `autorun/` directory. Boot once
   and allow the SDK-managed stock application restart to settle.
3. Open with SHIFT+DATA. Confirm DATA and PLUS still work.
4. For each bank A-D, tap several pads and confirm the display intentionally
   remains physical pad 01-16 while stock playback follows the selected bank.
5. Hold one pad, press/release a second, then release the first. Repeat with the
   release order reversed and with a repeated press on one pad. Confirm the
   second press is suppressed and normal input resumes after release.
6. Hold one pad, request SHIFT+DATA close, then release it. Confirm the overlay
   closes on that release and stock controls respond normally.
7. Hold a bank button while tapping a pad; then change banks during a held pad.
   Confirm no temporary dead pads and no incorrect close.
8. Repeat rapid press/release sequences and inspect for overlay flicker. A
   one-shot sample continuing after release is expected and is not a failure.
9. Close normally and check stock UI/audio/pads/DATA/banks. Shut down normally,
   remove the SD, and perform a stock-only recovery boot.

Stop on a stock-app restart loop, controls that remain unresponsive after all
physical releases, inability to close/restore, unexpected project/sample
mutation, or abnormal audio/UI behavior. Remove the SD after normal shutdown
and use the known-good stock-only/M0.3c recovery path.

M0.3d.3 remains host-tested until this procedure is run. Synthetic native
playback and background sequencing remain unvalidated and out of scope.

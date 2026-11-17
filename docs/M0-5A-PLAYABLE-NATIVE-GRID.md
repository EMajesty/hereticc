# M0.5a playable native grid candidate

Status: superseded by M0.5c after the first physical run confirmed the Step
paint hook but failed to activate the chord-dependent presentation.

## Integration boundary

The experimental `native-playable-grid` feature keeps the validated M0.3e
Sample-page build unchanged. Its separate artifact hooks the already verified
firmware-1.3.0.12 Step Edit paint slot. Stock paint runs first. NEUROGENIC then
uses only the callback-local Step screen and `Graphics` objects to:

1. validate the current sequence, track, Program and event-list context;
2. consume one copied grid intent;
3. advance at most one insert/observe/remove transaction stage;
4. draw the 16-step grid; and
5. request only a bounded follow-up verification paint while the live Step
   component is still owned by the callback.

No Step screen, graphics object or other firmware pointer is retained. Physical
input callbacks modify the reducer and publish one encoded `(pad, step)` intent;
they never invoke the native command service.

## Controls and commit semantics

- SHIFT + DATA opens or closes the grid only after a Step paint established the
  editing context.
- DATA moves the NEUROGENIC cursor and is also forwarded to stock as the
  temporary explicit repaint handshake.
- pads select one of the 16 physical pad rows; complete press/release pairs are
  consumed while open;
- PLUS publishes one toggle intent; complete pairs are consumed and rapid
  repeats are rejected while any edit is pending.

PLUS never changes the displayed committed bit. ON appears only after the stock
comparator finds one unique canonical event. OFF appears only after exact
native removal and verified absence. The footer displays `READY`, `PENDING`,
`FAILED`, or a context-change failure. Eight unsuccessful observation paints
are the fixed retry limit.

The repaint handshake is intentionally narrow. After opening or pressing PLUS,
the next DATA turn gives stock Step Edit a legitimate reason to paint. A
callback-local repaint request normally advances the subsequent verification;
another DATA turn is the safe fallback if the stock UI coalesces it. No worker
thread calls firmware UI functions.

## Context invalidation

Every Step paint copies and compares the same non-dereferenceable context token
used by the physically verified adapter. A sequence, track, Program or event-list
change closes the overlay, clears local bits and discards all receipts. A later
removal can therefore use only the canonical receipt owned by the same cell and
context. Painting the SDK Sample page also deactivates and clears the grid.

The SDK does not expose a general page-change callback. Transition directly
from Step Edit to another unhooked stock page may be detected only when the next
Step or Sample paint occurs. Until physical testing establishes the exact page
sequence, leave the overlay with SHIFT + DATA before navigating elsewhere.

The grid intentionally starts empty and does not import pre-existing events.
Use only a disposable empty sequence/track. A pre-existing equal event makes
insertion ambiguous and fails closed. Closing the overlay deliberately discards
all receipts and local bits; notes left in the stock sequence then become
stock-owned and cannot be removed by reopening this prototype.

## Artifact

Build and inspect without contacting hardware:

```sh
python3 tools/neurogenic-hil/hil.py build --native-m05a
```

The reviewed output is:

```text
target/m05a/build/neurogenic-m05a-native-grid.so
```

| Artifact | SHA-256 |
|---|---|
| linked ELF | `2b3080bdeeda565e840b81c5a1d9b2ea01638a153b720eac9441474604c595c0` |
| stripped shared library | `23221a9943a50865607818ab04672cec06aa04a363ccaee68a19c0c96ce4ac49` |

The M0.5b instrumented shared library is a 657,616-byte stripped AArch64 DSO. Static inspection
found no prohibited shell, process-control, or network imports. It uses the
pinned SDK revision `9487cbba3648afb29d8e3107e31a7ac930985220` and the
previously reviewed native adapter.
Two consecutive builds produced the documented ELF and shared-library hashes.

Host validation comprises 113 Python tests, 138 Rust workspace tests, Clippy
with warnings denied, formatting, both SDK patch-integrity checks, Nix flake
evaluation, the AArch64/QEMU ABI fixture, the M0.5a cross-build and ELF/import
inspection. The separate M0.3e rollback configuration also cross-builds and
passes its package inspector. None of these results proves Step overlay,
repaint, input, or audible behavior on physical hardware.

## Controlled first physical run

This procedure is the next owner action, not a completed result.

1. Save current work. Create a disposable empty one-bar 4/4 sequence and empty
   drum track, assign a known one-shot sample to A01, and open stock Step Edit.
2. Use the M0.5b guarded one-command cycle rather than the superseded manual
   multi-command sequence:

   ```sh
   python3 tools/neurogenic-hil/hil.py --config target/hil/config.json grid-experiment --confirm MPC-SAMPLE-1.3.0.12 --confirm-grid PLAYABLE-GRID-ONE-BAR-1.3.0.12
   ```

3. Reopen Step Edit after the controlled application restart. Toggle the grid
   with SHIFT + DATA, then turn DATA once to obtain the first overlay paint.
4. Return the cursor to step 1. Press PLUS, then turn DATA four detents to step
   5. Wait for `READY`; repeat for steps 5, 9 and 13. The expected ticks are
   0, 960, 1920 and 2880.
5. Start stock transport and listen for four evenly spaced A01 hits. Stop on a
   `FAILED` footer, UI loss, wrong notes, repeated application restart, input
   loss, or abnormal audio.
6. Select step 9, press PLUS, perform one DATA repaint handshake if needed, and
   wait for `READY`. Confirm three hits remain.
7. Close with SHIFT + DATA, turn DATA once if stock Step Edit needs a repaint,
   and inspect the four expected positions with tick 1920 absent.
8. Enter the exact PASS or FAIL acknowledgement. The runner restores stock in
   `finally`; if restoration fails it prints the stock-only recovery procedure.

Confirm the stock process settles and ordinary Step Edit, pads, transport and
audio work. Do not save the disposable sequence. Physical success must not be
recorded until the owner observes both the audible pattern and stock event list.

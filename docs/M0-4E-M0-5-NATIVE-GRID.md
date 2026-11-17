# M0.4e native lifecycle and M0.5 grid boundary

## M0.4e result

The owner-run report
`target/hil/runs/20261117T190024.644292Z-3813062/report.json` physically
establishes the complete volatile editing lifecycle on firmware 1.3.0.12:

1. one native note command was accepted;
2. one unique event was observed (`native=1`, `bytes=1`);
3. exact native removal was requested;
4. a later callback reported `removed`, `event_removed=1`, and a zero-event scan;
5. automatic stock restoration passed.

The run was marked failed only because polling skipped the transient
`remove_accepted` state. The runner now treats `removed` as satisfying the
earlier acceptance milestone only when sticky `event_removed=1` and diagnostic
`event_removed` are both present. Verified absence still requires that final
state. Failed, rejected, and expired states remain terminal. Regression tests
cover the skipped intermediate transition and reject incomplete sticky proof.

Together with the earlier owner observation of a visible and audible inserted
note, native insertion, unique observation, exact removal, absence, playback,
and stock restoration are physically verified. Repeating the same insertion
solely to change the report label is unnecessary.

## M0.5 host implementation

`native_grid::NativeGridEditor` is the fixed-capacity transaction coordinator
for 16 physical pads by 16 steps. It reuses `StockNoteRequest`, the 960-PPQ
timing model, the live pad-to-note resolver, and `NativeStockSequencePort`.

- Step ON creates velocity-100, 240-tick requests and commits the reducer bit
  only after one unique native observation.
- Step OFF is possible only for a cell whose canonical receipt the coordinator
  owns; it commits OFF only after verified absence.
- Missing receipts, concurrent operations, native errors, and ambiguous matches
  fail closed without optimistic UI changes.
- One verified Step-paint callback advances at most one command/observation
  boundary. No firmware pointer is retained.

Host tests cover tick mapping, delayed commit, exact receipt removal, delayed
absence, ambiguity, terminal native failure, and refusal to remove an
untracked active cell.

## Remaining execution-boundary blocker

The stable M0.3e overlay is painted on the stock Sample page. The verified
native sequence controller exists only in a visible stock Step Edit paint
callback. Those contexts are not simultaneously available.

The pinned SDK exposes worker tasks and Sample-page repainting, but no supported
UI-thread scheduler that can acquire a Step Edit context. `repaint_painted()`
requires the current live component on its owning UI thread; retaining a prior
Step screen or calling it from a worker/input thread would violate the proven
lifetime contract. A continuous repaint worker or speculative native call is
not an acceptable substitute.

Consequently the coordinator is not wired to physical PLUS yet, and no M0.5
device package is produced. The stable M0.3e behavior remains unchanged.

## Host validation

- 106 Python tests pass, including rapid removal-state polling regressions.
- 133 Rust workspace tests pass with all features.
- workspace formatting and Clippy with warnings denied pass;
- the pinned SDK patch-integrity checks and Nix flake check pass;
- the AArch64/QEMU fixture passes construction, native comparison,
  canonicalization, ownership/cleanup, duplicate rejection, context identity,
  exact removal, and ABI dispatch;
- the inspected AArch64 shared library is 625,320 bytes with SHA-256
  `9610b96cb91bfea8a8db109ee6944f1be594ba67faba9a322723066bb3ca7bee`;
  its linked ELF SHA-256 is
  `9bb0792edb1c9f1de750accc235539ac644041712d651c790692f11c269c97f7`.

These are host/QEMU results. They do not physically validate the M0.5
coordinator or create a runnable M0.5 package.

## Next integration experiment

The next code change must establish one legitimate shared UI context, preferably
by rendering the NEUROGENIC grid directly from the already-hooked Step Edit
paint callback and queuing only pointer-free input intents. It must demonstrate
how an idle Step screen is safely repainted without retaining its pointer or
calling firmware UI APIs from another thread. Only then should the four-on-the-
floor test insert A01 at ticks 0, 960, 1920, and 2880, remove tick 1920, and
verify the stock Step editor and audible transport result.

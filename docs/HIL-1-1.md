# HIL-1.1 — hardened Level B testing

Status: **host-validated; next hardware run pending**. The owner physically
validated HIL-1 on stock firmware 1.3.0.12: activation, restart/settle, a live
Level B `QUERY`, and stock restoration all succeeded. The observed initial
state was:

```text
v=1 open=0 track=1 step=1 bits=0000 flow=forward repaint=0 restore=0 pending_repaint=0
```

## Changes

The host no longer writes directly to the fixed request mailbox. It creates an
exclusive `request.lock` directory, then creates the controlled
`/run/neurogenic-hil/request.pending` with no-clobber semantics. It writes the
complete command, verifies its exact byte length, applies mode 0600, rechecks
that request and response are absent, and atomically renames it to `request`.
A POSIX/BusyBox-compatible trap removes the temporary file and lock on every
normal or trapped exit; stock restoration also removes those exact volatile
names. A stale lock, request, pending file, or response fails closed; none is
overwritten or silently discarded.

The reducer remains protected by a short-lived mutex and the worker still owns
all filesystem I/O. The loaded mod now mirrors only the overlay-open bit in an
atomic. If a physical callback collides with the worker's brief reducer update,
it never blocks and never silently forwards a control that the overlay owns:

- valid pads, DATA movement, PLUS, and owned releases fail closed while open;
- SHIFT+DATA fails closed while closed or open;
- unrelated buttons and controls continue forwarding.

The colliding event is deliberately not replayed or synthesized. This is the
smallest safe correction available without an SDK-supported UI-task dispatcher.
The SDK source reviewed for HIL-1 did not establish such an external dispatcher,
so HIL commands remain worker-thread reducer operations, not genuine SDK input
callbacks and not visual/audio assertions.

## Scenario

`test --level B --scenario` now runs exactly:

```text
RESET → OPEN → SELECT 4 → MOVE 3 → TOGGLE → QUERY → CLOSE → RESET
```

Each line reports the command, expected fields, actual complete reducer state,
and a pass Boolean. Invalid input, repeated commands, stale mailboxes, partial
publication, timeout, callback contention, and restoration have separate host
regressions.

## Next physical test

With explicit owner authorization, build/deploy the HIL test library, activate
its printed immutable hash once, run the scenario once, then run
`restore-stock`. Confirm every scenario line passes and normal stock UI, audio,
and controls return. Stop on a mailbox timeout, unexpected stock input, restart
failure, or restoration anomaly. No automatic retry is performed.

After that validation, HIL work should return to supporting the planned
stock-sequencer adapter rather than expanding the harness.

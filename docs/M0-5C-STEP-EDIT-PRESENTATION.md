# M0.5c Step Edit presentation replacement

Status: implemented, host/QEMU-tested, cross-built, and awaiting physical UI
validation on firmware 1.3.0.12.

## M0.5b result and cause boundary

The first guarded grid run installed the hook and observed 18 stock Step Edit
paint callbacks. Its final diagnostic remained `state=ready`, but reported zero
presentation activations and zero edit intents. Thus the Step hook and native
context worked; the SHIFT+DATA-dependent reducer transition did not occur. The
old counters cannot distinguish a missing Step-page button callback from a
different SHIFT event state, so M0.5c removes that activation dependency rather
than asserting an unsupported lower-level cause.

## Presentation model

The first valid stock Step Edit paint now establishes the copied context token,
opens the NEUROGENIC presentation directly, and draws the existing 320x240 grid
over stock output in the same callback. Stock paint still runs first. The stock
Step controller, sequence/event containers, command service, transport, and
audio engine are unchanged. No screen, graphics, sequence, track, Program, or
event-list pointer leaves the callback.

SHIFT+DATA now switches presentation only:

- NEUROGENIC is the default on first valid Step paint;
- SHIFT+DATA hides it and exposes the already-painted stock Step view;
- SHIFT+DATA returns to NEUROGENIC without clearing committed grid bits or
  canonical receipts.

The switch consumes its complete button pair. DATA turns move the NEUROGENIC
cursor but remain forwarded temporarily so stock schedules a legitimate Step
paint; native commands still execute only in that paint callback. This can also
move the hidden stock Step cursor, but does not itself create an event. PLUS and
physical-pad pairs are consumed while NEUROGENIC is visible; transport and
other controls forward.

## Editing and lifecycle safety

PLUS publishes one pointer-free cell intent. The existing coordinator inserts
or removes on the verified UI thread, requires unique presence or verified
absence, and commits the visible bit only after proof. Concurrent edits,
ambiguous identity, and changed sequence/track/Program/event-list context fail
closed. Follow-up repaint requests remain callback-local and capped at eight
observation attempts; the native operation guard rejects synchronous callback
reentry.

A visible SDK Sample-page paint deactivates the Step runtime. Deactivation
immediately stops input ownership and marks a reset even if a test worker holds
a reducer lock; the next Step paint clears stale context and receipts before
automatically opening a fresh presentation. The SDK has no general callback for
every unhooked stock page, so leave through the stock-view switch before other
navigation during this first test. No stale pointer is retained or dereferenced.

## Diagnostics and physical test

`GRID QUERY` v2 distinguishes hook installation (`hook`), Step paints
(`paint`), valid context (`ctx`), renderer calls (`draw`), accepted input
(`input`), queued intents (`intent`), verified insertion/observation/removal
(`ins`, `obs`, `rm`), invalidation (`inv`), and state. All are bounded counters
or bits; they contain no pointers or raw firmware data. A reported owner PASS
is rejected by the runner unless the hook/context and minimum edit evidence are
also present.

Run with the owner present:

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  grid-experiment \
  --confirm MPC-SAMPLE-1.3.0.12 \
  --confirm-grid PLAYABLE-GRID-ONE-BAR-1.3.0.12
```

Use a disposable empty one-bar sequence with A01 assigned. Enter Step Edit and
verify NEUROGENIC appears without a chord. Enable steps 1, 5, 9, and 13, using
DATA movement as the repaint handshake; verify four even audible hits. Remove
step 9 and verify three. SHIFT+DATA must expose stock Step Edit with ticks 0,
960, and 2880 present and 1920 absent. Enter PASS or FAIL once. The runner
collects diagnostics and restores stock in `finally`; physical success remains
unclaimed until this owner run passes.

## Artifact

The final reproducible hashes are recorded after the milestone cross-build:

| Artifact | SHA-256 |
|---|---|
| linked ELF | `556fd1d3b59986962ce925cfb0d8bdfd8f2bc53570dffe8c0af01cd7c409fa57` |
| stripped shared library | `9ed031e732fa6d0fc21f836fea582ca021b58296e5f63980d941a4315ea3d71d` |

The stripped library is 658,568 bytes. Two builds were byte-identical. Host
validation passed 114 Python tests and 140 Rust tests, Clippy with warnings
denied, formatting, both SDK patch-integrity checks, Nix flake checks, the
AArch64/QEMU ABI fixture, shared-library inspection, and the separate M0.3e
rollback build/package inspection. These checks do not prove physical display,
input, or audio behavior.

The stripped AArch64 library is 658,568 bytes. Static inspection passed and
found no prohibited shell, service-control, or network-listener imports.

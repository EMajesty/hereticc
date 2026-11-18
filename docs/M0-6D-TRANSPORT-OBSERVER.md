# M0.6d native transport observer

## Result and boundary

M0.6d implements a firmware-1.3.0.12-only, read-only observer for the stock
`NowPosition` timer callback. It is host tested and cross-built, but has not
been run on an MPC Sample. It does not edit events, schedule notes, operate the
transport, identify the playing sequence, or establish the playback engine's
event-consumption horizon. Live window replacement remains no-go.

The implementation is an isolated patch over MPC Sample SDK v0.7.1 revision
`9487cbba3648afb29d8e3107e31a7ac930985220`. The observer mechanism is adapted
from MIT-licensed Am MOD revision
`6cdc67276f43cd39f64871cb7e36782481244c65`. No broader Am MOD patching
framework is imported.

## Revalidated firmware contract

| Contract | Value | Pinned Am MOD evidence |
|---|---:|---|
| `NowPosition` entry | `0x2109a90` | `sd-mod-session/phone_build.py`, `NOW_TICK` |
| timer vtable | `0x6da4588` | `src/step_native.c`, `NOW_VPTR` |
| timer to engine | `+0x258` | `step_native_clock()` |
| engine tick getter | `0x2113b10` | `step_native_clock()` |
| engine to transport facade | `+0x768` | `step_native_clock()` |
| transport to playing interface | `+0x148` | `step_native_clock()` |
| `isPlaying` vtable slot | `+0x18` | `step_native_clock()` |

Am MOD's `phone_native.c::now_tick()` calls the displaced stock callback before
observing the timer. The SDK patch preserves that ordering. The new hook has a
`PACIASP` first-word fingerprint and is prepared, sealed, and committed in the
SDK's existing all-or-nothing startup transaction. It is not installed late
from `Mod::on_start`.

These offsets and layouts are private firmware evidence, not a stable public
SDK contract. The patched loader refuses an unexpected first instruction, and
the callback rejects the wrong timer vtable, missing objects, vtables outside
the app image, or an `isPlaying` method outside the app image. Exact executable
fingerprint validation remains the outer compatibility gate.

## Published observation

The callback forwards stock behavior first, borrows the object graph only for
that callback, then atomically publishes copied scalars:

- native 960-PPQ tick and running/stopped state;
- `CLOCK_MONOTONIC` observation time in milliseconds;
- callback, valid-observation, transition, and reentrant-drop counters;
- an observer-local serial, explicitly **not** a stock sequence generation;
- sticky started, stopped, backward-tick, stopped-movement, and invalid flags.

It publishes no firmware pointers. Callback work is bounded, allocation-free,
nonblocking, and performs no file or network I/O. A nonblocking write guard
drops reentrant observation while leaving the original callback forwarded.
Readers make at most eight attempts to obtain one coherent atomic snapshot.

Raw object dereferences still rely on the verified callback entry and exact
timer vtable: userspace pointer checks cannot prove an arbitrary address mapped
without inappropriate callback-time OS I/O. A corrupted live object graph could
therefore fault before a later guard. This remains a firmware risk, not
something host tests can eliminate.

## Guarded physical experiment

The test-only HIL bridge adds one exact command, `CLOCK QUERY`, with a bounded
ASCII response and no transport-changing operation. The dedicated runner uses
the existing immutable volatile deployment, host lock, identity checks,
structured reporting, and `finally` restoration. It requires the distinct
confirmation `READ-ONLY-TRANSPORT-OBSERVER-1.3.0.12`.

The owner-operated phases are stopped, start/loop, stop, continue, tempo
change, and sequence change. The runner verifies valid observation, tick
progression, a backward loop transition, and running/stopped transitions. It
records sequence-change behavior but does not invent a sequence identity.

Proposed command (not executed in this milestone):

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  transport-experiment \
  --confirm MPC-SAMPLE-1.3.0.12 \
  --confirm-transport READ-ONLY-TRANSPORT-OBSERVER-1.3.0.12
```

The experiment performs no native edits, but it still installs a new callback
hook in the running stock process. Physical execution remains an owner-reviewed
operation.

## Validation and limitations

- Patched SDK: 126 unit tests and 1 doctest pass, including original forwarding,
  transitions/discontinuities, invalid publication, and concurrent readers.
- HIL parser/orchestration: strict schema and bounds, distinct authorization,
  transition evidence, failure reporting, and restoration are host tested.
- The patch applies reproducibly to the exact pinned SDK revision.
- The cross-built output is a stripped AArch64 shared object; strict HIL
  inspection passes and no load segment is both writable and executable.
- `virtual-mpc-sample` was not run because no authorized proprietary stock
  firmware inputs are present locally. Emulator timing would not prove physical
  timing accuracy.

A physical pass would establish that the callback supplies a useful stock tick
across the requested transitions. It would not establish sequence identity or
how far ahead stock playback consumes events. The next justified step after a
pass is a separately reviewed, objective, bounded queue-horizon measurement;
the observer experiment itself does not authorize event mutation.

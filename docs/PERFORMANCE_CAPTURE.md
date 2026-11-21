# Performance capture

Performance capture records activation acknowledgements from the performance
launcher into the existing song arrangement. It does not introduce another
transport or clip format.

## Timing and follow actions

Slot and scene requests are quantized by `PerformanceLauncher`. The audio engine
acknowledges the actual activation frame; capture stores that frame with the
authoritative musical tick, source, scene, resolved velocity, and follow flag.
Canceled requests therefore create no arrangement data.

Stop, repeat, next, previous, specified-slot, and seeded-random follow actions
prepare one bounded transition per acknowledgement. The resolved target is
captured, so playback never repeats a random decision. Follow processing does
not recurse in the callback.

## Arrangement and history

Pattern activations become pattern clips. Audio and completed live-loop
activations become audio clips retaining immutable sample/region references. A
later activation or stop closes the prior clip on that track; capture stop closes
remaining clips. Replace removes overlapping clips and automation, while
overdub preserves them. Commit is one validated timeline transaction and is
undoable/redoable.

## Hybrid ownership

Session launching owns tracks whose slots have activated. Partial scenes replace
only listed session tracks; other active session tracks continue. Stop releases a
track. The current conservative publication uses a session plan while launching
is active; arbitrary merging with arrangement events on the same track remains
unsupported.

## API, safety, and limits

Use `performance.capture-{arm,start,stop,commit,discard}.v1`, scene actions under
`performance.scene-*.v1`, and `performance.slot-follow-set.v1`. State exposes
pending commands, diagnostics, capture counts, and acknowledged launch history.
Overflow is explicit and prevents a successful commit.

The callback only activates prepared plans and emits bounded acknowledgements.
Follow preparation, capture storage, edits, validation, persistence, and
reclamation are control-side. Follow frames use the tempo in effect during
preparation; physical MIDI transmission latency is outside this guarantee.

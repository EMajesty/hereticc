# Unified performance launching

Acknowledged launches and resolved follow transitions can be recorded through
the arrangement recorder; see [Performance capture](PERFORMANCE_CAPTURE.md).

Task 062 adds a presentation-neutral launch coordinator above the existing
timeline compiler, pattern materializer and audio-clip voices. It does not add
another clip engine. Definitions are project data; queued commands and active
voices remain transient runtime state.

## Model and identity

A launch slot has a stable 64-bit ID independent of its source and any UI
position. It names a track and one pattern, arrangement audio clip, or completed
live-loop clip. Behaviours are one-shot, toggle, gate, loop, retrigger and
legato. Legato requests preserve source phase where the existing source path
supports it. Slot definitions contain optional per-slot quantization and a
follow action.

Performance scenes have stable IDs and at most one slot per track. Preparation
validates every entry and emits a bounded batch sharing one activation tick and
frame. This guarantees coordinated callback-boundary activation, not physical
sample accuracy beyond the selected backend buffer.

Limits are 256 slots, 64 scenes, 64 entries per scene and 128 pending launch
operations. Capacity failure leaves the active timeline unchanged.

## Timing and playback

Quantization supports immediate, quarter-note, bar, two/four/eight bars,
pattern end and a positive tick subdivision. Integer musical boundaries use the
authoritative transport; external MIDI synchronization therefore follows the
same position and tempo estimate as pattern playback. A late command never
activates before the next valid boundary.

The persistent mode is arrangement, session or hybrid. Semantic launches
publish prepared session plans through `queue_timeline_at`. Pattern sources
become ordinary pattern clips and audio/live-loop sources become ordinary audio
clips, preserving track FX, group/aux routing, stretch, fades, immutable sample
ownership and recording taps. General merging into a song already in flight is
currently conservative and documented as deferred.

A completed live loop keeps its reserved Task 061 gapless handoff. Launcher
assignment is not on that critical path; subsequent launches use its ordinary
audio-clip identity.

## Follow actions and velocity

Follow actions are stop, repeat, next, previous, specified slot and seeded
random eligible slot. Selection is deterministic for a seed and repetition.

Velocity policy is independent of launch identity. `native` preserves
velocity-capable input and uses the configured fallback for non-velocity
controls; `fixed` uses that 0–1 value; `full-level` and the optional Full Level
override produce 1.0. No pad geometry or dedicated physical key is assumed.

## API, persistence and real-time rules

Versioned actions include `performance.slot-assign.v1`, `slot-clear`,
`slot-launch`, `slot-stop`, `scene-launch` and `velocity-policy`. Discovery
exports launch-slot/scene objects and capacities. Revisioned state contains
definitions, prepared operations, mode, policy and rejection diagnostics.

Timeline format 10 persists definitions, scenes and preferences, but never
queued operations or voices. Older timelines load with an empty launcher in
arrangement mode. Validation, scene preparation and persistence stay off the
audio thread; playback uses only existing prepared timeline publication.

Current limitations: scene editing has no dedicated semantic action, follow
selection is not yet auto-committed, launch gestures are not yet arrangement-
recorded, and arbitrary hybrid song/session merging remains deferred.

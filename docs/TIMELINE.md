# Musical timeline

The editable model in `neurogenic/timeline/model.hpp` is shared musical data,
not a step-sequencer, tracker, piano-roll, or controller model. Positions and
durations use signed 64-bit ticks at 960 ticks per quarter note. Pattern length
is arbitrary and each track loops independently. Tempo is currently one integer
microseconds-per-quarter value; the transport also carries time-signature
metadata. A later tempo map can replace the single tempo without changing event
positions.

Events have stable 64-bit identifiers and support duration notes, explicit
note-on/note-off, and stable 32-bit parameter identifiers with values. Tracks
have stable 64-bit identifiers, mute state, and one initial looping pattern. A
track is an event container, not an instrument, sample slot, or MIDI channel.
At equal frame positions playback orders by track ID, note-off before note-on
within a track, then event ID.

On the control thread, `compile_timeline` validates IDs, ranges and lengths,
expands duration notes into on/off pairs, and produces dynamically sized data
with audio-owned cursor storage. Plan allocation and reclamation remain on the control thread. The
audio thread accepts plan-slot indices through a bounded SPSC queue and performs
only integer scheduling, preallocated heap/buffer operations, voice updates,
and rendering. It does not allocate, free, lock, perform I/O, or execute Lua.
The configurable callback staging capacity defaults to 512 events. If full,
the earliest undispatched cursor remains pending and is delivered at frame zero
of a later callback; the overrun counter records the timing degradation. No
note-off is silently discarded. Plan-slot exhaustion rejects the newest plan.
Retired slots are destroyed only by the control thread or after audio shutdown.

Tick-to-frame conversion evaluates the absolute rational position:

```text
round(tick * sample_rate * microseconds_per_quarter
      / (960 * 1,000,000))
```

It never accumulates a rounded frames-per-step value, so loop and callback
boundaries do not introduce cumulative drift. Each track's pattern length is
its loop period. Note durations may cross a loop boundary; their generated
note-off remains attached to the originating track, stable note-on ID, and loop
occurrence. Overlapping same-pitch instances therefore remain independent.
Voice stealing makes a later note-off a no-op if its exact owner no longer
exists; there is no pitch-based fallback.

## Version 1 interchange

`NEUROGENIC_TIMELINE 1` is a narrow text interchange format, not the final
project format. Parsing and validation happen before playback. Blank lines and
lines beginning with `#` are ignored. Records are:

```text
NEUROGENIC_TIMELINE 1
transport MICROSECONDS_PER_QUARTER NUMERATOR DENOMINATOR
track TRACK_ID LENGTH_TICKS MUTED_0_OR_1 "NAME"
note TRACK_ID EVENT_ID TICK PITCH VELOCITY DURATION_TICKS
note_on TRACK_ID EVENT_ID TICK PITCH VELOCITY
note_off TRACK_ID EVENT_ID TICK PITCH VELOCITY TARGET_NOTE_ON_ID
parameter TRACK_ID EVENT_ID TICK PARAMETER_ID VALUE
```

Tracks precede their events. IDs are positive and unique in their scope, and
event positions fall within the owning pattern. Sample assignments remain
separate application configuration. `examples/multitrack.ngt` is a complete
three-track example selecting slots through notes 36, 37, and 38.

An explicit `note_off` has its own stable event ID and must name the stable ID
of its corresponding `note_on`. Duration notes generate this relationship
automatically. For compatibility, a version 1 file may omit the final target
only when exactly one preceding, not-yet-targeted note-on has the same pitch;
the loader resolves it before compilation. Ambiguous or unknown note-offs are
rejected, and saved files always include the explicit target.

## Transport behavior

The audio engine owns one frame position and play state. Timeline and legacy
Lua patterns receive the same frame range and tempo. Stop freezes position and
cleans active voices. Play continues from that position. Restart cleans voices
and starts at tick/frame zero. Each track loops independently. Replacing a
timeline keeps the current position, adopts new tempo metadata at the callback
boundary, and cleans old timeline voices. This deterministic segment-boundary
policy leaves room for a tempo-map transport and MIDI-clock position/tempo
commands without moving clock ownership into either scheduler.

The existing Lua `neurogenic.v1` API remains a 16-step compatibility surface.
Its next-bar replacement scheduler is retained, and `adapt_step_pattern`
converts that data to the general timeline for future callers. MIDI controller
actions remain in the mapping/control layer: demo note 36 can launch Lua, but
that mapping is not part of the musical event model.

Current limitations: one pattern plays per track, tempo is constant within a
plan, parameter events are scheduled but have no destination yet, and timeline
replacement takes effect at the next callback rather than a musical boundary.
Callback staging is finite and observable. Arrangement, recording, editing
interfaces, tempo maps, and asynchronous sample replacement are future work.

## Editing and generation service

`TimelineService` exposes hardware-independent snapshots and working-copy
transactions. A transaction contains the authoritative `Timeline` value, its
base revision, and a reproducible generation seed. Track/event creation,
deletion, naming, mute, loop length, notes, tempo, and time signature are C++
operations. The same service supplies deterministic RNG, Euclidean generation,
and timing/pitch/velocity transformations to Lua and future native clients.

Commit performs limit checks and `compile_timeline` validation before calling
the publication callback. It rejects stale base revisions, so concurrent
controllers must refresh and intentionally reapply their edit. Successful
commits replace the editable snapshot only after the complete playback plan is
accepted. Rapid commits are bounded by the existing playback-plan pool; a full
pool rejects the newest commit without changing editable state.

## Step editing adapter

`ApplicationService` maps step actions onto timeline transactions instead of
maintaining another pattern. Resolutions are 1/4, 1/8, 1/16 (default), 1/32
and their triplet forms, represented as exact 960-PPQ tick counts. Step `n`
starts at `n * resolution`. A non-divisible track length exposes the final
start tick inside the pattern and rejects the following step.

Each track has an application-level MIDI note target, defaulting to notes 36,
37, and so on by track order. Toggling removes the matching duration note at
that tick and pitch, or inserts one with a new stable ID, velocity and duration.
Unrelated events and tracks retain their identifiers. Commits become audible
at the next callback; validation, conflicts and publication failures leave the
prior timeline intact. Note-target project persistence and musical-boundary
publication remain future work.

Application adapters use stable semantic action IDs
`timeline.generate.v1`, `timeline.mutate-track.v1`,
`timeline.regenerate.v1`, `timeline.variation.v1`, `timeline.commit.v1`,
`timeline.discard.v1`, and `timeline.query.v1`. These identify controller/UI
intent; parameters and selection context belong to the calling adapter and the
shared C++ service, not to a physical control layout or Lua script.

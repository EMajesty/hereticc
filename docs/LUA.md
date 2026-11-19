# Lua API

Lua 5.4 runs through sol2 on the control thread, never in the audio callback.
Lua is one client of the same C++ `TimelineService` used by future hardware,
touchscreen, desktop, MIDI, and remote adapters; no normal application command
requires Lua. Editing, validation, revisions, deterministic randomness,
transformations, generation, and publication are implemented in C++.

## Timeline working copies

`neurogenic.v1.timeline.current()` returns a detached working copy of the
current authoritative timeline. Changes remain private until `commit()`.
`commit()` stages the copy, and the runtime publishes it only after the complete
script returns successfully. A Lua error after `commit()` therefore discards
the staged change. C++ clients use the equivalent `TimelineService::edit()` and
`TimelineService::commit()` API directly.

Each working copy records its base revision. A commit fails with
`timeline revision conflict` if another client committed first; edits are never
silently merged or overwritten. A successful commit increments the revision,
stores its generation seed, compiles a complete playback plan, and queues that
plan through `AudioEngine::queue_timeline`. It becomes active at the next audio
callback boundary. The prior plan remains active if validation or queueing
fails. Timeline replacement retains the established behavior of cleaning old
timeline-owned voices at activation; musical-boundary commits are not yet
implemented.

```lua
local project = neurogenic.v1.timeline.current()
local drums = project:create_track(1, "Drums", 960 * 4)
drums:add_note {
    tick = 0, pitch = 36, velocity = 1.0, duration = 240
}
project:set_tempo(120)
project:set_time_signature(4, 4)
project:set_seed(12345)
project:commit()
```

## Project API

- `project:revision()` and `project:seed()` read the working copy metadata.
- `project:set_seed(seed)` stores a reproducibility seed with the commit.
- `project:tempo()` and `project:set_tempo(bpm)` read or set constant tempo.
- `project:time_signature()` returns numerator and denominator.
- `project:set_time_signature(numerator, denominator)` accepts a power-of-two
  denominator.
- `project:create_track(id_or_nil, name, length_ticks)` creates a track and its
  single looping pattern. `nil` allocates the next stable positive track ID.
- `project:track(id)`, `project:tracks()`, and `project:delete_track(id)` query
  or edit tracks.
- `project:commit()` stages one validated commit. At most one commit is allowed
  per script execution.

The initial timeline model has one looping pattern per track. Its length is
arbitrary; the model does not impose 16 steps or 4/4.

## Track and event API

- `track.id`, `track:name()`, `track:set_name(name)`
- `track:length()`, `track:set_length(ticks)`
- `track:muted()`, `track:set_muted(boolean)`
- `track:events()` returns event tables with `id`, `type`, `tick`, `pitch`,
  `velocity`, and `duration`.
- `track:add_note{ id=?, tick=, pitch=, velocity=, duration= }` returns the
  stable event ID. Omitting or setting `id=0` allocates the next stable ID.
- `track:modify_note(id, note_table)` and `track:remove_event(id)` mutate or
  remove events.
- `track:add_parameter{ id=?, tick=, parameter_id=, value= }` and
  `track:modify_parameter(id, table)` edit extensible parameter events.

Pitch is 0..127, velocity is 0..1, duration is positive, and an event position
must be within its track loop. Argument errors abort the script transaction.

## Transformations and generators

The following methods call reusable C++ operations:

- `track:transpose(semitones)` rejects a result outside MIDI pitch range.
- `track:shift(ticks)` and `track:rotate(ticks)` wrap event positions modulo
  pattern length. Note durations are unchanged and may cross the loop boundary.
- `track:scale_velocities(factor)` clamps results to 0..1.
- `track:quantize(grid_ticks)` uses nearest-grid quantization, choosing the
  later point on an exact tie, then wraps at the pattern boundary.
- `track:humanize(rng, max_tick_delta, max_velocity_delta)` wraps timing and
  clamps velocity.
- `track:reverse()` maps each position to `(length - tick) mod length`.
- `track:duplicate{offset1, ...}` copies events with new stable IDs. Explicit
  note-off targets are remapped with their duplicated note-ons.
- `track:generate_euclidean(steps, pulses, rotation, pitch, velocity, duration)`
  resolves a Euclidean rhythm into ordinary duration-note events at edit time.

All transformations operate on editable musical data and are composable without
Lua through `neurogenic/timeline/edit.hpp`.

## Deterministic randomness

`neurogenic.random(seed)` creates an independent SplitMix64 stream. The stream
does not use global process randomness. Equal seeds and equal call sequences
produce equal results on x86-64 and AArch64.

```lua
local rng = neurogenic.random(12345)
if rng:chance(0.65) then
    local note = rng:choose { 36, 38, 42, 46 }
    local velocity = rng:integer(60, 100) / 100
end
```

Store the input seed with `project:set_seed(seed)` to reproduce a generation.
Changing the seed while retaining the algorithm creates a deterministic
variation.

## Legacy API and safety limits

The original API remains compatible:

```lua
local frame = neurogenic.v1.transport.frame()
local p = neurogenic.v1.pattern.new()
p:set_step(1, true, 36, 1.0)
neurogenic.v1.pattern.queue(p) -- next legacy bar boundary
```

Only base, math, table, and string libraries are opened. Timeline commits are
limited by default to 256 tracks and 65,536 events, Euclidean generation to
4,096 steps, and one staged commit per execution. An instruction hook stops a
script after approximately ten million Lua VM instructions. These safeguards
protect application responsiveness and memory bounds but are not a security
sandbox for hostile scripts.

Examples:

- [`timeline_drums.lua`](../examples/timeline_drums.lua)
- [`timeline_mutate.lua`](../examples/timeline_mutate.lua)
- [`timeline_polyrhythm.lua`](../examples/timeline_polyrhythm.lua)
- [`timeline_variation.lua`](../examples/timeline_variation.lua)

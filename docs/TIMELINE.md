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

On the control thread, `compile_timeline` validates IDs, ranges, lengths and
capacities, expands duration notes into on/off pairs, and produces fixed-size
real-time data. The audio thread accepts those plans through a bounded SPSC
queue and performs only integer scheduling, fixed-array sorting, voice state
updates, and rendering. It does not allocate, lock, perform I/O, or execute Lua.
Overflow drops the newest plan or excess scheduled event and increments an
observable counter.

Tick-to-frame conversion evaluates the absolute rational position:

```text
round(tick * sample_rate * microseconds_per_quarter
      / (960 * 1,000,000))
```

It never accumulates a rounded frames-per-step value, so loop and callback
boundaries do not introduce cumulative drift. Each track's pattern length is
its loop period. Note durations may cross a loop boundary; their generated
note-off remains attached to the originating track and pitch.

The existing Lua `neurogenic.v1` API remains a 16-step compatibility surface.
Its next-bar replacement scheduler is retained, and `adapt_step_pattern`
converts that data to the general timeline for future callers. MIDI controller
actions remain in the mapping/control layer: demo note 36 can launch Lua, but
that mapping is not part of the musical event model.

Current limitations: one pattern plays per track, tempo is constant, parameter
events are scheduled but have no parameter destination yet, timeline replacement
takes effect at the next callback rather than a musical quantization boundary,
and real-time compiled capacities are fixed. Arrangement, recording, editing
interfaces, tempo maps, and asynchronous sample replacement are future work.

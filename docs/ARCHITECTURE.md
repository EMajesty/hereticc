# Architecture

The control thread owns files, projects, scripts, mappings, and editable state.
It compiles edits into bounded pattern snapshots. The audio thread consumes
only fixed-size events and sample memory prepared in advance.

```text
CLI/UI -> command API -> project / Lua / MIDI mapping
                         | boundary-safe Pattern snapshot
                         v
musical scheduler -> frame-offset events -> sample voice -> DSP -> backend
                                                    \-> offline WAV
```

- `core`: stable command identifiers and application-domain boundary.
- `sequencer`: integer frame clock, deterministic ordering, next-bar swaps.
- `audio`: prepared sample voices, miniaudio playback adapter, and offline renderer.
- `midi`: timestamped messages, stable action mappings, simulated input, and a
  libremidi adapter. MIDI timing does not drive the musical clock.
- `scripting`: genuine Lua 5.4/sol2 `neurogenic.v1` API on a non-audio thread.
- `dsp`: allocation-free effect interface and first gain stage.
- `project`: deliberately tiny versioned format; not a promised final schema.

No callback operation performs I/O, locks, allocation, logging, or Lua work.
Trigger and pattern snapshots cross into audio through bounded SPSC queues.
Overflow drops the newest item and increments an atomic diagnostic counter.
The audio callback drains pattern snapshots, consumes trigger commands, and
schedules events into fixed stack storage. It never executes Lua or MIDI code.

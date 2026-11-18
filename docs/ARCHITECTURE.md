# Architecture

The control thread owns files, projects, scripts, mappings, editable state, and
immutable compiled timeline plans. The audio thread consumes only plan pointers,
bounded event staging, and sample memory prepared in advance.

```text
CLI/UI -> command API -> project / Lua / MIDI mapping
                         | compiled, bounded PlaybackPlan snapshot
                         v
authoritative transport -> scheduler frame range -> bounded voice pool -> DSP -> backend
                                                    \-> offline WAV
```

- `core`: stable command identifiers and application-domain boundary.
- `timeline`: UI-independent tick events, tracks, arbitrary loops, transport
  metadata, compilation, and sample-accurate playback.
- `sequencer`: the legacy 16-step compatibility scheduler and adapter, including
  next-bar Lua pattern swaps.
- `audio`: prepared sample voices, miniaudio playback adapter, and offline renderer.
- `midi`: timestamped messages, stable action mappings, simulated input, and a
  libremidi adapter. MIDI timing does not drive the musical clock.
- `scripting`: genuine Lua 5.4/sol2 `neurogenic.v1` API on a non-audio thread.
- `dsp`: allocation-free effect interface and first gain stage.
- `project`: deliberately tiny versioned format; not a promised final schema.

No callback operation performs I/O, locks, allocation, logging, or Lua work.
Pattern snapshots and control-thread triggers cross into audio through bounded
SPSC queues. The producer drops the newest item on overflow and increments an
atomic diagnostic counter. Each callback drains every trigger that fits its
fixed 64-event staging area, orders triggers by frame offset and enqueue order,
and carries future offsets into later buffers. At equal offsets, external
control events precede sequencer events.

Sample slots use stable 32-bit identifiers. Slot storage is dynamically sized;
16 is only the initial note-bank policy, mapping MIDI/sequencer notes 36–51 to
slots 0–15. Embedders can install any number of slots and map any note before
processing starts. The positional single-WAV compatibility API maps all notes
to slot 0. Each voice stores the resolved immutable sample index, so callback
lookup performs no string work, allocation, locking, decoding, or I/O.

The voice pool is allocated when `AudioEngine` is constructed (32 voices by
default, configurable by the embedding application). Allocation chooses the
lowest-index idle voice. If all voices are active, it steals the oldest-started
voice, with the lowest index breaking ties. Retriggering therefore never resets
an unrelated voice unless the configured pool is exhausted. The stolen-voice
counter exposes that overload condition.

The live MIDI callback validates note-on messages and pushes them to a 256-item
SPSC inbox; it does not call mappings, the engine, Lua, logging, or filesystem
code. A dedicated `ControlDispatcher` thread drains that inbox, invokes note
and action mappings, and drains accepted events before shutdown. Shutdown first
closes the MIDI producer, then drains and joins the dispatcher. Mapping setup
belongs to the control thread before dispatch starts. That dispatcher is the
sole producer for the audio trigger queue; the audio callback is its sole
consumer. The callback never executes Lua or MIDI code.

Editable timelines own strings and vectors on the control side. `compile_timeline`
validates stable identifiers and converts note durations into explicit on/off
events in a preparation-time-allocated `PlaybackPlan`. There is no fixed track
or compiled-event count. Four control-owned plan slots have atomic
free/queued/active/retired states. The callback only changes states and pointers;
the control thread alone destroys retired plans, so queue saturation cannot
leak a plan or invoke a destructor in real time. Scheduling uses a configurable
preallocated staging buffer (512 events by default). Overflow defers the next
event without advancing its cursor and increments an overrun counter. See
[TIMELINE.md](TIMELINE.md).

Each compiled track has audio-owned cursor state, and a preparation-time-sized heap contains
one next-event entry per audible track. A continuous callback visits the heap
root and due events rather than scanning the pattern. Steady-state scheduling is
`O(D (log T + log D) + 1)`, where `T` is playing tracks and `D` is dispatched
events; stored events far from the playhead do not add callback work. Seek,
restart, and plan activation rebuild cursors in `O(T log E + T)`.

Timeline voices are owned by `(track ID, note-on event ID, loop occurrence)`.
Generated duration note-offs preserve that token across loop boundaries.
Explicit note-offs name a target note-on ID. Voice stealing removes only the
selected voice; its later note-off finds no owner and cannot stop another voice.

`AudioEngine` owns the authoritative transport frame and play state. Both the
timeline and 16-step compatibility schedulers receive the same frame range and
tempo; neither owns a clock. Stop freezes position and cleans all voices, play
resumes, and restart cleans voices and returns to zero. Timeline replacement
preserves position and cleans timeline-owned voices. Plan destruction and final
reclamation happen only after the audio device has stopped.

Sample-slot memory and note mappings are installed before processing begins and remain immutable for
the lifetime of live processing. Attempts to replace it after the first audio
buffer are rejected, avoiding concurrent vector mutation or reclamation in the
real-time callback. The install API is a distinct preparation boundary so a
future snapshot-based asynchronous replacement mechanism can preserve the
rendering interface.

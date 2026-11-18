# Architecture

The control thread owns files, projects, scripts, mappings, and editable state.
It compiles edits into bounded pattern snapshots. The audio thread consumes
only fixed-size events and sample memory prepared in advance.

```text
CLI/UI -> command API -> project / Lua / MIDI mapping
                         | compiled, bounded PlaybackPlan snapshot
                         v
timeline scheduler -> frame-offset events -> bounded voice pool -> DSP -> backend
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
events in a bounded `PlaybackPlan` (32 tracks and 512 compiled events in this
initial real-time implementation). The control thread is the sole plan-queue
producer; the audio callback is its sole consumer. The callback schedules into
fixed storage and drops excess events with a counter. These are real-time
capacities, not musical-data or UI limits. See [TIMELINE.md](TIMELINE.md).

Sample-slot memory and note mappings are installed before processing begins and remain immutable for
the lifetime of live processing. Attempts to replace it after the first audio
buffer are rejected, avoiding concurrent vector mutation or reclamation in the
real-time callback. The install API is a distinct preparation boundary so a
future snapshot-based asynchronous replacement mechanism can preserve the
rendering interface.

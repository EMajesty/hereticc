# Architecture

The control thread owns files, projects, scripts, mappings, editable state, and
immutable compiled timeline plans. The audio thread consumes only plan pointers,
bounded event staging, and sample memory prepared in advance.

```text
clients -> application actions / parameters / state -> timeline / sample manager
                         | compiled, bounded PlaybackPlan snapshot
                         v
authoritative transport -> scheduler frame range -> bounded voice pool -> DSP -> backend
                                                    \-> offline WAV
```

- `core`: stable command identifiers and application-domain boundary.
- `application`: typed semantic actions and parameters, bounded ordered command
  processing, immutable state snapshots, controller capabilities/mappings,
  feedback, grid/navigation/text, and X/Y interaction models.
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

Sample slots use stable 32-bit identifiers. The musical slot namespace is not
limited to a fixed bank; 16 is only the initial note-bank policy, mapping
MIDI/sequencer notes 36–51 to
slots 0–15. Embedders can install any number of slots and map any note before
processing starts. The positional single-WAV compatibility API maps all notes
to slot 0. Each voice stores the resolved immutable sample-version pointer and index, so callback
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

`SampleManager` owns a bounded asynchronous command queue and one preparation
worker. WAV I/O, decoding, resampling, allocation, status locking, and errors
remain on that worker/control side. It publishes immutable samples through a
bounded version pool. The callback swaps stable-slot resolution, lets voices
retain old versions, and only marks unreferenced versions retired. Destruction
and pool reuse occur on the control side. Unload follows the same rule: new
triggers see an empty slot while existing voices finish. Publication overload
is rejected and counted rather than blocking or freeing memory in the callback.
Monotonic publication sequences acknowledge callback activation back to
`SampleManager`. Frontend snapshots keep audio-active metadata separate from
the newest pending request, so stale preparation completions cannot overwrite a
newer command's visible revision or status.
See [SAMPLES.md](SAMPLES.md).

## CPU and ABI portability

The engine uses standard C++20 and C11 without an x86 SIMD baseline. No target
enables AVX, AVX2, SSE, `-march=native`, or architecture-specific intrinsics.
Any future optimized DSP path must be optional, selected from supported runtime
CPU features, and retain a portable implementation as the default/fallback.

Timeline and legacy scheduler products use compiler-provided 128-bit integers
to avoid cumulative timing drift and overflow. GCC and Clang provide this type
on both supported 64-bit Linux targets. Public musical and serialized values
use fixed-width integer types; container indices remain process-local
`std::size_t`. The supported AArch64 target is little-endian Linux, matching
the RIFF/WAVE byte order used by the current decoder.

## Application editing services

Hardware, touchscreen, desktop, Lua, MIDI, and remote clients sit above shared
C++ command/query/state services. `TimelineService` is the first complete
musical editing service: it returns detached transactions and observable
snapshots, owns optimistic revision checks, validates through the authoritative
timeline compiler, and publishes complete plans through the existing bounded
audio-engine path. Built-in generators, deterministic RNG, and transformations
are C++ operations. Lua contains binding and transaction-lifetime glue only.

No editor holds a playback-plan pointer. The application/control side may copy,
allocate, mutate, compile, or reject timeline data. The callback sees only a
previously compiled immutable plan-slot index, never Lua or an editable object.

`ApplicationService` is the client-neutral facade over transport,
`TimelineService`, and `SampleManager`. Its action registry exposes stable typed
commands and honest availability; its parameter registry owns generic control
metadata; its snapshots expose copied state with application and subsystem
revisions. A bounded multi-producer queue assigns deterministic sequence
numbers and rejects newest commands on overflow. Controller adapters resolve
capability profiles into those commands, and revision-tagged feedback flows
back without a controller directly touching engine state. See
[CONTROLLERS.md](CONTROLLERS.md).

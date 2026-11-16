# Sequencer design

## Status

This is the agreed musical model. M0.2a implements a minimal host-testable subset: sixteen tracks, reusable independent patterns, scenes, note/parameter events, deterministic scheduling, overlapping voices, one scene-transition policy, validation, a bounded versioned binary round trip, and a host event-consumer trace. The remaining behavior is design-only. Numeric musical limits other than sixteen tracks remain provisional until tests and device measurements justify them.

## Model

### Track

There are sixteen independent polyphonic tracks, corresponding to the sixteen physical pads. A track owns:

- a default pad-bound sound reference;
- a set of patterns;
- track-level defaults and performance overrides;
- mute/solo and MIDI routing state.

The physical-pad correspondence is a control and default-sound relationship, not a one-sample restriction. Events may select another sample or a slice non-destructively.

### Pattern

A pattern belongs to exactly one track and has its own length, step/grid settings, event collection, and optional loop behavior. Patterns do not need equal lengths. While a scene plays, each selected track pattern wraps independently; this is deliberate polymeter.

Pattern identity is stable and separate from its display name or list position. Editing a pattern affects every scene that references it unless the user explicitly duplicates it.

### Scene

A scene is a set of sixteen references—one pattern selection per track—plus optional scene-level transition metadata. A scene does not copy pattern events. Missing/disabled references are silent for that track.

### Song

A song is an ordered arrangement of scene entries with repeat counts and transition rules. Manual scene launching and song playback use the same transition engine.

## Time and scheduling

The core uses integer musical time, not floating-point seconds. M0.2 provisionally uses **960 PPQN** because it represents common binary and triplet subdivisions with useful edit resolution. This is a project-format design choice, not a measured or guaranteed MPC Sample clock. Events are stored at an integer tick relative to their track pattern. Conversion to seconds/device frames is deferred to a runtime adapter.

M0.2a supports checked absolute time, positive pattern lengths, pattern-relative ticks, exact modular wrapping, and half-open intervals `[start, end)`. Scheduling windows must be contiguous for a stateful scheduler. Occurrence lookup uses quotient/remainder arithmetic rather than overflow-prone ceiling addition or `cycle * length` reconstruction. A next mathematical occurrence beyond `u64` is outside the representable timeline, while a note end or emitted timestamp that cannot be represented is an explicit transactional error. There is no floating-point accumulation or wall-clock timing.

Tempo is global initially. Tempo automation and external clock following are deferred until the base clock contract is measured.

The scheduler uses bounded look-ahead and deterministic ordering. At the same timestamp, the provisional precedence is:

1. transport/scene transition;
2. note termination and choke/mute actions;
3. event parameter locks and sample/slice selection;
4. note starts and retriggers;
5. continuous command updates.

This precedence must be encoded in tests before implementation is considered stable.

M0.2 implements the first four categories: scene transition, note-off, parameter change, and note-on. Stable tie-breaking uses track, persistent event identity, parameter address/intent, and runtime voice token rather than collection insertion order. Tracker commands and continuous updates are deferred.

## Events

An event may contain:

- note start, duration/gate, velocity, and probability/condition;
- default sound, alternate sample, or slice reference;
- pitch, sample start offset, reverse, and retrigger data;
- parameter locks;
- tracker command plus command memory where applicable.

Effect and mixer changes are semantic parameter events addressed through the common model in [Effects](EFFECTS.md). The scheduler emits values and musical timestamps; it does not process audio, own effect instances, or infer firmware routing.

References are logical IDs resolved by the device/project adapter. Events never store firmware pointers.

Polyphony is per track: overlapping events remain distinct voices until ended, choked, stolen by an explicit policy, or stopped by transport. The core must assign stable voice tokens so note-offs cannot terminate the wrong overlapping voice.

M0.2 assigns monotonically increasing runtime `VoiceToken`s after canonically ordering note starts. Tokens are not persisted. Notes may overlap the same sound and span pattern or scene boundaries; note-offs retain the originating token.

## Parameter locks and commands

A parameter lock overrides a track default for the scope defined by that parameter: usually the triggered voice, sometimes until the next event or pattern boundary. Scope and reset behavior are part of each parameter's schema; there is no implicit global carry.

Tracker-inspired commands include retrigger, pitch/portamento slide, sample offset, note delay/cut, and related timing effects. Command memory must be explicit and scoped to a track. Compatibility with MOD/XM/IT quirks is not a goal unless a command is documented as a compatibility mode.

If locks and commands target the same parameter at the same instant, the command-specific rule must be deterministic and tested. Unsupported device parameters remain in the project model but must be reported as unavailable rather than silently ignored during editing or export.

For polyphonic tracks, a voice-scoped lock applies only to the voice token created by that event. A track/insert/send/master lock targets a continuously running processor and remains effective for its declared scope; it must not instantiate or restart the processor on every note. Pattern- or step-scoped restoration is scheduled explicitly so independent pattern lengths cannot leave a value accidentally latched.

Tracker commands are time-domain musical operations. When a tracker command and a parameter lock address the same destination, the lock establishes the event baseline and the command supplies its documented absolute or relative evolution. Performance gestures and MIDI modulation are composed later by the effects control plane and do not rewrite the event.

## Transitions

Scene changes are requested immediately but committed on a quantization boundary: provisional options are immediate, next step/beat/bar, end of the current pattern, or a fixed musical interval. At commit:

- each track atomically switches its pattern reference;
- the transition policy determines whether its phase resets, preserves phase, or waits for that track's pattern end;
- held voices follow an explicit cut, release, or continue policy;
- queued requests resolve by a documented last-request-wins or queue policy.

No transition may expose a partially changed scene.

M0.2 supports one pending launch at an explicit absolute boundary. All sixteen pattern references switch atomically, every new pattern resets to phase zero, and already-active voices continue until their scheduled note-off. `Cut` and `Release` are represented by the policy enum but rejected as unsupported. Song arrangement and queued/replaceable launch requests are deferred.

## Persistence implemented in M0.2

`Project` carries format version 1 and uses stable typed IDs. The standard-library-only codec is deterministic and little-endian, and decode always revalidates the project. Validation rejects unsupported versions, duplicate pattern/scene/event IDs, invalid track slots, unowned patterns, zero durations, out-of-range event positions, invalid parameter addresses, missing initial scenes, and dangling/wrong-track scene references.

The binary format is an internal M0.2 format and is not yet promised as a permanent compatibility contract. M0.2a retains format version 1 and adds decoder policy rather than changing the wire schema. Default limits cover encoded project bytes, patterns, scenes, per-track references, per-pattern and total events, per-note parameter changes, parameter identifier bytes, and an estimated decoded-allocation budget. Declared sizes are checked before allocation; malformed tags, UTF-8, truncation, trailing data, and limit violations have structured errors. The allocation budget is a conservative accounting guard, not a precise heap measurement. The codec does not read stock MPC projects or perform filesystem/SD operations.

The `neurogenic-host` consumer preserves scheduler order, maintains voices by `VoiceToken`, and rejects malformed lifecycle events. It is useful for deterministic integration traces but does not convert ticks to time, render audio, send MIDI, or establish real-time safety.

## Explicitly deferred

- tracker commands, probability, ratchets, microtiming offsets, tempo conversion, and external clock;
- song arrangement, pattern editing APIs, undo/redo, and live recording;
- scene phase-preserve/wait policies and cut/release transition behavior;
- voice limits, stealing, choke groups, mute/solo behavior, and device triggers;
- parameter-lock lifetimes/restoration, modulation composition, smoothing, and DSP;
- immutable compiled snapshots and allocation-free caller-provided event buffers.

## Editing views

Grid and tracker editors manipulate the same events and commands. Neither view owns a separate sequence representation. Quantization is an edit operation and must preserve the original when previewed or cancelled.

Live recording stores raw captured time first, then applies the chosen quantization policy. Live slice recording records logical slice events; it does not destructively split audio.

## Performance state

Macros and track overrides are a separate runtime layer above saved pattern data. They can temporarily alter parameters, mute state, or pattern choice and can be cleared without rewriting events. Recording a performance override into automation is a future explicit operation, never an accidental side effect.

Performance-FX pad presses produce transient engagement/release commands. They do not restart patterns, retrigger notes, reset effect state, or modify saved parameters. Quantized engagement uses the same integer musical clock as scene transitions but a separate command queue, so an effect boundary cannot partially apply a scene.

## Required host tests

- independent pattern lengths and wrap points over long runs;
- exact event ordering at shared timestamps and boundaries;
- tempo conversion without cumulative drift;
- quantized transitions for reset/preserve/wait policies;
- overlapping voices and correct note-off association;
- lock scope/reset and tracker command memory;
- absolute/relative parameter composition, smoothing boundaries, and restoration;
- continuously running effect locks across overlapping voices and pattern wraps;
- performance-FX engage/release without sequencer restart;
- retrigger, slide, delay, cut, and sample-offset edge cases;
- deterministic playback after serialize/deserialize;
- malformed input, extreme lengths, queue capacity, and overload behavior;
- randomized/property tests comparing chunked scheduling with one-shot scheduling.

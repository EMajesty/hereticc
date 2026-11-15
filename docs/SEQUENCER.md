# Sequencer design

## Status

This is the agreed musical model for the host-testable core. It is a design specification, not an implementation claim. Numeric limits other than sixteen tracks remain provisional until tests and device measurements justify them.

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

The core uses integer musical time, not floating-point seconds. Events are stored at a high-resolution pulse position relative to their track pattern; microtiming is a signed offset with a defined clamp/wrap policy. Conversion to device frames occurs only in the runtime adapter.

Tempo is global initially. Tempo automation and external clock following are deferred until the base clock contract is measured.

The scheduler uses bounded look-ahead and deterministic ordering. At the same timestamp, the provisional precedence is:

1. transport/scene transition;
2. note termination and choke/mute actions;
3. event parameter locks and sample/slice selection;
4. note starts and retriggers;
5. continuous command updates.

This precedence must be encoded in tests before implementation is considered stable.

## Events

An event may contain:

- note start, duration/gate, velocity, and probability/condition;
- default sound, alternate sample, or slice reference;
- pitch, sample start offset, reverse, and retrigger data;
- parameter locks;
- tracker command plus command memory where applicable.

References are logical IDs resolved by the device/project adapter. Events never store firmware pointers.

Polyphony is per track: overlapping events remain distinct voices until ended, choked, stolen by an explicit policy, or stopped by transport. The core must assign stable voice tokens so note-offs cannot terminate the wrong overlapping voice.

## Parameter locks and commands

A parameter lock overrides a track default for the scope defined by that parameter: usually the triggered voice, sometimes until the next event or pattern boundary. Scope and reset behavior are part of each parameter's schema; there is no implicit global carry.

Tracker-inspired commands include retrigger, pitch/portamento slide, sample offset, note delay/cut, and related timing effects. Command memory must be explicit and scoped to a track. Compatibility with MOD/XM/IT quirks is not a goal unless a command is documented as a compatibility mode.

If locks and commands target the same parameter at the same instant, the command-specific rule must be deterministic and tested. Unsupported device parameters remain in the project model but must be reported as unavailable rather than silently ignored during editing or export.

## Transitions

Scene changes are requested immediately but committed on a quantization boundary: provisional options are immediate, next step/beat/bar, end of the current pattern, or a fixed musical interval. At commit:

- each track atomically switches its pattern reference;
- the transition policy determines whether its phase resets, preserves phase, or waits for that track's pattern end;
- held voices follow an explicit cut, release, or continue policy;
- queued requests resolve by a documented last-request-wins or queue policy.

No transition may expose a partially changed scene.

## Editing views

Grid and tracker editors manipulate the same events and commands. Neither view owns a separate sequence representation. Quantization is an edit operation and must preserve the original when previewed or cancelled.

Live recording stores raw captured time first, then applies the chosen quantization policy. Live slice recording records logical slice events; it does not destructively split audio.

## Performance state

Macros and track overrides are a separate runtime layer above saved pattern data. They can temporarily alter parameters, mute state, or pattern choice and can be cleared without rewriting events. Recording a performance override into automation is a future explicit operation, never an accidental side effect.

## Required host tests

- independent pattern lengths and wrap points over long runs;
- exact event ordering at shared timestamps and boundaries;
- tempo conversion without cumulative drift;
- quantized transitions for reset/preserve/wait policies;
- overlapping voices and correct note-off association;
- lock scope/reset and tracker command memory;
- retrigger, slide, delay, cut, and sample-offset edge cases;
- deterministic playback after serialize/deserialize;
- malformed input, extreme lengths, queue capacity, and overload behavior;
- randomized/property tests comparing chunked scheduling with one-shot scheduling.


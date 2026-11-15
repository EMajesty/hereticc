# Architecture

## Status and recommendation

This document proposes an architecture; none of it is implemented.

Start with **C: a separate host-testable sequencer core**, then integrate through **A: `mpc-sample-sdk`** if a focused device spike validates the required contracts. Do not fork or adapt a complete existing mod at this stage.

Why:

- Sequencer correctness—event ordering, polymeter, transitions, locks, and persistence—can be specified and tested without undocumented firmware state.
- [`mpc-sample-sdk`](https://github.com/ioma8/mpc-sample-sdk) is Rust/MIT, checks firmware fingerprints, exposes typed input/drawing/sample/slice/audio/lighting access, and supports reversible SD deployment.
- Its public surface does not yet prove a sample-accurate stock-engine voice-trigger API or safe coexistence with the stock sequencer. Coupling the musical model to those interfaces now would turn unknown firmware behavior into core design constraints.
- Am MOD demonstrates deep stock-app sequencing edits, but its patch set is broad, firmware-specific, and much harder to isolate. MINIMOD is a useful SDK example but is a feature collection rather than a sequencer foundation. Sample Lab and MPCTRACKER replace the stock application and therefore conflict with the preservation goal.

## Component boundaries

```text
control/UI/MIDI input
        |
        v
device adapter  <---->  project adapter / persistence
        |
        v
sequencer core -----> scheduled semantic events -----> voice adapter
        ^                                                   |
        |                                                   v
 transport clock                                  stock engine or fallback
```

### `hereticc-core`

Pure Rust domain logic with no firmware addresses, UI objects, ALSA handles, filesystem access, or wall-clock calls.

Responsibilities:

- tracks, patterns, scenes, songs, events, locks, and deterministic IDs;
- integer musical time and independent pattern wrapping;
- transition planning and look-ahead scheduling;
- tracker command state and event precedence;
- immutable snapshots or bounded commands suitable for a real-time consumer;
- versioned, platform-neutral project representation.

The core accepts explicit time/transport input and emits semantic events such as note start/stop, sample offset, slice selection, parameter change, and scene commit. It must not know how the stock firmware represents samples or voices.

### Scheduler runtime

Owns the active snapshot, converts musical positions to the integration clock, and emits events into preallocated bounded queues. It must define overload behavior explicitly; dropping diagnostics is preferable to blocking audio, while musical-event overflow must be visible and testable.

No allocation, file I/O, drawing, logging, or blocking synchronization is permitted in a real-time audio callback. Project edits are prepared off the real-time path and swapped at a safe boundary.

### Device adapter

Potentially backed by `mpc-sample-sdk`, behind HERETICC-owned traits. Responsibilities include:

- intercepting or forwarding controls;
- obtaining a monotonic transport position;
- mapping stable HERETICC sample references to live firmware objects;
- applying scheduled triggers and parameter changes;
- drawing screens and restoring native pad lights;
- detecting supported firmware and failing closed.

Firmware pointers, offsets, unsafe calls, and thread-affinity rules stay inside this boundary. A host fake implements the same traits.

### Voice adapter

The key unresolved boundary. Preferred behavior is to trigger and control voices through the stock audio engine so stock routing, effects, and stereo I/O remain intact. That capability is **not yet verified** by the reviewed SDK documentation. A separate mixer is not an assumed fallback: it would require an explicit architecture decision because it can bypass stock voice behavior and effects.

### UI adapter

Grid and tracker views are projections of the same event model. UI state such as cursor, selection, and temporary audition is separate from saved musical state. Input ownership must be page-scoped; events not owned by HERETICC are forwarded to the stock application.

### Persistence adapter

HERETICC data should use a versioned sidecar on the SD card until safe round-trip storage inside stock projects is demonstrated. Store logical references and metadata, never raw firmware addresses. Writes use a temporary file, flush, and atomic rename where the filesystem permits. Unknown future fields must survive load/save where practical.

## Integration proof required before device architecture is frozen

A minimal, non-release spike must establish:

1. pad/button/encoder/knob interception and correct forwarding/consumption;
2. custom paint and full restoration of native pad lighting;
3. repeated scheduled note-on/note-off through the stock engine, including velocity, polyphony, pitch, sample/slice selection, and effects routing;
4. a monotonic clock correlated with audio output and measured jitter under UI load;
5. behavior while the stock transport and sequencer are running;
6. safe project/sample changes without stale object access;
7. inert behavior on unsupported firmware and stock boot after SD removal.

If item 3 cannot be achieved safely, reconsider the preservation requirement and compare a replacement application separately. Do not silently mix the two architectures.

## Principal risks

- Firmware 1.3.0.12 is currently the only verified target in the reviewed projects.
- Reverse-engineered object layouts may change across firmware and during page/project lifetimes.
- A UI/event hook is not evidence of sample-accurate scheduling.
- Stock-engine triggering may not expose every voice parameter needed for locks and tracker commands.
- Long operations in the stock application may stall UI-driven callbacks.
- SD autorun is reversible but unofficial; recovery from an unbootable device is not verified by the reviewed projects.


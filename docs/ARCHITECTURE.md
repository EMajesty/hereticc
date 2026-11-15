# Architecture

## Status and recommendation

The device architecture remains proposed. M0.2a implements the hardware-independent `hereticc-core` model, validator, scheduler, typed parameter representation, and bounded versioned codec, plus a separate host-only `hereticc-host` semantic event consumer. There is no device adapter, voice adapter, effects engine, audio processing, UI, MIDI, or deployment code.

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
        |
        +------ control events ------> effects control plane
                                             |
                                             v
                                      audio routing/DSP adapter
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

Effects are not part of event generation. The core may schedule typed parameter intents and performance engagement boundaries, but an effects control plane resolves them into immutable/bounded parameter updates for an audio adapter. Audio never flows through the sequencer core.

M0.2 implements the first part of this boundary. Persistent parameter addresses use logical scopes/owners, optional processor slots, and symbolic parameter IDs. The scheduler carries absolute/relative intents but does not implement lock lifetimes, smoothing, modulation composition, or DSP.

### Scheduler runtime

Owns the active snapshot, converts musical positions to the integration clock, and emits events into preallocated bounded queues. It must define overload behavior explicitly; dropping diagnostics is preferable to blocking audio, while musical-event overflow must be visible and testable.

No allocation, file I/O, drawing, logging, or blocking synchronization is permitted in a real-time audio callback. Project edits are prepared off the real-time path and swapped at a safe boundary.

### Current allocation and real-time boundary

The M0.2a scheduler is intentionally host-oriented:

- project construction, validation, encoding, and decoding allocate;
- `Scheduler::new` validates the editable project;
- each `schedule` call clones scheduler state transactionally, allocates occurrence/event vectors, sorts them, and returns a new `Vec`;
- a capacity limit produces an error and leaves scheduler state unchanged, but it does not prevent temporary allocation before the check;
- the dependency-free codec allocates strings and collections and is never an audio-thread operation; it bounds encoded bytes, collections, identifiers, event totals, and an estimated decoded-allocation budget before allocating from untrusted declarations.

Therefore no current public scheduling operation is claimed real-time safe. A future boundary should compile a validated project into an immutable, canonically ordered snapshot and schedule into caller-owned preallocated storage. That work must preserve the M0.2 half-open interval and ordering semantics; it does not require changing the persistent model.

### `hereticc-host`

The host adapter is a test oracle and integration-shape prototype, not a device implementation. It consumes already ordered `ScheduledEvent` batches, transactionally tracks active voices by runtime token, records parameter addresses and scene commits, rejects duplicate tokens/unmatched releases/order violations, and emits a deterministic human-readable trace. The included demonstration uses two audible polymetric tracks and an explicit scene boundary without audio, MIDI, clocks, sleeping, or firmware access.

### Device adapter

Potentially backed by `mpc-sample-sdk`, behind HERETICC-owned traits. Responsibilities include:

- intercepting or forwarding controls;
- obtaining a monotonic transport position;
- mapping stable HERETICC sample references to live firmware objects;
- applying scheduled triggers and parameter changes;
- drawing screens and restoring native pad lights;
- detecting supported firmware and failing closed.

Firmware pointers, offsets, unsafe calls, and thread-affinity rules stay inside this boundary. A host fake implements the same traits.

The future contract is specified in [Device adapter contract](ADAPTER.md). In particular, tick-to-frame conversion belongs at the transport/adapter boundary; it must use checked rational or fixed-point arithmetic and must not change semantic event ordering.

### Voice adapter

The key unresolved boundary. Preferred behavior is to trigger and control voices through the stock audio engine so stock routing, effects, and stereo I/O remain intact. That capability is **not yet verified** by the reviewed SDK documentation. A separate mixer is not an assumed fallback: it would require an explicit architecture decision because it can bypass stock voice behavior and effects.

### Effects control plane and audio adapter

The logical mixer has sixteen stereo track paths, two insert slots per track, two stereo send/return buses, two stereo master insert slots, and independent performance-FX stages. This is a platform-neutral target, not evidence that the stock application exposes those buses.

The control plane owns effect descriptors, parameter addresses, modulation composition, routing snapshots, bypass transitions, tail policy, and performance gestures. It consumes sequencer parameter events, UI gestures, macros, and MIDI control without generating notes or changing transport.

The audio adapter maps that graph onto one of three separately evaluated backends:

- stock effect objects/routing, if safe live interfaces can be established;
- custom stereo-master processing at a confirmed replaceable final-output point;
- custom per-track processing only if isolated pre-sum track audio is available with defined ownership and latency.

An output callback that can add samples to the final mix does not by itself provide replace-in-place master processing or isolated tracks. Logical tracks must never be equated with stock audio buses without measurement. See [Effects](EFFECTS.md).

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
8. whether the audio tap is additive-only or permits safe in-place master replacement without double audio;
9. whether sixteen isolated stereo track paths and two feedback-safe send returns exist or must be custom-built;
10. stock effect creation, real-time processing, parameter, tail, latency, and destruction contracts.

If item 3 cannot be achieved safely, reconsider the preservation requirement and compare a replacement application separately. Do not silently mix the two architectures.

## Principal risks

- Firmware 1.3.0.12 is currently the only verified target in the reviewed projects.
- Reverse-engineered object layouts may change across firmware and during page/project lifetimes.
- A UI/event hook is not evidence of sample-accurate scheduling.
- Stock-engine triggering may not expose every voice parameter needed for locks and tracker commands.
- Long operations in the stock application may stall UI-driven callbacks.
- SD autorun is reversible but unofficial; recovery from an unbootable device is not verified by the reviewed projects.

# Effects and performance control

## Status

This document specifies a hardware-independent target. No effects engine, routing hook, DSP, benchmark, or physical-device capability is implemented or confirmed by HERETICC. The logical model must remain valid if a backend supports only a subset.

## Logical audio graph

The sequencer emits notes and timestamped parameter intents; it never owns or processes audio. A separate effects control plane resolves parameters and publishes bounded updates to an audio backend.

```text
stereo input (optional source)                         send A return inserts
          |                                                    |
voice adapter -> track 1..16 -> insert 1 -> insert 2 -> gain/pan/mute --+--> master insert 1 -> master insert 2 -> stereo output
                                      |                |                 |
                                      |                +-- send A ------+
                                      |                +-- send B ------+
                                      |                                  send B return inserts
                                      +-- performance FX at declared target(s)       |
                                                                                     +-- summed before master inserts
```

Each of sixteen logical tracks is stereo internally and has:

- two ordered insert slots;
- gain and equal-power or backend-declared pan law;
- two post-insert send levels, with pre/post-fader selection deferred;
- mute plus per-slot and chain bypass;
- a stable logical ID unrelated to firmware bus numbers.

There are two stereo send/return buses. Each has a bounded, acyclic set of sources, a configurable effect chain whose capacity is declared by the project format and backend, return gain/pan/mute, and a defined tail. Send returns feed the master and cannot send to themselves or each other initially. This prohibition prevents accidental feedback; any future feedback network requires explicit gain limiting, delay, cycle detection, and a separate decision.

The stereo master has two ordered insert slots and final gain/safety handling. Stereo input and output must remain two-channel throughout. A backend may expose input as a track source only after latency, monitoring, and feedback behavior are validated.

Performance FX are independent transient stages. A stage declares whether it is inserted on a track, a named group, send A/B, or master. Groups are sets of logical tracks, not presumed stock submix buses. Processing a group independently requires the backend to provide an isolated group sum.

The graph describes intent, not availability. A backend capability report determines which nodes can run. Unsupported routes remain serializable and editable but are visibly inactive; they are never silently redirected to master.

## Backend levels

- **Stock control:** manipulate existing stock Akai effect objects/routing through a guarded interface. Desired because it may preserve stock sound and integration, but live lifecycle and routing are unproven.
- **Custom master:** replace/process the final stereo block. Can support master inserts and master-target performance FX, but cannot honestly provide track inserts or sends.
- **Custom per-track:** process isolated stereo track buffers before summing, accumulate sends, and compensate latency. This is required for the complete graph and is the most demanding/unproven option.

Offline whole-sample rendering is a fourth workflow, not a live backend.

## Common parameter model

### Address

A parameter address is stable project data:

```text
scope / owner-id / processor-slot / parameter-id
```

Scopes are:

- `track` — gain, pan, sends, mute, bypass, or other track state;
- `voice` — one stable voice token or an event's future voice template;
- `track_insert` — track ID, slot 1 or 2, processor parameter;
- `send` — send A/B routing and return state;
- `send_effect` — send A/B, processor slot, parameter;
- `master` and `master_insert`;
- `performance` — performance slot/gesture and target;
- `macro` — named macro input, resolved to one or more destinations.

Addresses contain logical IDs and symbolic/versioned parameter IDs, never firmware pointers, offsets, raw stock parameter indices, or MIDI CC numbers. A backend maps supported addresses to implementation handles and reports unsupported ones.

### Descriptors and values

Every parameter has a versioned descriptor:

- type: boolean, trigger, integer, normalized continuous, bipolar continuous, enumerated choice, frequency, time, tempo division, gain in dB, pan, or opaque backend value;
- valid range, default, display unit/curve, and quantization step;
- automation rate: event, control-rate, or audio-rate-capable;
- smoothing policy and maximum transition rate;
- scope/lifetime and reset boundary;
- absolute/modulatable flags and modulation limits;
- real-time safety/capability metadata supplied by the backend.

Normalized storage is acceptable only when a documented curve maps it to a meaningful unit. Values are validated and clamped at the descriptor boundary; non-finite values are rejected. Enumerations use stable symbolic variants rather than numeric firmware ordinals.

### Absolute state and relative modulation

Absolute sources set the baseline. Relative sources offset or scale that baseline without rewriting it.

Provisional composition order:

1. processor/track default;
2. saved scene/track absolute override;
3. current sequencer parameter lock or absolute tracker command;
4. recorded absolute automation, if supported by the event model;
5. performance edit absolute value when explicitly in edit/record mode;
6. sum/clamped combination of relative tracker motion, macros, MIDI CC modulation, optional aftertouch, and performance gesture modulation;
7. safety clamp and smoothing.

Later absolute layers replace earlier baselines; relative layers never mutate them. For destinations that cannot combine linearly, the descriptor supplies a deterministic combiner such as multiplicative ratio, maximum, boolean OR, or last-edge-wins. Source ordering is stable by source class and ID, never thread arrival order.

MIDI CC mappings specify absolute or relative mode explicitly, input range/curve, polarity, pickup, and destination depth. Unknown relative encodings are rejected rather than guessed.

### Priority and reset

Safety/mute and backend failure override audible output. Explicit UI editing has authority to change saved values only while an edit transaction is active. Performance gestures, MIDI, and macros are transient unless armed for recording.

Reset is descriptor-defined:

- voice scope: voice end;
- event/step scope: scheduled end or next event for that destination;
- pattern scope: that track pattern's wrap/change;
- scene scope: scene transition;
- gesture scope: clean performance release;
- latched scope: explicit unlatch/mode exit/project change;
- project scope: explicit edit or load only.

Restoration publishes a value transition; it does not recreate a processor. Continuously running insert/send/master effects keep state and tails while parameters change. A voice lock cannot change another overlapping voice. A track-level lock affects the shared processor for all voices on that logical track during its declared interval.

### Smoothing

Gain, pan, wet/dry, filter cutoff, delay time where safe, and other continuous audible controls require a descriptor-defined ramp. Boolean bypass uses a short click-free crossfade or processor-specific safe transition. Triggers and discrete modes switch only at supported boundaries.

Smoothing occurs in the audio/control adapter using preallocated state and frame counts, not UI timers. Tempo changes update tempo-relative targets without resetting processors unless the effect explicitly requires it. Backend-native smoothing may be used only when its behavior is understood.

## Performance FX interface

### Gestures

An effect pad sends `engage`, optional pressure/modulation updates, and `release` for a performance slot. Momentary is the default. A slot may be configured as latched, and engage/release may be quantized to immediate, step, beat, bar, or another fixed musical interval.

Pad velocity does not change intensity by default. Pad aftertouch is ignored by default and may be explicitly mapped as relative modulation. Pad release always schedules a clean release for a momentary slot, including when another UI action closes the mode.

Engagement changes an effect stage's active mix/state; it does not restart transport, patterns, voices, envelopes, or the underlying effect instance. Latching never changes saved parameters unless the user is in explicit edit/record mode.

### Initial effect families

The initial interface should describe, without promising a backend:

| Effect | Core performance contract |
|---|---|
| Beat repeat/stutter | Capture/repeat a bounded tempo-relative window; release rejoins the live timeline cleanly |
| Trance gate | Tempo-synced amplitude pattern with phase policy independent of note starts |
| Filter | Stereo filter with smoothed cutoff/resonance and safe gain behavior |
| Bitcrush/rate reduction | Quantized depth/rate with click-safe wet/dry |
| Distortion/saturation | Bounded drive, output trim, and anti-runaway policy |
| Reverse/freeze | Bounded captured window; defined capture, hold, and release behavior |
| Tape stop | Time-shaped slowdown with a defined release/recovery timeline |
| Delay throw | Momentarily feed a delay; release stops input while the tail may continue |
| Rhythmic ducking | Tempo-phase gain envelope; not an assumed compressor sidechain |

Effects that capture audio must use preallocated bounded buffers and declare maximum capture duration. Freeze/reverse/tape effects define what happens when transport stops, tempo changes, or the target disappears.

### Multiple effects and order

Multiple pads may be held simultaneously. Each performance slot has a stable order key. Processing order is deterministic:

1. target-local performance stages ordered by configured slot/order;
2. the target's normal inserts where the slot declares pre/post placement;
3. sends and returns;
4. master performance stages and master inserts according to their declared placement.

Default placement is post-track-insert/pre-send for track/group effects and pre-master-insert for master effects. A UI must display non-default placement. Two effects cannot derive order from press timing.

If the backend lacks capacity, engagement fails visibly and leaves existing audio/state unchanged. It must not evict an arbitrary running effect.

### Release and tails

Release policies are per effect: immediate click-free bypass, timed crossfade, input-close/tail-drain, or return-to-live-timeline. Tail-capable effects stop accepting new target input on release and remain active until silence, a bounded timeout, or explicit kill. Mute policy distinguishes muting source input from killing return tails.

Leaving performance mode releases every momentary slot, preserves only explicitly latched slots when policy permits, clears transient modulation, restores pad lights and control pickup state, and leaves saved values unchanged. Project load, firmware-adapter loss, or audio fault releases/kills all transient stages safely.

## Physical control philosophy

> Pads decide what happens. Knobs decide how it happens. The fader decides how much.

Defaults:

- pad press engages its assigned effect;
- pad release disengages a momentary effect;
- velocity does not automatically alter intensity;
- aftertouch is ignored unless explicitly mapped;
- K1–K3 control three named parameters shown on screen;
- the pad-volume fader controls selected wet/dry or scene/macro morphing;
- absolute knobs/fader use pickup after context changes;
- effect-pad performance never changes saved parameters unless editing or recording is explicitly armed.

MAIN VOLUME remains the stock/global volume control unless a separately validated mode says otherwise.

### Proposed performance pad layout

This layout is a default template, not a reserved hardware mapping:

```text
pads 13–16: four user/macro slots
pads  9–12: distortion | reverse/freeze | tape stop | delay throw
pads  5–8 : filter | bitcrush/rate | trance gate | rhythmic ducking
pads  1–4 : beat repeat divisions / stutter variants
```

Every pad is assignable. Color indicates family; brightness indicates available, engaged, latched, or tailing state. The screen must distinguish target and latch state without relying on color alone.

Suggested controls, subject to conflict testing:

- B1: target (`Track`, `Group`, `Send A`, `Send B`, `Master`);
- B2: momentary/latch policy;
- B3: `PERFORM`/`EDIT` page toggle;
- DATA: select track/group/effect slot or parameter page;
- DATA push: confirm or toggle an explicitly focused value;
- PLUS/MINUS: page through assignments/parameter banks;
- K1–K3: the three displayed parameters;
- fader: wet/dry or selected morph;
- SHIFT: secondary labels only where a conflict audit permits it;
- ERASE + pad: clear an assignment in edit mode only, with no performance action.

Entry/exit has no assigned shortcut yet. A future page-by-page conflict audit must cover stock MIX/FX or Knob FX behavior, PAD BANK, 16 LEVELS, NOTE REPEAT, SAMPLE/SEQ modes, transport, SHIFT/ERASE chords, and screen-button meanings. Unowned controls are forwarded.

### MIX/FX editing workflow

1. Enter a dedicated MIX/FX page through a validated, reversible control chord.
2. Choose target with B1/DATA; pad lights show the selected track/group or performance assignments.
3. In MIX, select gain/pan/send A/send B/mute/bypass; K1–K3 edit labeled values and the fader auditions morph only when explicitly selected.
4. In FX, select insert/send/master/performance slot, choose a supported processor, then page its named parameters in threes.
5. Absolute controls wait at pickup until they cross the stored value. The display shows stored and physical positions.
6. Audition uses transient state. `Apply` is explicit; `Cancel` restores the snapshot. Recording automation is a separately armed action.
7. Exit restores stock pad lights, control ownership, pickup state, transient audition, and every momentary effect.

## DSP and real-time constraints

### CPU and memory budget

No fixed effect count or CPU percentage is promised. Each backend must measure worst-case callback time, not average UI load. Establish a per-block deadline from actual stream geometry, reserve headroom for the stock application and operating system, and reject/degrade work before the deadline is threatened.

Effect instances, delay lines, capture windows, routing buffers, smoothing state, and queues are allocated/prepared outside the audio callback. Memory is bounded by a reviewed graph snapshot. Stereo doubles relevant sample storage; captured performance effects declare their maximum frames.

### Buffer ownership

The audio callback receives explicitly owned or borrowed non-aliasing buffers with a lifetime limited to the callback. Graph snapshots are immutable for the block and swapped atomically at a safe boundary. No allocation/free, filesystem access, logging, drawing, firmware-object discovery, blocking, or contended locks occur in the callback.

Track/send scratch buffers are preallocated for the maximum validated block. Unexpected channel count, format, rate, or oversized block fails safe to native/unprocessed audio and reports asynchronously.

### Latency and compensation

Every processor reports zero or a bounded frame latency. Serial latencies add; parallel dry/wet and sends align to the longest relevant path with preallocated delay compensation. Dynamic latency changes require a graph transition outside the callback and a click-free swap. Unknown-latency stock effects cannot participate in compensated parallel routing until measured.

Performance capture aligns to the audio clock, not UI event arrival. Quantized commands carry a target frame derived from musical time. UI-to-audio and device I/O latency are measured separately.

### Bypass, tails, and overload

Bypass must be unity, stereo-safe, and click-free. Processors may remain warm while bypassed if bounded CPU permits; otherwise suspend only after tail/state policy completes. Re-enabling must not expose stale unbounded feedback or discontinuity.

Overload policy is deterministic:

1. keep native/dry audio continuous;
2. refuse a new optional performance effect before disrupting existing paths;
3. disable non-audio telemetry/visual updates;
4. bypass a faulting custom processor with a short safe transition;
5. never block, allocate, recurse, or silently change routing.

Musical/control queue overflow is surfaced asynchronously. Existing processors are not reordered or randomly dropped.

Send matrices are acyclic by construction. Return-to-send and send-to-send routes are rejected. Gains are clamped to declared ranges, feedback-capable delay internals have their own stability limits, and non-finite samples trigger processor bypass/sanitization according to backend policy.

## Host-testable contracts

- parameter-address serialization and capability mapping;
- absolute/relative composition independent of source arrival order;
- scope/reset across voices, tracks, patterns, scenes, and gestures;
- smoothing and click-free graph transitions over arbitrary block partitioning;
- deterministic simultaneous performance effects and quantized boundaries;
- tail drain, mute, cancel, latch, mode exit, and fault restoration;
- stereo channel identity and unity bypass;
- latency accumulation/alignment and graph-swap invariants;
- send-cycle rejection, gain bounds, non-finite handling, and capacity failure;
- equivalence of processing one interval as one block or many supported blocks, where processor semantics allow it.

Host tests validate the model and custom DSP contracts only. They do not establish stock firmware routing, CPU capacity, or physical hardware behavior.

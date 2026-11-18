# M0.6 independent playback architecture

## Decision

NEUROGENIC remains the authoritative sequencer. It owns tracks, patterns,
scenes, songs, arrangement, exact event times, and editing history. For audio,
the recommended first backend is a **bounded stock-sequence materializer**:
compile a finite look-ahead window of NEUROGENIC events into disposable native
stock sequence events, then let the stock transport and sound engine schedule
and play them. MIDI and high-level arrangement remain NEUROGENIC-owned. This is
a hybrid architecture, but not a dual source of musical truth.

This decision does not require compatibility with the stock project/sequence
format. Native events are a volatile playback cache. The NEUROGENIC project is
canonical and must never contain firmware pointers or native object bytes.

Direct stock-engine voice calls are not the baseline. They remain a possible
later backend for audition, note-cut, or capabilities the stock sequencer cannot
express, after their timing and voice-lifetime contracts are measured.

## Evidence levels

- **Physical:** observed by the owner on an MPC Sample running firmware
  1.3.0.12.
- **Source-supported:** established by inspected pinned source, but not
  necessarily exercised by NEUROGENIC on hardware.
- **Hypothesis:** requires a bounded experiment before product use.

The pinned integration references are `mpc-sample-sdk` revision
`9487cbba3648afb29d8e3107e31a7ac930985220` (MIT) and Am MOD revision
`6cdc67276f43cd39f64871cb7e36782481244c65` (MIT). NEUROGENIC does not copy
Am MOD's broad patch system; its implementation is evidence for narrow,
firmware-specific adapter work.

## Current verified capabilities

### Physical

- The SDK mod can coexist with the stock application and restore stock-only
  operation after volatile deployment.
- On firmware 1.3.0.12, NEUROGENIC can construct a native tag-3 note, submit it
  through the stock command/Undo boundary on the Step Edit UI thread, observe a
  unique native event with the stock comparator, remove that exact event, and
  verify absence.
- A NEUROGENIC-created event is visible in stock Step Edit and stock transport
  plays the assigned sample audibly at the expected 960-PPQ position.
- M0.5c physically confirmed interactive insertion and removal of several
  correctly positioned stock events through the NEUROGENIC grid.

### Host and QEMU, not physical proof

- `neurogenic-core` schedules exact integer musical time, independent pattern
  lengths, deterministic simultaneous events, overlapping voice tokens, and an
  atomic 16-pattern scene transition.
- The M0.4 native ABI fixture covers the firmware-specific constructor and
  command bridge. It is not proof of other firmware versions or arbitrary live
  contexts.
- Format v1 encodes the current fixed 16-track subset. It is not the final
  product schema.

### Not established

- A safe batch/replace transaction for continually materializing future events
  without polluting stock Undo history.
- Atomic replacement of a future playback window while transport runs.
- A measured safe editing horizon, stock commit latency, or behavior when the
  playhead reaches an event list being edited.
- A stock-engine voice API with sample-accurate timing, stable voice handles,
  reliable overlapping release, and scheduler-thread permission.
- A transport-correlated MIDI-output deadline and measured audio/MIDI jitter.
- Native duration/release behavior for every stock pad playback mode.

## Agreed product requirements

- NEUROGENIC owns tracks, patterns, scenes, songs, and arrangement. Stock
  sequence-format compatibility is not required.
- Preserve stock sample playback, effects, routing, transport, and hardware
  integration wherever feasible.
- The initial project has 16 audio tracks and 16 MIDI tracks, with extensible
  stable logical track identities. A track is not a pad. One audio track may
  contain a complete drum-kit performance.
- The primary editor scrolls vertically and is tracker-inspired. Rows use a
  selected fixed musical resolution; subdivisions are not dynamically inserted.
  Note columns expand horizontally when required, with one note per row in each
  note column.
- Replacing an occupied note cell is an undoable replacement, not an automatic
  retrigger conversion. Each note has independent velocity, duration, and
  microtiming. Unquantized recording is a required goal.
- Changing display resolution never destroys or requantizes events.
- Scenes, pattern chaining, and song arrangement are mandatory.
- Control mappings, live-recording column allocation, maximum note-column
  count, and whether tracker effect commands ship remain open decisions.

## Playback approaches

### A. Directly trigger stock voices

**Demonstrated/source-supported:** the SDK can forward a live pad or MIDI event.
Am MOD calls deeper firmware note-on/note-off entries and maintains native
engine, track, recorder, held-note, and generation state. This establishes
technical feasibility, not a stable scheduling contract. In NEUROGENIC's
physical M0.3d work the live pad callback had input and handler pointers but no
properties pointer (`H1 I1 P0`); ordinary forwarded pad playback worked, while
an independent safe dispatch path was not established.

**Timing:** an independent scheduler would need a transport-correlated,
deadline-capable call site. The SDK exposes a monotonic millisecond clock and
an audio block callback, but no absolute stock transport/audio-frame deadline
attached to a note call. Calling UI/pad firmware functions from the audio
callback is unsupported.

**Voice lifecycle:** the reviewed direct calls expose note/pad identities, not
a NEUROGENIC voice token. Am MOD's held-note bookkeeping shows that overlapping
release is not a trivial symmetric call. Stock one-shot sounds may ignore an
audible release even when input pairing is correct.

**Selection and effects:** a live Program/pad mapping can select stock sounds,
and playback should retain stock routing/effects, but lifetime across Program
changes and arbitrary sample/slice selection remain firmware-specific.

**Hooks and lock-in:** this path needs deeper engine/transport hooks, native
object generations, a real-time command queue, and firmware-specific voice
ownership. It has the greatest ABI and stuck-voice risk.

**Small experiment if later needed:** on a disposable project, issue two
overlapping notes for one mapped pad from a verified stock callback, capture
audio-block onset and release evidence, and prove deterministic cleanup across
transport stop and Program change. Passing would validate only that bounded
case, not a general scheduler.

### B. Materialize native stock sequence events

**Demonstrated:** one-event insert, unique observation, exact removal, absence,
and audible stock playback are physical facts. Tag-3 notes use 960-PPQ timing,
pad-to-note resolution from the live Program, and the stock command service.

**Timing:** stock transport schedules materialized events. NEUROGENIC computes
exact ticks and supplies a bounded future window; it does not wake a host thread
for every audio note. Overlapping voices, pad playback modes, stock effects, and
routing stay in the stock engine.

**Selection:** logical audio events resolve through an explicit project mapping
to a current stock Program and pad/note. A logical track can therefore contain
many drum-kit notes; track identity is independent of pad identity.

**Transitions:** independent pattern lengths, scenes, chains, and songs are
flattened into future native events. NEUROGENIC decides the transition boundary
and regenerates only the not-yet-played window. The stock sequence is a cache,
not the scene model.

**Limits:** the current adapter edits only a live current sequence/track from a
verified UI callback. Continual generation needs a bounded batch boundary,
ownership tags/receipts, an editing horizon, and safe replacement while playing.
Repeated stock Undo commands may be unsuitable for an automatic playback cache.
Atomic batch replacement and asynchronous stock commit behavior are unknown.

**Smallest experiment:** while transport is stopped, materialize and uniquely
verify a bounded two-bar event batch in a fresh unsaved sequence, including two
pads, an overlap, and off-grid tick offsets; play it with stock transport, then
remove only the received events and verify absence. This tests scaling, timing,
mapping, overlap, and cleanup without a live-window race.

### C. Hybrid

Use approach B for audio and a NEUROGENIC-owned scheduler/backend for MIDI,
scene launches, chains, and songs. Both consume the same immutable project
snapshot and integer timeline. Audio events become stock sequence cache entries;
MIDI events become bounded timestamped adapter messages correlated to the stock
transport. This preserves stock audio behavior without forcing logical MIDI
tracks into stock drum tracks.

This is the recommendation. The MIDI half remains an interface requirement,
not an implemented capability: exact output, clock correlation, latency,
external sync, and overload behavior require separate evidence. Until then,
only the audio materialization side is ready to advance.

## Feature feasibility and scheduling constraints

| Requirement | Practical implementation | Evidence/gap |
|---|---|---|
| Independent lengths and polymeter | Core expands each pattern modulo its own length into one merged future window | Host-tested core; native batch/window update untested |
| Pattern/scene launch | Choose a quantized boundary, retain committed prefix, replace future cache | Core boundary logic exists; atomic live replacement unknown |
| Pattern chaining/song | Arrangement produces the same scene-boundary plan | Product requirement; song model not implemented |
| Unquantized timing | Preserve captured integer tick; row is only a view projection | 960-PPQ native ticks verified; capture accuracy/jitter unknown |
| Retrigger | Expand into explicit bounded note events where stock semantics suffice | Not physically tested as a generated batch |
| Note cut/release | Native duration where supported; direct voice cut only after a separate contract | Playback-mode semantics unknown |
| Overlap/polyphony | Emit independent native note events and let stock engine voice them | One event audible; batch overlap still needs measurement |
| Quantized scenes | Core schedules boundary; materializer swaps future window | Live safe horizon/commit atomicity unknown |
| Stock effects/routing | Resolve to current Program/track and use stock sequence playback | Single-note stock path physically verified |
| Audio/MIDI sync | Common 960-PPQ timeline; MIDI deadlines correlated to observed stock transport | No verified MIDI transport/output adapter yet |

Preparing or switching whole stock sequences could support coarse pattern/song
changes, but it would make stock sequence structure a product constraint and
does not solve independent track lengths. A rolling event window is more
flexible. Whole-sequence switching remains a fallback experiment if safe live
event replacement proves unavailable.

## Core review and minimal revisions

Keep the existing scheduling semantics and format-v1 decoder unchanged while a
version-2 model is designed and migrated explicitly.

| Current assumption | Conflict | Minimal future revision |
|---|---|---|
| `TrackId` is `u8 < 16`; project and scene use `[...; 16]` | Need 16 audio + 16 MIDI initially and extensible identities | Stable wider `TrackId`, `Vec<Track>`, `TrackKind::{Audio,Midi}`, sparse scene slots |
| `Track::default_sound` is a pad and `SoundSource::Pad` contains `TrackId` | Tracks must not equal pads; one track may play a whole kit | Separate logical track routing from per-note `SoundTarget`/pitch/pad mapping |
| `StepEvent` has time, duration and sound but no typed velocity or note | Required note identity, velocity, duration, microtiming, MIDI | Add typed note/target, velocity, exact timestamp, route; keep locks separate |
| Patterns store exact ticks but no editor resolution or note lanes | Tracker rows/columns and non-destructive resolution changes | Persistent note-lane IDs plus view resolution metadata; ticks remain canonical |
| Scene is exactly one pattern per fixed track | Extensible tracks and silence | Sparse `TrackId -> Option<PatternId>` selections |
| No song, chain, edit transaction, recording, or Undo model | Mandatory arrangement and replacement semantics | Add song/chain entries and reversible project edit commands before live recording |
| Scheduler allocates/clones/sorts per call | Cannot be a real-time MIDI scheduler or window feeder | Compile immutable snapshots and emit into bounded caller-owned buffers |
| Runtime produces generic voice tokens but no backend split | Audio cache and MIDI have different delivery contracts | Route semantic events to typed audio-materializer and MIDI-output ports |

Display resolution is metadata for projection and entry, never event storage.
An off-grid event retains its exact tick and displays a signed microtiming offset.
If changing resolution causes multiple events to project onto one row, the view
must expose the collision/extra column rather than delete or merge events.
Entering a note into a genuinely occupied `(pattern, note-lane, target row)` cell
is one undoable replace command. It does not create a retrigger. The precise
live-recording lane-allocation policy and maximum lane count remain undecided.

The version-1 codec must not be silently reinterpreted. A future format-v2
migration maps the 16 existing pad-shaped tracks to 16 audio tracks and records
all defaults explicitly.

## Risks and lock-in points

- **UI-thread editing:** the verified native boundary is tied to a live stock
  controller. Product playback cannot depend on keeping Step Edit visible.
- **Undo pollution:** using an interactive Undo command for every look-ahead
  update could fill or interfere with stock history. A dedicated disposable
  sequence and verified batch transaction are gates.
- **Window races:** editing too near the playhead can miss, double, or mutate a
  sounding event. A measured horizon and generation/receipt protocol are needed.
- **Ownership:** cache entries must be uniquely attributable to one generation;
  ambiguous matches fail closed and never delete user events.
- **Program changes:** pad-to-note and routing mappings are live context. A
  Program/track/sequence change invalidates compiled receipts.
- **Firmware ABI:** all native containers, commands, layouts, and callbacks are
  1.3.0.12-specific and must remain isolated behind a typed adapter.
- **960-PPQ limit:** adequate for current stock events, but unquantized capture
  fidelity and MIDI jitter need measurements rather than assumptions.
- **Hybrid drift:** MIDI and audio must share a measured transport epoch; two
  independent wall-clock schedulers are unacceptable.

## Recommended implementation strategy

1. Treat the NEUROGENIC project and scheduler as canonical. Introduce no stock
   sequence bytes or pointers into the core.
2. Add a host-tested `PlaybackWindowCompiler` that converts a bounded musical
   interval into sorted audio materialization requests and MIDI intents. It
   reuses existing occurrence/timing semantics rather than adding a scheduler.
3. Extend the native adapter with a bounded batch transaction for a dedicated,
   fresh, unsaved stock playback target. Every inserted event returns a canonical
   receipt; verification and removal use existing comparator/context guards.
4. Physically validate a static two-bar batch before editing during playback.
5. Measure one rolling-window replacement at a quantized bar boundary. Only
   after this passes should scene/song playback depend on live materialization.
6. Design project format v2 around extensible typed tracks, exact-timestamp
   notes, note lanes, scenes, and songs. Keep a tested v1 migration.
7. Build the MIDI port separately against the same transport snapshot and report
   timing capability honestly. Do not delay the audio proof on MIDI completion.

## Next hardware experiment

The next device experiment should be **M0.6a bounded static materialization**,
not direct voice scheduling:

1. Guard firmware 1.3.0.12 and use the existing volatile HIL restoration path.
2. Require a fresh, unsaved two-bar stock sequence and known drum Program with
   transport stopped; record sequence/track/Program identities.
3. Materialize a fixed, host-inspected batch containing two mapped pads,
   quarter/sixteenth positions, one overlap, and two small off-grid tick offsets.
4. Observe exactly one native match for every receipt before playback.
5. Let stock transport play the batch; owner records timing/sound observations.
6. Stop transport, remove only uniquely owned receipts, verify absence, and
   restore stock. Do not save the stock sequence.

Pass means bounded multi-event materialization, unique ownership, stock timing,
overlap, mapping, and cleanup work in this context. It does not yet prove safe
live replacement, scenes, MIDI sync, or unquantized recording quality.

## Proposed milestones

- **M0.6a:** host window compiler plus the bounded static materialization test.
- **M0.6b:** one measured, generation-tagged future-window replacement while
  transport runs; establish edit horizon and failure behavior.
- **M0.7:** format-v2 project/editor model with typed audio/MIDI tracks, tracker
  rows/note lanes, replacement Undo, scenes, chains, and song arrangement.
- **M0.8:** transport-correlated MIDI backend and audio/MIDI synchronization
  measurements.

If M0.6b cannot safely replace future events, stop and compare whole-sequence
double-buffer/switching with the direct-voice experiment. Do not fall back to a
replacement sampler without an explicit architecture decision.

# Future device adapter contract

## Status

This is a design contract for a separately authorized M0.3 experiment. No Akai adapter, SDK dependency, audio callback, MIDI backend, or device executable exists. `hereticc-host` validates semantic event consumption only; it does not verify any MPC Sample capability.

## Boundary

The sequencer supplies a canonically ordered, capacity-bounded stream of `ScheduledEvent`s over an explicit half-open musical interval. A device adapter must translate those events without modifying project data, event order, runtime voice ownership, or the core's integer timeline.

The adapter reports a capability snapshot before playback. Unsupported trigger, release, parameter, sample/slice, clock, or routing features fail explicitly; they are never approximated silently.

## Clock and lookahead

- Convert 960-PPQN musical ticks to absolute audio frames using the active tempo and sample rate with checked rational or fixed-point arithmetic. 960 PPQN is a HERETICC design choice, not a verified stock clock.
- Carry fractional conversion remainder between adjacent windows so chunking cannot introduce drift. Tempo changes split conversion segments at exact musical boundaries.
- Define which audio-frame clock is authoritative, its epoch relative to stock transport, its behavior on seek/stop/loop, and whether callbacks expose absolute or block-relative frames.
- Request enough lookahead to deliver every event before its target frame, but bound both lookahead and output capacity. A UI timer is not assumed sufficiently accurate.
- Preserve core precedence at a shared frame: scene transition, note termination, parameter change, then note start. If device APIs require another order, report incompatibility rather than reorder silently.

All clock access, callback cadence, jitter, and stock-transport correlation remain unverified and require physical measurement.

## Voice and sound resolution

- Resolve stable `SampleId`/`SliceId` and pad/default references through a project-lifetime mapping. Never persist or return firmware pointers to the core.
- A successful note-on creates adapter-owned state keyed by the scheduler's `VoiceToken` and records the corresponding stock-engine handle, if one exists.
- Note-off addresses that exact token. Duplicate tokens, unmatched releases, stale handles, project-generation mismatches, and stock voice stealing are explicit adapter outcomes.
- Define ownership when the stock engine ends a voice early and how completion is reported back without reusing a live token.
- On scene transitions with the implemented `Continue` policy, existing voices retain their old resolved resource/handle until their scheduled termination. Resolution of new events uses the new scene/project snapshot.

Stock-engine note triggering, explicit release, polyphony, sample/slice selection, and stable voice handles are unknown until M0.3 hardware experiments. Public SDK sample/slice inspection is not evidence of a usable live trigger API.

## Parameters and capabilities

- Dispatch logical `ParameterAddress` and absolute/relative intent through a versioned capability table. The table names supported scopes, owners, slots, ranges, update rates, timing precision, and reset behavior.
- Keep symbolic HERETICC IDs separate from numeric firmware IDs. Firmware-specific mappings remain private to the guarded adapter revision.
- Resolve voice-scoped changes to the event's runtime token before note-on; continuously running track/effect parameters must not restart processors.
- Return unsupported, temporarily unavailable, and failed parameter application as distinct diagnostics. DSP smoothing/composition remains outside the sequencer.

No reviewed evidence currently proves comprehensive stock-effect parameter control or independent stock buses for sixteen logical tracks.

## Capacity, errors, and recovery

- Accept caller-owned/preallocated event and command storage in a future real-time path. No allocation, blocking, file I/O, UI drawing, or synchronous logging occurs in an audio callback.
- Advertise maximum events per block/window and maximum active voices. Capacity exhaustion must be observable before partial dispatch where possible.
- Treat a batch transactionally at the planning boundary. If external calls can partially succeed, return the exact applied prefix and enter a defined recovery state.
- On recoverable voice failure, stop/release only adapter-owned voices, clear mappings, resynchronize at a musical boundary, and preserve stock-owned playback.
- On clock, object-lifetime, firmware-fingerprint, or callback-contract failure, fail closed and detach/restore hooks. Never continue with guessed addresses or stale objects.
- Keep control interception, UI restoration, audio restoration, and native lighting restoration paired on every exit path.

## Stock transport interaction

M0.3 must choose and test one explicit relationship: follow stock transport, run HERETICC transport while stock sequencing is suppressed, or coexist with both engines. It must document start/stop/seek/loop semantics, tempo source, count-in, stock preview voices, duplicate triggering, and project/page changes. No mode is currently verified safe, and the host adapter does not choose one.

## Minimum M0.3 evidence

1. Exact device/firmware and SDK revision, license, fingerprints, and inert mismatch behavior.
2. Audio-frame clock access and measured event timing/jitter under idle and UI load.
3. Repeated stock-engine note-on/release with overlapping identical sounds and exact token association.
4. Stable sample/default/slice resolution across page and project changes.
5. Shared-frame ordering and bounded-output behavior under deliberate overload.
6. Adapter error injection followed by voice cleanup, native-state restoration, and stock-only reboot after SD removal.
7. Explicit results for interaction with stock transport/sequencer, separated into verified, inferred, and unknown claims.

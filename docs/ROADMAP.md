# Roadmap

Status labels: **planned**, **in progress**, **complete**. Only M0.1 documentation is currently complete; no device functionality is implemented.

## M0 — Foundation

### M0.1 Research and scaffolding — complete

Acceptance criteria:

- project goals and current non-implementation status are explicit;
- five related projects are reviewed with pinned source links;
- verified, inferred, and unknown claims are separated;
- architecture, sequencer behavior, safety policy, and contribution rules exist;
- no external code/dependency, executable firmware, installer, or device patch is added.

### M0.2 Core specification and test harness — planned

Create a minimal Rust workspace for a platform-neutral core only.

Acceptance criteria:

- integer time, IDs, track/pattern/scene/song/event types are specified;
- deterministic scheduler handles independent lengths and boundary ordering;
- transition, polyphony, and lock semantics have unit and property tests;
- serialization has a version and round-trip/golden tests;
- core has no SDK, OS, audio, UI, or firmware dependency;
- real-time-facing APIs document allocation and capacity behavior.

### M0.3 Integration feasibility spike — planned

This requires a separate, explicitly reviewed task because it creates device-facing executable code.

Acceptance criteria:

- exact upstream SDK revision and license recorded;
- firmware mismatch fails inertly;
- controls can be consumed/forwarded and native UI/lighting fully restored;
- stock-engine trigger behavior is demonstrated or conclusively found unavailable;
- trigger timing/jitter is measured against audio frames under UI load;
- sample/slice identity and object lifetime across project changes are documented;
- physical test results are clearly separated from emulator/host results;
- SD removal/reboot returns to stock without persistent device changes.

Decision gate: proceed with the SDK adapter only if stock-engine control and timing satisfy an agreed threshold. Otherwise reassess the preservation goal before implementing a mixer.

## M1 — Sequencer core

Planned scope: sixteen polyphonic tracks, independent patterns, scenes, transport, deterministic scheduling, and persistence.

Acceptance criteria:

- long-run drift and polymeter tests pass;
- overlapping voices terminate correctly;
- scene changes are atomic and quantized;
- malformed/capacity cases fail predictably;
- host tests run without device artifacts.

## M2 — Minimal device adapter

Planned scope: one guarded firmware target, input mapping, one grid view, transport display, stock-engine voice adapter if validated, and read-only sample mapping.

Acceptance criteria:

- unsupported firmware remains stock;
- no blocking/allocation in real-time callbacks;
- stock controls not owned by HERETICC still work;
- UI and pad lights restore on every exit/error path;
- measured timing and physical test matrix are published.

## M3 — Editing and transitions

Planned scope: grid/tracker views, independent pattern editing, scenes, songs, quantized launches, live event capture, undo/redo.

Acceptance criteria:

- both editors round-trip the same event model;
- transition policies pass boundary tests;
- project writes are crash-conscious and recoverable from backup;
- no destructive stock sample changes are required.

## M4 — Expressive sequencing

Planned scope: parameter locks, retriggers, slides, offsets, tracker commands, slice events, and non-destructive live slice capture.

Acceptance criteria:

- command memory and lock reset rules are documented/tested;
- unavailable device parameters are reported, not silently ignored;
- slice references survive save/reload or fail with actionable diagnostics;
- timing remains within the threshold established at M0.3.

## M5 — Performance and MIDI

Planned scope: macros, temporary track overrides, MIDI routing/control, and recording performance state where explicitly requested.

Acceptance criteria:

- override clearing never mutates source patterns;
- MIDI feedback cannot create loops by default;
- overload and disconnect behavior are deterministic;
- stock stereo I/O/effects compatibility is regression-tested.

## Practical next engineering task

Implement **M0.2 only**: a small Rust core with `MusicalTime`, stable IDs, sixteen tracks, independent pattern lengths, scene references, a deterministic scheduling iterator, and host tests for polymeter and atomic scene transitions. Do not add `mpc-sample-sdk` until the core boundary and integration-spike acceptance tests are reviewed.


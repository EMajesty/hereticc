# Roadmap

Status labels: **planned**, **in progress**, **complete**. M0.1, M0.1b, M0.2, M0.2a, and the research-only M0.3a are complete; no device functionality is implemented.

## M0 — Foundation

### M0.1 Research and scaffolding — complete

Acceptance criteria:

- project goals and current non-implementation status are explicit;
- five related projects are reviewed with pinned source links;
- verified, inferred, and unknown claims are separated;
- architecture, sequencer behavior, safety policy, and contribution rules exist;
- no external code/dependency, executable firmware, installer, or device patch is added.

### M0.1b Effects architecture and performance controls — complete

Acceptance criteria:

- a hardware-independent logical mixer, parameter-addressing model, and performance-FX interface are specified;
- stock-effect control, custom master DSP, and true per-track processing remain separate feasibility options;
- control mappings use the known MPC Sample inventory but reserve no untested shortcut;
- stereo, latency, smoothing, overload, feedback, tail, and state-restoration constraints are documented;
- no DSP, firmware hook, dependency, executable, or deployment artifact is added.

### M0.2 Core specification and test harness — complete

Implemented as the standard-library-only `crates/neurogenic-core` workspace member.

Acceptance criteria:

- integer time, IDs, track/pattern/scene/song/event types are specified;
- deterministic scheduler handles independent lengths and boundary ordering;
- transition, polyphony, and lock semantics have unit and property tests;
- serialization has a version and round-trip/revalidation tests; stable golden-file compatibility is deferred while the schema is provisional;
- core has no SDK, OS, audio, UI, or firmware dependency;
- real-time-facing APIs document allocation and capacity behavior.

### M0.2a Scheduler hardening and host adapter — complete

Acceptance criteria:

- large valid timestamps and extreme pattern lengths avoid intermediate arithmetic overflow while genuinely unrepresentable note ends fail transactionally;
- version-1 decoding rejects truncated, malformed, oversized, and invalid-reference data under centralized allocation/count/string limits;
- a separate host-only adapter validates voice lifecycle, parameter/scene records, order, and deterministic trace output;
- a two-track polymeter/scene-transition demonstration runs without device, audio, MIDI, or wall-clock integration;
- original NEUROGENIC code is explicitly MIT-licensed while upstream/proprietary material remains outside that grant;
- format, Clippy, workspace tests, Nix flake checks, and whitespace checks pass.

### M0.3a Stock engine and custom UI feasibility research — complete

This milestone contains source research, interface boundaries, UI architecture, and experiment plans only.

Acceptance criteria:

- SDK `e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39` is re-pinned, its current branch state and MIT license are recorded, and no dependency is added;
- stock pad-path note calls are distinguished from token-addressable/sample-accurate voice scheduling;
- Sample-page post-paint drawing, public primitives, repaint behavior, font/bitmap limits, and pad-light restoration are evidence-classified;
- page-scoped input ownership forwards unowned controls and retains consumed press/release pairs;
- minimum adapter responsibilities remain outside `neurogenic-core` and no speculative public hardware traits are frozen;
- UI architecture and separately reviewable experiments A–E are documented.

### M0.3b-A0 SDK hook safety investigation — complete

This is source analysis and architecture only. It creates no device executable or SDK dependency.

Acceptance criteria:

- the pinned v0.6.0 loader, 22 branch hooks, trampoline engine, callback publication, and separate paint-vtable patch are traced from source;
- stock-code mutation, page-permission, cache-maintenance, rollback, partial-installation, and process-lifetime risks are explicit;
- every hook is inventoried with dependencies and experiment-A necessity;
- a zero-hook compatibility probe is selected for the first physical experiment;
- selective and hardened-full alternatives have reviewable failure-state requirements;
- M0.3b-A implementation remains a separate review gate.

### M0.3b-A Zero-hook compatibility probe — A4 physically validated

Acceptance criteria achieved in source/build evidence:

- exact SDK commit retrieval, revision verification, deterministic patch
  application, and patched-tree identity are automated;
- the actual SDK table verifier handles 63 matches, altered/unreadable words,
  unsupported/missing images, and repeated verification in host tests;
- the firmware base is published only after complete match;
- the AArch64 ELF has no feature-hook modules, defined interposition exports,
  ALSA symbols, `mprotect`, process-memory write import, or writable/executable
  section;
- the transient autorun package and all constituent files are inspectable and
  checksummed, and two package generations are byte-identical;
- the owner-run A2 package left stock UI, pads, and audio operational and
  recovered normally without the card, but image discovery failed before the
  fingerprint check in all four reports.

M0.3b-A1 found that post-restart autorun continuation was not supportable.
M0.3b-A2 resolves retrieval without a continuation: the constructor retains the
`/run` report and attempts one bounded, no-clobber report on the uniquely
identified `rw` exFAT SD data partition. Host failure injection and package/ELF
review are complete. A2 validated mount identity, filesystem publication, and
stock coexistence on one unit, but not loader image discovery or firmware
compatibility. M0.3b-A3 added probe-only discovery reasons and process-context
classifications and was subsequently observed on the same unit.

The owner-run A3 reports came only from unexpected executable identities and
exhausted the four shared slots. A4 isolated four bounded unexpected slots from
two expected-application slots while retaining all A3 observations and the
unchanged fingerprint gate. The owner subsequently ran A4 on firmware 1.3.0.12:
the expected stock executable was reached, all 63 words matched, zero-hook
reporting completed, controls and audio remained normal, and stock-only reboot
succeeded. This validates that one device/run, not other firmware or hooks.

### M0.3b-B0 selective input-hook feasibility — host validated, not deployable

B0 selects the SDK DATA encoder-dispatch site and adds an isolated, patched-SDK
build with a one-site prepare/commit API, transparent forwarding handler,
failure-state model, host tests, and AArch64 static inspection. It produces no
SD package and has not executed on hardware. See [B0 input-hook analysis](INPUT-HOOK-B0.md).

A separately authorized B1 remains required for any physical hook test. Before
that authorization, review temporary text-page permissions, cache maintenance,
concurrent execution, rollback uncertainty, report retrieval, and service
restart/recovery boundaries.

B0.1 hardens branch-restoration classification and adds bounded saturating input
counters, isolates constructor reports in no-clobber SD slots, and removes
generated artifacts from version control. It intentionally does not publish
post-interaction counters. B0.2 implements a host-validated fixed shared `/run`
snapshot and opt-in identity-checking reader without another hook or callback
I/O. Device invocation and SD export remain unimplemented: a safe way to run
the reader while the same stock process remains alive is unproven. B1 execution
remains blocked on that lifecycle/privilege fact as well as
the existing live-patching risk review.

B1-P0 strengthens snapshot identity with a versioned process-start token and
documents a reversible package design and unauthorized operator checklist. It
finds no confirmed non-restarting USB, network, shell, or passive SD retrieval
path. B1 hardware authorization remains no-go until such read-only access is
established without enabling an undocumented service.

B1-P1 completes the host-only access-mechanism review. The pinned SDK, A4
autorun, A2--A4 physical record, and B0.2 reader establish no supported USB,
network, autorun, or stock-export path that can retrieve counters after
interaction while preserving the live stock PID. Read-only computer and stock
UI discovery is specified in [B1-P1](INPUT-HOOK-B1-P1.md); B1 remains no-go.

B1-P2 incorporates physical confirmation of USB audio/MIDI, CDC-NCM networking,
and Akai's updater HTTP service on firmware 1.3.0.12. The rendered updater has
only a confirmed version read and firmware-upload path. Generic object/WebSocket
client code is not evidence of active server support, and one subtree request
returned 404. [P2](INPUT-HOOK-B1-P2.md) adds an offline evidence analyzer and a
bounded operator procedure; no stock diagnostic or `/run` retrieval path was
found, so B1 remains no-go.

B1-P2.1 analyzes the captured updater bundle with a conservative improved
offline inspector. The verified bundle contains active updater fetches and a
generic object/WebSocket client, but the configured client instance is not
retained and no updater consumer is evident. Further generic-API probing is not
justified; see [P2.1](INPUT-HOOK-B1-P2-ANALYSIS.md).

B1-P3 selects a future, separately reviewed one-shot USB diagnostic relay as the
only concrete design that supplies both a pre-interaction arm gate and an
operator-controlled post-interaction read. It must run in an independent,
runtime-only unit, reuse the unchanged B0.2 validator, bind only to confirmed
CDC-NCM, return one bounded report, and exit. Device lifecycle, UID, bind, and
hardening facts remain unverified, so implementation/deployment and B1 physical
testing remain no-go. See [B1-P3](INPUT-HOOK-B1-P3.md).

B1-P3.1 implements the relay's standard-library-only host model: strict actual
constructor-report gates, a B0.2-equivalent fixture validator and unchanged-reader
adapter boundary, deterministic deadlines and terminal state transitions, and a
bounded two-operation request parser. It opens no listener and creates no device
payload. The constructor report lacks a start token, so `ARMED` additionally
requires an initially verified snapshot. See [P3.1](INPUT-HOOK-B1-P3-1.md).

B1-P3.1a hardens that model before lifecycle discovery. It enforces the arming
deadline on every operation, models reader completion time against the earliest
inclusive deadline, matches B0.2's eight non-overlapping read pairs and
post-stability identity order, rejects counter-total overflow with a stricter
checked relay gate, and requires canonical single-Host HTTP framing. It changes
no B0 code, listener, device payload, or physical-test authorization.

B1-P3.2 prepares, but does not execute, lifecycle discovery. It provides a
bounded offline evidence analyzer, an explicit passive command allowlist that
remains blocked because no device command channel is established, a nine-gate
decision matrix, and a separately authorized zero-hook transient-unit proposal.
No service, restart, listener, payload, or hook is created; see
[P3.2](INPUT-HOOK-B1-P3-2.md).

### M0.3b-C–E Device feasibility experiments — planned and blocked

This requires a separate, explicitly reviewed task because it creates device-facing executable code.

M0.3b-A does not use the pinned SDK's full installer. Experiments B–E remain unimplemented and unauthorized until A passes physical validation and each later experiment receives separate approval.

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

UI feasibility is a separate gate within M0.3b: one named page scope must sustain an agreed repaint/primitive budget and restore the stock screen and pad lights on every exit. Passing that gate approves renderer implementation work, not a complete UI.

### M0.4 Effects feasibility gates — planned

These are three independent experiments; success in one does not imply success in another. They require a later task explicitly authorizing device-facing executable work.

#### A. Stock Akai effect control

Minimum experiment:

- enumerate only effect factories/instances supported by evidence and map parameter count, safe ranges, defaults, stereo behavior, setup/process/destruction, tails, transport input, and bypass behavior;
- process preallocated live stereo test blocks outside and then, if safe, inside the relevant stock callback;
- change parameters at bounded rates and verify smoothing, lifetime, and restoration;
- determine whether an existing stock chain can be controlled without rebuilding it or corrupting saved projects.

Pass condition: repeatable live processing with named/ranged parameters, bounded callback work, known ownership, no leaks/stale calls, and physical-device evidence. The current SDK offline renderer alone does not pass.

#### B. Custom stereo-master DSP

Minimum experiment:

- identify whether the final stock stereo block can be safely replaced in place, not merely observed or added to;
- apply a unity processor and prove bit-transparent bypass, correct channel order, no doubled dry path, stable geometry, and restoration;
- measure block deadline, end-to-end latency, discontinuity on engage/release, and behavior during stream reconfiguration;
- validate one preallocated, bounded stereo effect and tail drain under overload.

Pass condition: a safe final stereo insert point with deterministic timing and clean bypass. This enables master/performance processing only, not track inserts or sends.

#### C. True per-track inserts and sends

Minimum experiment:

- prove that a logical NEUROGENIC track can be associated with an isolated stereo PCM path before summing, including overlapping voices and alternate samples;
- identify buffer ownership, capacity, callback order, gain/pan location, stock effects, and all fallback paths;
- route two tracks independently through unity insert chains and two bounded send accumulators, then sum returns exactly once;
- test mute, bypass, tails, latency compensation, project/page changes, live input, preview, stock playback, and unsupported paths;
- demonstrate feedback prevention and restoration to native routing.

Pass condition: isolated, complete, physically tested track paths with no missing or double-routed audio. This is the only gate that can validate the full logical mixer target.

If C fails, retain the logical parameter/routing model for portability but expose only capabilities actually backed by A or B. Do not simulate track inserts by processing the final master.

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
- stock controls not owned by NEUROGENIC still work;
- UI and pad lights restore on every exit/error path;
- measured timing and physical test matrix are published.

The first UI remains a diagnostic/grid slice sufficient to validate the adapter. Complete Tracker, Perform, MIX/FX, project browsing, typography refinement, and animation are later UI implementation work after M0.3b rendering/input feasibility passes; they are not hidden inside M2 acceptance.

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

Effects acceptance additionally requires the relevant M0.4 gate; parameter locks targeting unavailable routing remain editable but visibly unsupported.

## M5 — Performance and MIDI

Planned scope: macros, temporary track overrides, MIDI routing/control, and recording performance state where explicitly requested.

Acceptance criteria:

- override clearing never mutates source patterns;
- MIDI feedback cannot create loops by default;
- overload and disconnect behavior are deterministic;
- stock stereo I/O/effects compatibility is regression-tested.
- momentary/latching performance effects release cleanly without restarting the sequencer;
- pickup, simultaneous effects, deterministic ordering, tail policy, and state restoration pass host tests and the applicable effects gate.

## Practical next engineering task

The next decision is whether to authorize P3.2a's exact passive allowlist after
first establishing a supported or separately reviewed read-only command-access
mechanism. Without that access path P3.2a remains blocked. P3.2b transient-unit
testing, cross-building, deployment, and physical DATA-hook execution remain
later separate authorization gates. Rendering, stock voice, and timing remain
separate later approvals.

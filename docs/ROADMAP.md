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

B1-P3.2c designs a narrower alternative for the baseline: a future one-shot SD
autorun collector that publishes selected pre-restart facts and exits without a
drop-in, daemon-reload, service restart, helper, or listener. The host analyzer
now rejects contradictory parent rows, requires exact `/28` address/route/link
evidence, and validates a bounded versioned report. No collector or package is
implemented, and independent-unit survival remains unproved; see
[P3.2c](INPUT-HOOK-B1-P3-2C.md).

B1-P3.2d implements a dependency-free, library-only Rust collector model with
bounded selected-source parsing, exact stock identity continuity, injected
deadlines, deterministic reports, and a host-tested no-clobber publication
boundary. The analyzer separates schema validity, producer completeness, and
evidence consistency and reports legacy conflicts. Deterministic inert-manifest
and structural safety tools create no autorun package. Device acquisition,
AArch64 invocation, and full physical SD validation remain unimplemented; see
[P3.2d](INPUT-HOOK-B1-P3-2D.md).

B1-P3.2e replaces the exFAT-incompatible hard-link publication model, makes
network normalization order-independent, and adds a narrow Linux acquisition
crate using a fixed command list plus exact MainPID-derived `/proc` paths. The
libraries cross-compile to AArch64 relocatable objects and pass host tests. A
descriptor-relative publication race, hard interruption of direct reads, full
descendant cleanup, fixed-command availability, executable ABI, and physical
mount behavior remain unproved; physical execution is therefore no-go. See
[P3.2e](INPUT-HOOK-B1-P3-2E.md).

B1-P3.2f completes the host-side physical-readiness candidate: socket
enumeration is removed, deadline/containment failures are terminal, fixed
utilities run in a fully cleaned process group, the 1.3.0.12 prefix fingerprint
is checked locally, and exFAT publication is descriptor-relative with live
magic and identity revalidation. A reproducible 540528-byte AArch64 PIE is
linked for GLIBC 2.17 and statically inspected. Standalone loader, fixed utility
availability, autorun privilege, and physical mount behavior remain unproved.

B1-P3.2g constructs and host-inspects one deterministic zero-hook A4-format
autorun package. Its fixed wrapper independently checks the 1.3.0.12 firmware
prefix, invokes the reviewed P3.2f AArch64 collector exactly once, preserves its
status, and contains no preload, service control, hook, listener, or internal
write. Generation is restricted to the ignored host build tree; strict package
inspection, byte-for-byte reproduction, and wrapper simulations pass. Loader,
utility, privilege, and physical mount behavior remain unverified, so physical
execution requires separate authorization and B1 hook testing remains no-go;
see [P3.2g](INPUT-HOOK-B1-P3-2G.md).

The first owner-run P3.2g physical diagnostic produced no report and no obvious
startup hitch while stock operation remained normal; this is inconclusive and
the unchanged package must not be retried. B1-P3.2j prepares a separate
marker-only automatic autorun that can establish shell entry, canonical package
directory, firmware-prefix compatibility, and SD publication without invoking
the collector. The same physical SD and volume label successfully published
A2--A4 reports, so generic SD identity, formatting, and basic writability are
already demonstrated. Diagnosis prioritizes autorun recognition, wrapper and
firmware-guard behavior, then standalone collector startup from the `noexec`
SD. P3.2j physical execution remains owner-authorized only; see
[P3.2j](INPUT-HOOK-B1-P3-2J.md).

The owner subsequently obtained a complete passing P3.2j marker on stock
1.3.0.12, physically confirming autorun recognition, shell entry, package
directory resolution, firmware-prefix validation, and SD marker publication.
B1-P3.2k leaves the reviewed P3.2f collector unchanged, stages it exclusively
at a fixed volatile `/run` path, invokes it once in the foreground, records a
separate bounded stage marker, cleans up after return, and never touches the
stock service. The deterministic package and simulations are host-complete;
physical execution remains owner-only and B1 hook testing remains no-go. See
[P3.2k](INPUT-HOOK-B1-P3-2K.md).

The owner-run P3.2k marker physically confirmed firmware/collector guards, RAM
staging, invocation return with status 1, absent report, and cleanup. P3.2l found
that the collector did not decode Linux `\040` mount-field escapes, corrected
that parser without weakening identity/magic/no-follow checks, and adds one
whitelisted volatile stage token to localize entry, firmware, SD, process,
collection, and publication boundaries. The distinct deterministic package is
host-complete; one owner-authorized zero-hook run is next. B1 hook testing
remains no-go; see [P3.2l](INPUT-HOOK-B1-P3-2L.md).

The diagnostic branch is now closed for feature planning. Do not add another
collector milestone or repeat P3.2k. P3.2l may be run later only if its evidence
is required by a concrete relay decision.

### M0.3c SDK page and input prototype — physically validated

Reuse the current pinned MIT `mpc-sample-sdk` v0.7.1 directly. Add one
device-only mod that renders a fixed NEUROGENIC step page, owns a minimal
button/DATA/pad set while that page is active, forwards everything else, and
restores the stock screen and pad lighting on exit. Use the SDK's existing
guarded build/autorun path;
do not extend the B1 relay, create another diagnostic package, or require SSH.
The owner physically validated SDK load, overlay drawing, SHIFT+DATA open/close,
DATA cursor movement, pad selection, PLUS editing, and stock UI/control/audio
restoration on firmware 1.3.0.12. See [M0.3c SDK UI](M0-3C-SDK-UI.md).

Acceptance criteria:

- the SDK dependency is pinned to `9487cbba3648afb29d8e3107e31a7ac930985220`
  with its MIT notice preserved;
- a static 320x240 grid is drawn only on the intended stock page;
- DATA moves a cursor, pads select tracks, and one button toggles an in-memory
  step without project or sample writes;
- unowned controls and all inactive-page input forward unchanged, with paired
  press/release handling;
- leaving the page restores stock paint and pad lights;
- host checks and package inspection pass before separate physical
  authorization.

### M0.3d stock sample trigger — physical acceptance failed; corrected by M0.3d.1

The M0.3c page now uses callback-local SDK pad objects to dispatch one bounded
stock `send_note_on`/`send_note_off` pair. It retains no firmware pointers,
allows one audition voice, defers close until release, and forwards safely when
the SDK callback context is unavailable. Host reducer tests, AArch64 cross-build,
and strict SDK package inspection passed. Physical testing showed the combined
capability gate forwarded pads before selection; ordinary stock playback
explains the sound, so native dispatch remains unvalidated. See
[M0.3d stock trigger](M0-3D-STOCK-TRIGGER.md).

Acceptance requires one controlled physical run covering stopped/running
transport, repeated/rapid triggers, attempted overlap, pad switching, close
while held, stock restoration, and stock-only recovery. A pass establishes only
interactive pad-path audition—not scheduler timing, voice handles, or
polyphony. M0.3d.1 restores M0.3c ownership independently of native capability
and adds bounded on-screen `H/I/P` presence bits. See
[M0.3d.1 pad ownership](M0-3D1-PAD-OWNERSHIP.md). Sequencer playback waits on
that corrected stock-engine gate.

M0.3d.1 physically established `H1 I1 P0`. M0.3d.2 resolves the null-properties
blocker by using the SDK's supported `Flow::Forward` path for the owned first
physical pad pair, preserving the firmware's exact arguments; overlap remains
consumed and close remains deferred. It does not call `send_note_on` with null
or claim synthetic scheduler playback. See
[M0.3d.2 native properties](M0-3D2-NATIVE-PROPERTIES.md).

Physical M0.3d.2 testing confirmed the first forwarded pad and overlap
suppression, but found unreliable release ownership/restoration under repeated
input. M0.3d.3 replaces the per-pad bit mask with a bounded edge ledger that
matches pad/input identity while tolerating the observed press/release source
mismatch. It also removes redundant pre-forward repaint and explicitly scopes
bank behavior to the 16 physical pads because SDK v0.7.1 exposes no A-D bank
identity. See [M0.3d.3 input stability](M0-3D3-INPUT-STABILITY.md).

M0.3d.3 subsequently failed physical acceptance: release ownership and
deferred close remained unreliable, pads could become temporarily unresponsive,
bank A differed from B-D, and flicker persisted. M0.3e abandons audition
ownership and restores the physically proven M0.3c editor model. Pads select
one of 16 tracks and are consumed while the overlay is open. A small core bridge
translates the grid into existing deterministic scheduler events. Playback is
redirected toward a bounded stock sequence-command SDK extension informed by
Am MOD's native event-list/Undo implementation. See
[M0.3e sequencer integration](M0-3E-SEQUENCER-INTEGRATION.md).

### M0.4e Native stock-sequence editing lifecycle — physically verified

M0.4 converts one active selected NEUROGENIC cell into a bounded, pointer-free
request for the stock UI's current sequence/current track. The request uses the
existing 960-PPQ grid, keeps physical-pad identity unresolved until a live
Program mapping is available, and can be submitted through its adapter at most
once. Host tests cover validation, constructor arguments, failure, cancellation,
and repeat refusal.

M0.4a implements the missing firmware-specific port as an isolated,
feature-gated extension adapted from Am MOD's MIT-licensed command path. It
validates a callback-local stock Step controller, resolves pad-to-note, owns the
tag-3 value and native containers/strings/shared result, submits through the
stock add-command/Undo service, supports exact native removal, and retains no
live model pointers. An AArch64/QEMU fixture executes the constructor,
hidden-result ABI, add/remove dispatch and cleanup. Normal M0.3e is unchanged.

A distinct HIL artifact arms one fixed insertion through atomics and executes
it only from the stock Step-screen paint callback. It distinguishes command
acceptance, observed event-list presence, removal and failure. M0.4c physically
confirmed one live insertion, its visible stock Step event, audible stock
playback, and automatic restoration. See
[M0.4 native sequencer](M0-4-NATIVE-SEQUENCER.md).

M0.4b additionally binds arm/lookup/removal to the same sequence-track context,
rejects a pre-existing identical event, prevents recursive paint from entering
native work, verifies exact absence after removal, and adds a separately
confirmed one-command HIL experiment with automatic stock restoration. Its
command path has now executed on hardware. See
[M0.4b safety review](M0-4B-NATIVE-SAFETY.md).

The first physical attempt reached and armed a valid Step-editor context but
never reported command acceptance; restoration passed. Evidence supports an
idle-screen repaint gap, not a native-command failure. M0.4c adds explicit
post-arm paint and adapter-stage evidence, host-side expiry, and one bounded
operator DATA-detent repaint request. See
[M0.4c diagnosis](M0-4C-NATIVE-INSERTION-DIAGNOSIS.md).

That retry accepted the command and the owner observed and heard the inserted
note, but automated observation failed because the adapter compared all 56
bytes rather than using the stock event comparator. M0.4d uses the comparator
established by pinned Am MOD, requires a unique match, adopts the live canonical
value for removal, and emits bounded scan counters. Host/QEMU validation passes.

M0.4e physically verified the complete bounded lifecycle: the comparator found
one unique event, exact removal reached `event_removed`, a later scan found zero
events, and stock restoration passed. The HIL runner alone reported failure
because polling missed the transient `remove_accepted` state. It now accepts
the later `removed` state only with sticky `event_removed=1` evidence, while the
separate absence milestone still requires that final evidence. See
[M0.4e and M0.5 grid boundary](M0-4E-M0-5-NATIVE-GRID.md).

### M0.5a First playable native grid — hardware candidate

The fixed-capacity coordinator now maps the stable M0.3e 16-step grid to the
verified native adapter. ON uses the live pad mapping and existing 960-PPQ
timing; OFF removes only a canonical receipt owned by that cell. Grid bits
change only after unique presence or verified absence, and pending, failed and
ambiguous states remain explicit. Host tests cover timing, delayed commit,
exact receipt removal and refusal to remove untracked stock events.

M0.5a supplies that boundary in a distinct feature-gated artifact. It renders
from the verified Step Edit paint hook, queues only pointer-free input intents,
advances native work on the owning UI thread, and requests bounded follow-up
paints only with the callback-local component. PLUS is transactional rather
than optimistic. DATA is temporarily forwarded as the explicit repaint
handshake. The stable M0.3e Sample overlay is unchanged.

The artifact is ready for its first controlled physical test but is not yet
hardware-validated. Initial acceptance remains A01 at steps 0, 4, 8 and 12,
followed by removal of step 8 and stock restoration. See
[M0.5a playable native grid](M0-5A-PLAYABLE-NATIVE-GRID.md).

### M0.5b Guarded first playable-grid test — ready for owner run

The existing HIL CLI now provides one separately authorized `grid-experiment`
cycle. It builds and immutably deploys the M0.5 artifact, verifies the Step hook
through a bounded pointer-free diagnostic snapshot, holds the build active for
the owner-operated musical test, records an exact PASS/FAIL acknowledgement,
and restores stock in `finally`. Restoration outcome is independent of musical
acceptance. The native edit implementation is unchanged. Physical success is
still pending; see [M0.5b guarded grid test](M0-5B-GUARDED-GRID-TEST.md).

### M0.5c Step Edit presentation replacement — physically validated with UI defects

The first M0.5b run physically confirmed hook installation and 18 Step paint
callbacks, but the chord-dependent presentation never activated and no intents
were accepted. M0.5c opens NEUROGENIC automatically on the first valid Step
paint while leaving the stock controller alive. Hardware then confirmed native
insert/remove, correct stock positions, audible playback and stock-view
switching. DATA used raw multi-count deltas, committed bits waited for a later
paint, and stock-view switching left stale overlay regions; see
[M0.5c Step Edit presentation](M0-5C-STEP-EDIT-PRESENTATION.md).

### M0.5d DATA navigation and redraw — hardware candidate

The native grid now applies the pinned SDK's documented one-step-per-encoder-
callback normalization, performs one same-paint verification after synchronous
command acceptance, and forwards only the complete SHIFT+DATA presentation
pair so stock schedules a full Step repaint. `GRID QUERY` v3 adds bounded raw
encoder, logical movement, repaint, switch and commit counters. The verified
native adapter is unchanged. Host/QEMU validation does not prove physical
detents or framebuffer restoration; see
[M0.5d DATA and redraw](M0-5D-DATA-AND-REDRAW.md).

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

### Effects feasibility gates — planned (legacy pre-M0.6 label)

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

Effects acceptance additionally requires the relevant effects gate above;
parameter locks targeting unavailable routing remain editable but visibly
unsupported.

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

HIL-0 is physically validated on firmware 1.3.0.12: strict USB SSH identity,
volatile deployment, one controlled stock-app restart, M0.3e operation, and
stock restoration worked. HIL-1 adds immutable hash-addressed builds, explicit
activation and rollback, NixOS/BusyBox portability fixes, and a test-only
volatile mailbox that invokes the actual in-process UI reducer. Level B does
not simulate SDK callbacks; Level C input and Level D visual/audio capture
remain planned. Prefer the MIT-licensed `virtual-mpc-sample` backend rather
than a new emulator. See [HIL-0](HIL-0.md) and [HIL-1](HIL-1.md).

The first HIL-1 hardware run also validated activation, live Level B `QUERY`,
and stock restoration. HIL-1.1 atomically publishes commands and fails closed
for overlay-owned callbacks during the worker's short reducer lock. Its next
action is one scripted Level B hardware run, after which development returns to
the stock-sequencer adapter. See [HIL-1.1](HIL-1-1.md).

HIL-1.2 provides a single fail-safe `run` command and per-run JSON/text reports.
Its first physical full cycle validated build, deploy, activation, command
delivery, automatic restoration, and reporting. The scenario exposed a
test-context conflict: synthetic `OPEN` did not establish the SDK page context,
so a normal background page-loss callback closed the reducer before `SELECT`.
HIL-1.3 adds a feature-gated test-session ownership token covering both
asynchronous closure paths and prints immediate expected/actual failure details.
One repeat of the same eight-command cycle is the only next HIL action; after
that, priority returns to the bounded stock-sequencer adapter. See
[HIL-1.2](HIL-1-2.md) and [HIL-1.3](HIL-1-3.md).

M0.5c physically confirmed that the Step Edit replacement creates, removes and
audibly plays native stock-sequencer notes. M0.5d's navigation/redraw correction
remains host/QEMU-tested pending a guarded device run; it is not a playback
architecture gate. The practical sequencer next step is M0.6a's bounded static
materialization compiler and experiment described below. B1 DATA-hook testing
remains unrelated and unauthorized.

## M0.6 — Independent playback architecture — decision complete

M0.6 records the expanded product model: 16 audio and 16 MIDI tracks initially,
extensible identities, tracker note lanes, exact velocity/duration/microtiming,
non-destructive display resolution, scenes, chains, and songs. The current fixed
16-pad format-v1 model remains implemented history, not the final schema.

The selected playback strategy keeps NEUROGENIC authoritative and materializes
bounded audio windows into disposable stock native sequence events. The stock
transport and audio engine schedule those events; a later NEUROGENIC MIDI
backend consumes the same timeline. Direct engine voice calls are not the
baseline because timing, voice identity, release, and thread contracts remain
unverified.

Next milestone: M0.6a adds a host-tested playback-window compiler and a guarded
static two-bar native batch experiment. The hardware experiment must verify
unique receipts, multiple pads, overlap, off-grid ticks, stock playback, exact
cleanup, and restoration before any live rolling-window work. See
[M0.6 playback architecture](M0-6-PLAYBACK-ARCHITECTURE.md).

### M0.6a — Static materialization — physically confirmed

The compiler and fixed-capacity batch coordinator are implemented and host
tested. A distinct volatile HIL artifact compiles eight events across two
logical tracks/pads, including two off-grid positions and an overlap, then
advances one native boundary per verified Step paint. Per-event evidence allows
exact cleanup of only uniquely observed receipts after partial failure.

The owner-operated firmware-1.3.0.12 experiment passed: all eight A01/A02
events were uniquely materialized, played audibly, removed, and followed by
stock restoration. Timing/jitter and Undo-stack behavior were not measured.
See [M0.6a static materialization](M0-6A-STATIC-MATERIALIZATION.md).

### M0.6b — Live window replacement — research stop

Pinned Am MOD source identifies a plausible native 960-PPQ engine tick, but it
requires an additional firmware entry hook not exposed by SDK v0.7.1 or the
existing verified Step-paint context. More importantly, no inspected source or
physical result establishes how far ahead the playback engine queues events.
Without both a fresh exact-context playhead and a measured queue horizon, any
edit-ahead interval would be guessed. M0.6b therefore adds no live-edit artifact
or HIL command. The smallest next experiment is a read-only, startup-installed
transport observer; live replacement remains no-go. See
[M0.6b live-window feasibility](M0-6B-LIVE-WINDOW-FEASIBILITY.md).

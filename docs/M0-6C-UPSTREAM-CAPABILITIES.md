# M0.6c upstream capabilities and playback alternatives

## Decision

Do not add another playback hook yet. The upstream audit removes three reasons
to reverse-engineer from scratch, but it does not remove M0.6b's safety gate:

1. adapt Am MOD's read-only `NowPosition` observer instead of rediscovering a
   stock transport clock;
2. adapt its native bulk-event command construction if NEUROGENIC later needs
   fewer Undo entries than the already verified per-event cache;
3. use `virtual-mpc-sample` for stock-app autorun, UI, event-list, and coarse
   audio regressions before physical runs.

No inspected project proves how far ahead stock playback consumes native
events, a safe live mutation boundary, or a callable prepared-sequence swap.
Consequently rolling native-event replacement remains **no-go**. The smallest
justified next implementation is a reviewed, read-only transport-observer SDK
extension derived from the pinned Am MOD contract. It should be tested in the
virtual MPC first and then in one physical, non-editing clock experiment.

Direct stock-engine triggering is the best researched fallback if native cache
replacement remains unsafe. Am MOD demonstrates the relevant entry points and
the surrounding lifecycle complexity; it does not provide a stable public
voice API or evidence of sample-accurate scheduling.

## Scope and evidence labels

This is a source audit only. It adds no hook, binary, HIL command, dependency,
or device package. Existing NEUROGENIC physical evidence remains authoritative:
M0.6a inserted, observed, played, and removed eight static A01/A02 events on
firmware 1.3.0.12. M0.6b already established Am MOD's `NowPosition` observer
and the unknown stock playback-queue horizon.

- **Public API** means an interface intentionally exported by the pinned SDK.
- **Private source evidence** means firmware offsets/layouts used by upstream;
  it is not a stable or safe NEUROGENIC contract.
- **Hardware evidence** is claimed only where the upstream repository records
  a physical run. Emulator and QEMU evidence remain separate.

## Audited revisions and reuse policy

| Project | Pinned revision | License finding | Policy |
|---|---|---|---|
| `ioma8/mpc-sample-sdk` | [`9487cbb`](https://github.com/ioma8/mpc-sample-sdk/tree/9487cbba3648afb29d8e3107e31a7ac930985220) | root MIT license | **Direct dependency** for supported hooks, UI, input, and packaging; **adapt upstream** for a narrowly reviewed transport observer. |
| `AlekseyMalykh/am-mod` | [`6cdc672`](https://github.com/AlekseyMalykh/am-mod/tree/6cdc67276f43cd39f64871cb7e36782481244c65) | root MIT license | **Reference/adapt** verified firmware contracts. Do not import its broad clone-patch/runtime architecture. Preserve attribution for copied code. |
| `ioma8/mpc-minimod` | [`18d41fd`](https://github.com/ioma8/mpc-minimod/tree/18d41fd805beace4fed623653a250c46a4fa2243) | Cargo manifests say MIT; no root license file at this revision | **Reference** only until licensing/notice scope is clarified. |
| `orrpunk-art/virtual-mpc-sample` | [`15f732c`](https://github.com/orrpunk-art/virtual-mpc-sample/tree/15f732c71fdd722c924590b9cdec2429f7a1ec2b) | root MIT license | **Direct development dependency/tool**, without redistributing proprietary firmware inputs. |
| `jeffwebb-to/mpc-sample-tracker` | [`4a13c17`](https://github.com/jeffwebb-to/mpc-sample-tracker/tree/4a13c1792ac9a4200567950ac9eb5c0b505348ce) | root MIT license | **Reference** for bounded tracker scheduling and hardware I/O; **avoid** its replacement-engine architecture for NEUROGENIC. |
| `gospodindilly-ops/mpc-sample-lab` | [`3d482b9`](https://github.com/gospodindilly-ops/mpc-sample-lab/tree/3d482b94c918b96d174f31c411686f2b18b74ce6) | no license found | **Facts/reference only**; do not copy code. Its standalone apps stop stock `ac50`. |

## Capability matrix

| Capability | Exact upstream source | Evidence | Reuse and complexity | Remaining uncertainty / physical test |
|---|---|---|---|---|
| SDK hook transaction and stock callbacks | SDK `src/hook.rs`, `src/sites.rs` | Public SDK source; SDK path already physically used by NEUROGENIC | **Direct**. Startup-only prepare/seal/commit and fixed hook table should remain the installation boundary. | A new transport site still needs prologue/fingerprint review and one physical validation. |
| Stock transport tick/running observation | Am MOD `sd-mod-session/src/step_native.c`, `step_native_clock()` and `step_native_clock_ticks()`; hook declared in `phone_build.py` as `NOW_TICK` at `0x2109a90`; caller in `src/phone_native.c::now_tick()` | Private 1.3.0.12 source plus upstream ARM fixtures. Timer vtable `0x6da4588`, engine `+0x258`, tick getter `0x2113b10`, transport `+0x768`; copied atomics only. | **Adapt**, medium complexity, as an isolated SDK extension. It eliminates new clock reverse engineering. | Must verify callback cadence, tick/loop/seek/tempo semantics and exact sequence correlation physically. It does not reveal queue depth. |
| Native event construction and Undo editing | Am MOD `src/step_native.c`: event constructor `0x27e54d0`, add factory `0x19500e0`, remove factory `0x194fa30`, comparator `0x19722e0`, event-list context at controller `+0xc0` | Private source; the corresponding NEUROGENIC path is physically verified for single and static-batch events | Already **adapted**. Keep the command/Undo boundary and canonical receipts. | Running-transport mutation remains unsafe without a queue boundary. |
| Bulk native event command with one outer history entry | Am MOD `src/batch_events.cc`: native `std::multiset` ABI, sequence list `0x2957ae0`, bulk command constructor `0x19d79d0`, bounded traversal and rollback; `src/batch_copy.cc` owns perform/undo | Private source and upstream fixtures; no NEUROGENIC physical test of this bulk factory | **Adapt later**, medium/high ABI risk. It can reduce Undo growth and avoid per-event factory duplication. | Live atomicity is not proved; its loop performs per-list commands and rollback. Physical test required before replacing the verified adapter. |
| Direct stock-engine note start/release | Am MOD `src/poly_record.c`: event construction plus note-on `0x211b090`, release `0x2118930`; `src/batch_copy.cc::run_event_preview()` also uses ordinary-pad receiver `0x1e270d0` and engine note-on | Private implementation. Am MOD carries engine/track/context generations, held-note maps, recorder routing, release choice, and thread-local guards | **Reference/adapt only**, high firmware coupling. Demonstrates feasibility without inventing layouts. | No returned voice handle, scheduler deadline, or proven sample-accurate calling thread. Identical-note overlap/release, stealing, effects routing, and timing need physical tests. |
| SDK pad/MIDI replay | SDK `src/midi.rs::NoteEvent::replay_as_pad`, `send_note_on`, `send_note_off`; MINIMOD `rust/crates/minimod/src/midi.rs` | Public SDK API, but requires a live incoming event/input/properties on its owning callback thread | **Direct** only for input transformation, not independent playback. | It cannot accept a future timestamp or return voice identity. No additional experiment is needed to reject it as the main scheduler backend. |
| External MIDI-to-pad routing and recording boundary | MINIMOD `docs/MECHANISMS.md` and `docs/HISTORY.md`; implementation in `rust/crates/minimod/src/midi.rs` | Source plus documented hardware observation: transformed external MIDI sounds but bypasses the stock recorder/event list | **Reference**. Avoid rebuilding its bounded ownership ledger unless MIDI input transformation becomes a feature. | It does not solve native sequence insertion or MIDI output. No playback-architecture experiment needed. |
| Prepared sequence selection/queue indication | Am MOD `src/music_ui.c` exposes selected/present sequences; `src/phone_native.c::sequence_palette_tick()` observes native pad-light pulsing and records that selection while playing is queued until bar end | Read-only private observation; no programmatic switch API identified | **Reference**, low immediate reuse. It shows stock has quantized UI sequence switching. | No verified setter, target ownership, cancellation, completion, or pointer-stable double buffer. A source/virtual proof is required before any physical switch experiment. |
| Mutating event containers without Undo | No safe implementation found. Am MOD traverses containers but edits through native command objects, including its bulk path | Negative source finding | **Avoid**. Direct tree mutation would bypass normalization, ownership, Undo, and playback synchronization. | No further experiment justified unless authoritative firmware code establishes a transaction contract. |
| Stock MIDI sequencing/output | SDK exposes incoming MIDI transformation, not a timestamped output scheduler. No audited project supplies a stock sequencer MIDI-output adapter | Negative source finding | Implement a separate NEUROGENIC MIDI port later; share the core timeline, not firmware pointers. | Device MIDI output API, clock correlation, and latency still require focused research/physical measurement. |
| Stock-app emulator with panel, screen, audio and autorun | virtual MPC `lab.py` (`autorun`, preload, QMP and checked firmware inputs); `panel/server.py` (control UART, QMP screen capture, external MIDI and audio bridge) | MIT source; executes the stock application with operator-supplied proprietary inputs. Software emulation explicitly uses a different audio buffer and can run slower | **Direct tool**, medium setup. Can replace many package, hook-install, callback, UI, event-list, and cleanup experiments. | Cannot establish physical latency, jitter, ALSA/device scheduling, electrical controls, or true stock queue horizon. Those remain physical. |
| Standalone tracker scheduler/audio | tracker `src/player.c`, `src/hw.c`, `autorun/tracker.sh`; lab `sampler/sampler.c`, `firmware/engine.c`, `firmware/hw.c` | Tracker MIT; lab records hardware runs but is unlicensed. Both stop `ac50` and own ALSA/DRM/control surface | **Avoid** as playback backend; **reference** bounded scheduling and overload policy only. | It deliberately forfeits stock sample/effects/project reuse, contrary to product requirements. No experiment needed. |

## What this audit eliminates or simplifies

- **No new clock reverse engineering:** the Am MOD observer is the concrete
  starting contract. Work is SDK integration, review, and validation.
- **No speculative direct event-tree writer:** all credible upstream editing
  continues through native command objects. The verified NEUROGENIC adapter is
  the safer baseline.
- **No assumption that SDK MIDI replay is a scheduler:** MINIMOD confirms that
  this path transforms live input and can bypass stock recording.
- **No new emulator:** virtual MPC already supports the stock application,
  autorun packages, panel frames, screenshots, audio, SSH, and result capture.
- **No replacement audio-engine detour:** tracker and lab solve a different
  problem by stopping `ac50`; adopting them would discard stock effects and
  sample/project integration.
- **No premature prepared-sequence implementation:** upstream shows the stock
  UI queues selection at a bar boundary, but not a safe callable switch API.

## Playback architecture comparison

Scores are relative: 1 is poor/high risk and 5 is strong/low burden.

| Architecture | Feasibility | Timing reliability | Low firmware coupling | NEUROGENIC scenes/songs | Stock engine/effects | Low implementation/test burden | Rank |
|---|---:|---:|---:|---:|---:|---:|---:|
| A. Rolling native-event materialization | 3 | 4 if a horizon is proved; 1 today | 3 | 4 | 5 | 2 | **2** |
| B. Prepared native sequence switching/double buffer | 2 | 4 in principle | 2 | 3 | 5 | 2 | **3** |
| C. Direct stock-engine voices | 3 | 2 with the demonstrated UI/timer paths | 1 | 5 | 4 | 1 | **4** |
| D. Hybrid: static/prepared native audio plus NEUROGENIC arrangement/MIDI | 4 | 4 for prepared audio | 3 | 5 | 5 | 3 | **1** |

### A. Rolling native-event materialization

This remains the closest fit to stock sound, effects, timing, and the already
verified adapter. Upstream contributes the missing clock implementation, not
the missing queue horizon. Until consumption/prefetch is measured, replacing a
future event can be late even when its command verifies before the event tick.

### B. Prepared sequence switching

Stock UI behavior indicates quantized sequence selection exists, which could
avoid mutating a playing container. The audit found only observers of selected,
present, and visually queued sequences—not a callable, ownership-safe switch or
a way to create disposable native sequence buffers without adopting stock
project structures. This is promising but less mature than rolling events.

### C. Direct stock-engine voices

Am MOD proves that private note-on/release calls can be made and shows the
substantial state needed around them. This best matches arbitrary NEUROGENIC
polymeters and scenes, but transfers scheduling, voice lifecycle, recorder
interaction, and exact release ownership into the firmware adapter. Its known
UI/message-timer arpeggiator is not evidence of audio-grade timing.

### D. Recommended hybrid

Keep NEUROGENIC's timeline authoritative. Continue using bounded native event
materialization for stopped/prepared audio windows, where it is physically
verified, while scene/song logic and future MIDI remain NEUROGENIC-owned. Add
only the read-only clock observer next. That evidence then selects one branch:

- if a conservative stock consumption horizon can be measured, advance A;
- if a safe native queued-sequence setter is established first, use B at coarse
  pattern/scene boundaries;
- if neither is safe, run one bounded direct-voice timing/lifecycle experiment
  based on Am MOD rather than designing another private API.

This is a decision sequence, not three simultaneous backends.

## Testing substitution matrix

| Question | Host/QEMU ABI fixture | virtual MPC | Physical MPC |
|---|---|---|---|
| Request ordering, capacity, partial cleanup, comparator rules | sufficient | optional | not needed for every change |
| AArch64 constructor/calling convention/container ownership | strong evidence | stronger stock-process evidence | final first-use validation |
| Hook fingerprint/install/rollback and stock callback arrival | limited | appropriate preflight | first physical confirmation still required |
| UI navigation, panel input frames, screen redraw, autorun | reducer only | appropriate and automatable | final control/visual acceptance |
| Native event appears in stock list and emulated playback path | fixture can inspect adapter only | appropriate | final audio confirmation |
| Transport tick monotonicity and semantic transitions | logic fixtures | good preflight | required for device contract |
| Queue/prefetch horizon, jitter, audio glitches, latency | cannot establish | cannot establish reliably under different buffers/TCG | required with objective capture |
| Direct-voice overlap, release, stealing, stock effects/routing | state model only | useful preflight | required |

## Exact remaining unknowns

1. `NowPosition` cadence and generation semantics for start, stop, continue,
   loop, seek, tempo change, and sequence change on the physical device.
2. Which exact sequence the observed engine tick belongs to at queued switches.
3. The stock engine's effective event-consumption/prefetch horizon and whether
   it varies with tempo, UI/audio load, loop wrap, or event density.
4. Whether a native sequence-selection command can be called with an owned
   target and verified completion without adopting persistent stock projects.
5. Whether bulk event commands reduce Undo growth without changing normalization
   or partial-failure behavior compared with the verified per-event adapter.
6. For direct voices: correct scheduling thread, deadline, stable sound/slice
   resolution, identical-note overlap, exact release, stealing, recorder side
   effects, and effects/routing equivalence.
7. A timestamped native MIDI-output path and its correlation to the audio clock.

## Smallest justified next milestone

Implement one firmware-1.3.0.12-only, read-only transport observer as an
upstream-compatible SDK extension:

1. adapt only Am MOD's validated `NowPosition` hook contract;
2. install it in the SDK's existing startup hook transaction;
3. publish fixed-size copied tick/running/serial/callback-count data, never
   firmware pointers;
4. add host and AArch64 fixture tests for validation, discontinuity, and
   fail-closed behavior;
5. run the artifact first in virtual MPC for hook/callback/stock restoration;
6. only then propose one physical observation run with no sequence edits.

This milestone does not authorize live replacement. A later physical horizon
measurement is required before A; source proof of a safe sequence switch could
instead redirect the project to B.

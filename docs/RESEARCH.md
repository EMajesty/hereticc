# Existing-project research

Research date: **2026-11-15**. Repositories were inspected at the commits linked below. Upstream projects can change after this snapshot.

HIL-0 rechecked the requested upstream heads on 2026-11-16. The SDK and
MINIMOD revisions remain `9487cbb` and `18d41fd`. The temporary root-SSH
reference remains `8334ca0` and still has no license, so NEUROGENIC does not
copy or redistribute it. `virtual-mpc-sample` at `15f732c` is MIT-licensed and
already provides scripted control-surface frames, autorun/preload testing, QEMU
screen capture, and an audio bridge; it is the preferred future Level C/D
backend rather than a new emulator. See [HIL-0](HIL-0.md).

HIL-1 retains that reuse decision. The SDK task implementation was reviewed,
but no supported external-to-UI callback scheduler was established. The
test-only bridge therefore confines I/O to its own worker and mutates only the
pointer-free reducer under a nonblocking callback boundary; it does not call
firmware drawing APIs or claim SDK input injection. See [HIL-1](HIL-1.md).

The first physical HIL-1.2 scenario proved that a reducer-only `OPEN` does not
establish the SDK Sample-page context: a normal visibility or non-Sample paint
callback closed it before the next mailbox command. The report cannot identify
which callback class ran. HIL-1.3 therefore gives only a bounded synthetic test
session ownership of page state; production page-loss behavior is unchanged.
This remains Level B reducer evidence, not SDK input or paint validation.

M0.4a adapts the minimal stock Steps add/remove command pattern from Am MOD
`6cdc67276f43cd39f64871cb7e36782481244c65` under its MIT license. Source and
AArch64/QEMU evidence cover the tag-3 constructor, native containers/strings,
hidden shared-result ABI, Step-controller command service, exact remove factory
and cleanup. The required live owner is specifically a visible
`StepEditScreen` callback; the ordinary Sample-page overlay does not provide
that service. This is host/QEMU evidence only.

M0.4b's pinned-source audit confirms the adapted signatures and hidden-result
bridges against those exact files. Am MOD treats an event-list pointer change
as a sequence-context change and exposes the current track at
`CurrentThings+0x140`; no stable native sequence UUID was established. The
hardened adapter therefore compares pointer-derived, non-dereferenceable tokens
for sequence, track, event list and Program within one short-lived process.

## Confidence labels

- **Verified:** directly supported by linked source/documentation or an explicit upstream hardware result.
- **Inferred:** a reasonable architectural conclusion, not a demonstrated NEUROGENIC capability.
- **Unknown:** evidence was absent or insufficient.

“Hardware-verified” below means the upstream author reports a device test. NEUROGENIC has not reproduced it.

## MPC Sample USB updater evidence

Akai's official
[MPC Sample update guide](https://support.akaipro.com/en/support/solutions/articles/69000877649-mpc-sample-firmware-update)
documents a browser updater at `mpc-sample.local` with `192.168.155.1` as the
fallback address. Owner observations on firmware 1.3.0.12 confirm CDC-NCM and
the updater HTTP service. The public inMusic
[AZ0x open-source release](https://github.com/inmusic-software/az0x-open-source)
provides third-party Linux sources for license compliance but no reviewed
updater-server or remote-object protocol documentation. Generic object/WebSocket
client code in the updater bundle is therefore not evidence of a supported
diagnostic API; see [B1-P2](INPUT-HOOK-B1-P2.md).

## Summary

| Project | Strategy | Relevance | License/reuse |
|---|---|---|---|
| [`mpc-sample-sdk`](https://github.com/ioma8/mpc-sample-sdk/tree/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39) | Rust `LD_PRELOAD` extension of stock app | Leading integration candidate | MIT; attribution/license preservation required |
| [`mpc-minimod`](https://github.com/ioma8/mpc-minimod/tree/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25) | Stock-app feature mod using SDK | Best example of SDK use and lifecycle | Cargo workspace declares MIT, but no root `LICENSE` was present at inspected commit; clarify before copying |
| [`am-mod`](https://github.com/AlekseyMalykh/am-mod/tree/6cdc67276f43cd39f64871cb7e36782481244c65) | Verified stock app copied, patched, and run from RAM | Strong evidence for deep stock UI/sequencer modification | MIT; preserve notice; broad firmware-specific patching raises maintenance/provenance review cost |
| [`mpc-sample-lab`](https://github.com/gospodindilly-ops/mpc-sample-lab/tree/3d482b94c918b96d174f31c411686f2b18b74ce6) | Hardware research and replacement sampler | Primary hardware/protocol reference | **No license file found:** public source is not permission to reuse code; facts may be independently documented |
| [`mpc-sample-tracker`](https://github.com/jeffwebb-to/mpc-sample-tracker/tree/4a13c1792ac9a4200567950ac9eb5c0b505348ce) | Standalone tracker replacing stock app while active | Useful sequencer/timing/UI reference, contrary to stock-preservation goal | MIT; attribution/license preservation required |

No external code or dependency has been copied into NEUROGENIC.

## `mpc-sample-sdk`

Inspected commit: [`e62ffd3`](https://github.com/ioma8/mpc-sample-sdk/commit/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39).

### Verified

- Typed callbacks cover buttons, pads, DATA encoder, knobs/CC, MIDI notes, paint, and control-surface events. Callbacks can forward or consume stock handling: [events](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/event.rs), [pads](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/03-pads.md).
- Drawing wraps stock graphics calls, with paint interception and repaint support: [drawing](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/08-drawing.md), [implementation](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/draw.rs).
- Pad RGB overrides use the app's native callback and can restore saved native colors; button LEDs are explicitly unsupported: [lighting](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/25-lighting.md).
- Typed helpers expose selected sample metadata/data, pad-to-sample lookup, slice count/markers, stock slice mutation calls, and Manual chop mode: [samples](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/09-samples.md), [slices](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/10-slices-and-trim.md).
- ALSA hooks observe input and can mix S32 data into output. The documentation forbids allocation, locking, drawing, or blocking in audio callbacks: [audio](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/11-audio.md).
- The loader targets 1.3.0.12. Upstream prose says 59 fingerprints, while the pinned implementation iterates 63 instruction words; NEUROGENIC treats the table as authoritative. Hook rollback limitations are documented in [SDK hook safety](SDK-HOOK-SAFETY.md): [firmware](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/17-firmware.md), [loader](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/loader.rs).
- Its generated SD package checks the firmware hash, copies the library to `/run`, installs a transient service drop-in, and restarts the app. Removing `autorun` and rebooting restores stock behavior; device disk is claimed unchanged: [build/install](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/22-build-and-install.md).
- License is [MIT](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/LICENSE).

The reuse decision is recorded in
[Reuse-first MPC Sample integration](REUSE-FIRST-INTEGRATION.md): the SDK is the
direct device dependency; MINIMOD is the primary SDK-consumer reference pending
license clarification; Am MOD is a native stock-engine research reference;
Sample Lab and Tracker are not product integration bases.

### Temporary USB SSH reference

Sample Lab references `orrpunk-art/mpc-sample-root-ssh` at
`8334ca085b3cff327f62a23cf0aad489dc8d578d`. Source inspection confirms a
runtime-only transient unit running the stock `sshd`, bound to
`192.168.155.1:2222`, with root public-key-only login and forwarding disabled.
Its author reports two successful 1.3.0.12 cold boots with stock UI/audio/pads
normal and no `ac50.service` restart. It retains a full root-shell attack
surface, does not stop merely when the SD is removed, and has no license file.
It is therefore a separately authorized external debugging reference, not a
dependency or prerequisite for the SDK page/input prototype.

### Inferred relevance

The SDK is the cleanest current bridge for a Rust extension that preserves the stock application. Its guarded firmware table and explicit unsafe boundaries are preferable to NEUROGENIC owning raw offsets initially.

### Unknown or unverified

- No reviewed public API clearly provides arbitrary, scheduled stock-engine voice start/stop with per-event sample/slice/pitch/parameter control.
- The audio callback is frame-oriented, but sequencing integration with the stock transport and sample-accurate trigger timing is not documented.
- Physical-device validation breadth, worst-case timing, hook coexistence, and behavior during project/page destruction need independent testing.
- Support beyond firmware 1.3.0.12 is absent.

## `mpc-minimod`

Inspected commit: [`3770fb7`](https://github.com/ioma8/mpc-minimod/commit/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25).

### Verified

- MINIMOD is a Rust SDK consumer that keeps the stock app and implements AutoTrim, pitch behavior, sample rendering/effects, slice writes, a custom menu, MIDI behavior, and a live looper: [README](https://github.com/ioma8/mpc-minimod/blob/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25/README.md), [Rust crate](https://github.com/ioma8/mpc-minimod/tree/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25/rust/crates/minimod/src).
- It demonstrates consuming/forwarding controls, custom paint, native sample/slice access, pad-light restoration, worker separation, audio taps, and host/emulator testing.
- Deployment is an SD `autorun` folder and is described as reversible by removing it or booting without the card.
- It targets firmware 1.3.0.12 and pins SDK v0.6.0 in the inspected lockfile: [Cargo files](https://github.com/ioma8/mpc-minimod/tree/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25/rust).
- The Cargo workspace declares `license = "MIT"`: [workspace manifest](https://github.com/ioma8/mpc-minimod/blob/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25/rust/Cargo.toml).

### Inferred relevance

MINIMOD is the reference implementation for SDK lifecycle, UI ownership, background work, and restoration. Individual patterns may inform an integration spike after licensing is clarified.

### Unknown or unverified

- It is not a general polyphonic sequencer and does not establish stock-transport scheduling accuracy.
- Its live looper mixes owned audio rather than proving arbitrary stock sample-engine triggers; its README says physical latency is not proven sample-accurate for every I/O path.
- No root `LICENSE` file was present at the inspected commit. The Cargo declaration is evidence of intent but insufficiently clear for copying non-Cargo documentation/assets or repository-wide code. Seek clarification before reuse.

## `am-mod`

Inspected commit: [`6cdc672`](https://github.com/AlekseyMalykh/am-mod/commit/6cdc67276f43cd39f64871cb7e36782481244c65).

### Verified

- Am MOD uses the stock autorun mechanism, verifies files and the stock app, copies the app to RAM, and patches the copy; it states that firmware and bootloader are not written: [README boot/build sections](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/README.md), [loader source](https://github.com/AlekseyMalykh/am-mod/tree/6cdc67276f43cd39f64871cb7e36782481244c65/sd-clone-trial/src).
- It deeply changes stock screens and behavior and reports physical confirmation for many pad-lighting, poly-record, event-editing, and step-sequencer paths: [feature matrix](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/docs/FEATURES.md).
- Its Steps page edits the same events as stock Step Edit, and its Chop/16 Levels editor retains event pitch, slice, velocity, length, modifiers, and microtiming according to the feature documentation.
- Reversion is by booting without the mod card/removing `autorun`; it tells users to save projects to External because Internal is temporary during the mod session.
- Build notes require the user's own local stock 1.3.0.12 image and contain extensive firmware-specific verification scripts; no firmware is distributed by the repository.
- License is [MIT](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/LICENSE), with additional trademark/non-affiliation notice in [NOTICE](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/NOTICE.md).

### Inferred relevance

Am MOD is the strongest evidence that stock sequence/event models, custom screens, poly recording, and detailed lighting can be modified. Its findings are valuable for identifying integration questions.

### Unknown or unverified

- Its patch architecture is large and coupled to exact stock binary layouts; adapting it would import substantial maintenance and safety burden.
- The reviewed documentation does not establish a reusable, stable sequencing API or sample-accurate external scheduler contract.
- Some feature rows are explicitly marked trial/candidate/pending. Those must not be treated as hardware-confirmed.
- Compatibility beyond 1.3.0.12 is not established.

## `mpc-sample-lab`

Inspected commit: [`3d482b9`](https://github.com/gospodindilly-ops/mpc-sample-lab/commit/3d482b94c918b96d174f31c411686f2b18b74ce6).

### Verified

- The project documents pad, pressure, button, knob, fader, encoder, button LED, and pad RGB protocols, carefully separating disassembly findings, hypotheses, and hardware checks: [protocol](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/docs/control-surface-protocol.en.md).
- It directly owns DRM display, raw-MIDI control surface, and ALSA audio after stopping the stock app. Its replacement Sample Mode loads WAVs and implements its own voices; it does not trigger through the stock audio engine: [README](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/README.en.md), [firmware source](https://github.com/gospodindilly-ops/mpc-sample-lab/tree/3d482b94c918b96d174f31c411686f2b18b74ce6/firmware).
- The replacement runs from RAM, restarts the stock service on exit, and reports no flash writes. The repository also contains a read-only eMMC backup script, which is outside NEUROGENIC's scope.
- Research and hardware checks target firmware 1.3.0.12.

### Inferred relevance

This is the best reviewed reference for raw hardware mapping and a useful independent check on SDK assumptions. Its standalone audio/display design is a contingency reference only.

### Unknown or restricted

- No `LICENSE` file or explicit reuse license was found. Do not copy code, tables, captures, fonts, or assets. Public availability alone does not grant reuse rights.
- It replaces the stock application while running, so stock samples, slices, effects, sequencing, and workflow are not available through its engine.
- Its recovery method is explicitly unverified; secure boot and several LED/protocol details remain open.

## `mpc-sample-tracker`

Inspected commit: [`4a13c17`](https://github.com/jeffwebb-to/mpc-sample-tracker/commit/4a13c1792ac9a4200567950ac9eb5c0b505348ce).

### Verified

- MPCTRACKER stops the stock app and directly owns DRM, raw MIDI, and ALSA. Its autorun runs the binary from RAM and restarts stock on exit: [README](https://github.com/jeffwebb-to/mpc-sample-tracker/blob/4a13c1792ac9a4200567950ac9eb5c0b505348ce/README.md), [hardware layer](https://github.com/jeffwebb-to/mpc-sample-tracker/blob/4a13c1792ac9a4200567950ac9eb5c0b505348ce/src/hw.c), [autorun](https://github.com/jeffwebb-to/mpc-sample-tracker/blob/4a13c1792ac9a4200567950ac9eb5c0b505348ce/autorun/tracker.sh).
- It decodes pads/buttons/encoder/knobs, draws a tracker UI, and drives pad/button/bank LEDs using the raw protocols.
- Its own mixer provides eight sequencer tracks plus pad-jam voices. Tick advancement occurs inside audio rendering with fractional frame accounting, so its own event timing is tied to rendered frames: [player](https://github.com/jeffwebb-to/mpc-sample-tracker/blob/4a13c1792ac9a4200567950ac9eb5c0b505348ce/src/player.c).
- Host self-tests cover pitch, row timing, slide limits, and effect memory: [self-test](https://github.com/jeffwebb-to/mpc-sample-tracker/blob/4a13c1792ac9a4200567950ac9eb5c0b505348ce/tools/selftest.py).
- README states it was developed mostly in the emulator; starting and audio were tested on a real unit, while remaining hardware testing was pending.
- It targets 1.3.0.12 and is [MIT-licensed](https://github.com/jeffwebb-to/mpc-sample-tracker/blob/4a13c1792ac9a4200567950ac9eb5c0b505348ce/LICENSE).

### Inferred relevance

Its frame-driven scheduler, tracker effects, input-thread audition, and host renderer are useful comparative designs for NEUROGENIC's core tests.

### Unknown or unsuitable as a base

- It uses its own WAV loader/mixer and cannot demonstrate stock-engine sample/slice/effects access.
- It currently has eight tracker tracks, not NEUROGENIC's sixteen independent polyphonic tracks.
- Because the stock app is stopped, extending it would abandon the primary preservation goal.
- Full physical timing, UI, MIDI, and recovery behavior remain unverified upstream.

## Capability matrix

| Capability | SDK | MINIMOD | Am MOD | Sample Lab | MPCTRACKER |
|---|---|---|---|---|---|
| Intercept pads/buttons/knobs/encoder while stock runs | Verified API | Verified use | Verified via patches | No; owns raw device with stock stopped | No; owns raw device with stock stopped |
| Custom screen while stock runs | Overlay/paint verified | Verified use | Deep custom UI reported | Replacement DRM UI | Replacement DRM UI |
| Pad LED control | Verified SDK API | Verified use | Reported/confirmed broadly | Raw protocol verified | Raw protocol implementation |
| Button LED control | Not in SDK API | Not established | Patched stock behavior | Raw protocol partly verified | Raw protocol implementation |
| Trigger stock sample engine arbitrarily | Unsafe pad-path calls verified; scheduler/voice contract unknown | Incoming-event replay proven; not a scheduler API | Deeper native playback shows feasibility; reusable contract unknown | No | No |
| Read stock sample/slice data | Verified typed helpers | Verified use | Deep internal access | No stock access while replacement runs | No stock access |
| Sequencing timing evidence | Audio callback available; stock integration unknown | Looper timing only | Stock sequencer edits; external scheduling unknown | Own engine only | Own frame-driven mixer + host tests |
| Reversible SD deployment | Documented | Documented | Documented | RAM replacement procedure | Documented |
| Firmware compatibility | 1.3.0.12 only | 1.3.0.12 only | 1.3.0.12 | Research target 1.3.0.12 | 1.3.0.12 |

## Foundation decision

Choose **C first**: implement the platform-neutral sequencer model, scheduler contract, serialization, and tests. In parallel only conceptually—not as a dependency yet—treat **A (`mpc-sample-sdk`) as the leading adapter**. The next device-facing milestone is a disposable integration spike, not a product feature.

Do not choose B now:

- MINIMOD is architecturally compatible but not a sequencer base and has a repository-level license ambiguity to clarify.
- Am MOD proves valuable capabilities but adapting its broad binary patch system would couple the project to undocumented layouts immediately.
- Sample Lab has no reuse license and replaces stock.
- MPCTRACKER is MIT and has useful algorithms, but its replacement mixer/application contradicts the current goal.

The decision should be revisited if the integration spike cannot trigger stock voices with adequate timing and parameter control.

## Effects and mixer research addendum

Research date: **2026-11-15**, using the same pinned snapshots above.

### Stock effects exposed by `mpc-sample-sdk`

**Verified in source/documentation:** the SDK wraps firmware factories for exactly three named processors—reverb, vinyl, and decimator—and renders a complete mono/stereo buffer in 512-frame blocks. It can write raw values to six parameter-array positions and allocates the rendered output. Reverb rendering appends and trims a tail. The module describes itself as applying firmware effects to a whole sample offline: [effect documentation](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/21-effects.md), [effect source](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/platform/effect.rs).

**Not established:** live insert attachment, automation safety, parameter names/ranges, processor latency, arbitrary catalog access, send/return routing, per-track buses, master replacement, or preservation of an existing processor's state. Parameter-array access is firmware-specific and should not become NEUROGENIC's public parameter model.

### SDK audio taps and output mixing

**Verified in source/documentation:** the SDK interposes on ALSA input/output calls. The output hook receives mutable signed-32-bit interleaved frames immediately before the app writes them and is documented for adding/mixing audio. It reports channel/rate/block geometry and ring-derived latency. Input hooks observe capture. Reentrant taps forward untouched, and the callback rules prohibit allocation, locks, UI work, and blocking: [audio documentation](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/11-audio.md), [audio source](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/audio.rs).

**Inference:** because the slice is mutable, replacement may be technically possible, but the supported documentation only promises additive mixing. NEUROGENIC must not claim a master insert until unity replacement, dry-path behavior, channel geometry, and restoration are tested.

**Not established:** the final tap's relationship to every stock effect/output route, isolated track buffers, callback deadline, format/rate stability, or a way to compensate different per-path effect latencies.

### Am MOD mixer research

Am MOD's inspected tree contains a substantially broader experimental mixer architecture: eight logical buses plus master, routing adapters, native effect catalog work, metering, and stock-path analysis. Its [mixer design](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/docs/MIXER_DESIGN.md), [native audit](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/docs/MIXER_NATIVE_AUDIT.md), [bus design](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/docs/MIXER_BUSES.md), and [native adapter](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/docs/MIXER_NATIVE_ADAPTER.md) explicitly distinguish fixture/ARM-QEMU checks from complete live integration.

**Verified at repository evidence level:** native object layouts and callbacks have been investigated; test adapters exercise stereo buffers, native processors, gain/pan/routing concepts, and a catalog broader than the SDK's three offline effects.

**Not verified for NEUROGENIC or as a general hardware API:** sixteen complete live track buses, every voice/source/output path, two shared feedback-safe sends, master replacement, persistence, latency compensation, CPU capacity, and physical-device behavior. Some Am MOD documents state that live voices, non-empty native insert chains, physical/multi-output, preview/live input, and other renderers remain incomplete or outside a scoped adapter. These findings justify experiments, not code reuse or capability claims.

### Consequences

- Option A, controlling stock effects, is promising but requires a new guarded live interface and parameter metadata.
- Option B, custom master DSP, may fit the SDK output location only if replacement rather than additive mixing is validated.
- Option C, true track inserts/sends, requires pre-sum isolated audio that the current SDK does not expose. It needs deeper reverse engineering or a custom voice/mixer architecture.
- Offline stock rendering is useful for destructive/export workflows but does not satisfy live performance FX.
- No reviewed source supports assuming that sixteen NEUROGENIC tracks equal sixteen stock audio buses.

## Stock trigger and UI feasibility addendum (M0.3a)

Research date: **2026-11-15**. Exact branch heads were queried and cloned for source inspection; no upstream code was copied into NEUROGENIC.

### Zero-hook compatibility implementation addendum (M0.3b-A)

NEUROGENIC now carries a patch, not generated upstream source, against SDK commit
`e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39`. The patch adds a compile-time
read-only probe mode using the SDK's actual fingerprint iterator. It does not
compile the normal loader, hook sites, patch engine, audio interposition, MIDI,
input, lighting, drawing, or feature-platform modules into the diagnostic.

Host fixtures exercise all 63 words matching, a changed word, unreadable word,
zero/unsupported image, missing image, repeated verification, and the
compile-time absence of feature hooks. A separate report test enforces the
768-byte bound and absence of raw addresses. The AArch64 ELF inspection finds
no defined dynamic exports, ALSA symbols, `mprotect`, `pwrite`, hook identifiers,
or writable/executable sections. This supports the zero-hook source claim but
does not establish device loader behavior or physical compatibility.

### Revisions and licenses

| Project | Revision inspected | License evidence | Result |
|---|---|---|---|
| `mpc-sample-sdk` | [`9487cbba3648afb29d8e3107e31a7ac930985220`](https://github.com/ioma8/mpc-sample-sdk/tree/9487cbba3648afb29d8e3107e31a7ac930985220), tag `v0.7.1`, dated 2026-11-16 | [MIT](https://github.com/ioma8/mpc-sample-sdk/blob/9487cbba3648afb29d8e3107e31a7ac930985220/LICENSE), Jakub Kolcar; manifest declares MIT | Current direct-dependency candidate. Relative to the physically fingerprinted v0.6.0 source, it moves image-base publication after fingerprint success, tightens UI-thread repaint safety, fixes lighting retry behavior and external MIDI classification, and adds slice snapshot/restore APIs. These changes remain source/host evidence until the new revision is physically exercised. |
| MINIMOD | [`18d41fd805beace4fed623653a250c46a4fa2243`](https://github.com/ioma8/mpc-minimod/tree/18d41fd805beace4fed623653a250c46a4fa2243), dated 2026-11-16 | Rust workspace/crates declare MIT; no repository-root license file was found | Current SDK consumer using SDK 0.7; whole-repository reuse remains ambiguous. |
| Am MOD | [`6cdc67276f43cd39f64871cb7e36782481244c65`](https://github.com/AlekseyMalykh/am-mod/tree/6cdc67276f43cd39f64871cb7e36782481244c65), dated 2026-11-15 | [MIT](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/LICENSE), AM Mod contributors | Primary native-sequencer evidence; M0.4a narrowly adapts the reviewed add/remove command and ABI pattern with notice preserved. |

The current SDK is Rust 2024, version 0.7.1, MSRV 1.97, and depends on `libc`.
Historical A/B experiments remain pinned to their reviewed v0.6.0 tree; M0.3c
pins v0.7.1 as the device crate's first external dependency and preserves its
MIT notice.

M0.3c now pins v0.7.1 directly. Its host build and package inspection pass, and
the owner physically validated normal hook load, paint/input behavior, close,
and stock coexistence on firmware 1.3.0.12. The SDK
still installs its full site table rather than a feature-selected subset, so
using only UI/input APIs does not narrow the live patch transaction.

### Stock voice path

**Verified in the SDK source:** physical pad press/release hooks decode `PadEvent`, ask the mod for `Forward` or `Consume`, and call the displaced stock handlers only when forwarded. The public unsafe [`midi::send_note_on`/`send_note_off`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/midi.rs) wrappers can call those displaced handlers directly. `send_note_on` takes live `input`, `source`, `pad`, `properties`, and velocity; `send_note_off` takes the matching input/source/pad. Their documented contract requires live app objects on their owning UI thread and says they bypass normal mod policy. The [pad guide](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/03-pads.md) confirms that consuming the physical event prevents normal stock sample playback.

This proves a callable stock pad-input path, not a complete voice API:

- no stock voice handle/token or completion is returned;
- the call addresses a pad/source, not a stable sample ID or independent slice;
- properties are borrowed firmware state, not documented per-event parameters;
- release matching and repeated/overlapping triggers of one sound are not specified;
- calls are UI-thread/lifetime constrained and accept no target audio frame;
- there is no documented contract for transport, stock-sequence coexistence, voice stealing, choke/mono behavior, or project changes.

M0.3d physical testing did not validate that call path. Its implementation
required nonzero input/properties plus `midi::notes_ready()` before even handing
the press to the UI reducer. The observed lack of track selection proves that
combined gate failed and the event forwarded; normal stock playback therefore
explains the audio. Source inspection confirms `notes_ready()` means both
displaced note sites are installed, while press callback entry itself proves
only the note-on site. M0.3d.1 decouples ownership and records only the three
presence bits needed to distinguish the failed term.

M0.3d.1 physically resolved the term as properties (`H1 I1 P0`). The SDK does
not synthesize that value: it forwards the firmware's original argument to the
displaced handler. M0.3d had already demonstrated that ordinary forwarding with
this null value sounds the stock pad. MINIMOD's current Rust implementation also
uses SDK-native forwarding/ownership rather than `send_note_on(null)`. M0.3d.2
therefore uses `Flow::Forward` for the first owned physical pair and consumes
overlap; it does not weaken the unsafe helper's contract.

M0.3d.2 physical testing confirmed first-pad forwarding and second-pad
suppression, but exposed stale ownership under repeated/random presses and an
unreliable deferred close. The pinned SDK has exactly `Press` and `Release` pad
actions; pressure is delivered separately as `SurfaceEvent`. It exposes the
physical pad index and callback-local input/source values, but no bank A-D
state. MINIMOD revision `18d41fd805beace4fed623653a250c46a4fa2243`
documents a different firmware source ID on physical release and uses input
identity as the stable fallback. M0.3d.3 adopts that source-supported behavior
in a NEUROGENIC-owned fixed ledger without copying upstream code. Bank buttons
remain forwarded and the prototype explicitly models only 16 physical pads.

M0.3d.3 did not resolve the physical instability, so M0.3e removes that ledger
and restores M0.3c input behavior. The playback investigation instead targets
the stock sequencer. SDK v0.7.1 has no sequence/event-list insertion API, and
MINIMOD shows that note replay enters below the stock pad-grid recording
observer. Am MOD revision `6cdc67276f43cd39f64871cb7e36782481244c65`
(MIT) provides the strongest reuse evidence: firmware 1.3.0.12 tag-3 notes are
56-byte inline events with 960-PPQ timing, and its Step implementation uses the
live Seq controller/event list plus native add/change/remove command results and
Undo. The recommended reuse is a narrow upstream-compatible SDK wrapper around
that UI-thread command boundary—not copied offsets or direct project-memory
writes in NEUROGENIC.

M0.4 re-inspected that exact SDK and Am MOD revision. The SDK still has no
sequence/event-list or Undo insertion surface. Am MOD's actual Steps path is
not a single callable constructor: it validates a live Step controller, Program,
sequence/event list and command service on its UI thread; resolves pad-to-note;
constructs a 56-byte tag-3 value; owns native multisets, strings and shared
results; and dispatches the stock add command. Its QEMU fixtures do not prove
live audio or Undo replay. NEUROGENIC therefore implements only copied typed
constructor arguments and an at-most-once adapter port. No upstream code,
offsets, raw event bytes, or native calls were added.
Current upstream heads were checked on 2026-11-17: SDK v0.7.4
`7d82a51279c4e9af4a571eb4479805caeb0f1f9c` still has no sequence insertion
surface, and Am MOD `119546dcd0078275393afc1155c4803f507cfcd6` has not changed
the reviewed Steps implementation/native map/ABI bridge since the pinned
revision. There is no newer upstream API to adopt directly.

The SDK's [`NoteEvent::replay_as_pad`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/midi.rs) rewrites an already-arriving MIDI event as a selected pad/16-Levels row, forwards it, and restores the bytes. MINIMOD pairs overlapping external MIDI presses/releases with a fixed-capacity ledger before using this path: [policy](https://github.com/ioma8/mpc-minimod/blob/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25/rust/crates/minimod/src/midi.rs). This supports safe interception/rewrite of incoming events; it does not create arbitrary frame-stamped scheduler events.

Am MOD constructs native step events and calls deeper engine play/release entries for Poly/Chords: [implementation](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/sd-mod-session/src/poly_record.c). Its comments and tests explicitly manage held-note maps, release choice, track/engine pointers, record queues, and native object generations. This is evidence that deeper stock playback control is possible with substantial reverse engineering, not a stable SDK contract and not proof of per-voice NEUROGENIC token mapping.

### Clock correlation

The SDK exposes a millisecond monotonic clock used by the app and an output callback immediately before ALSA write with block geometry and estimated input/output latency: [time](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/16-status-and-time.md), [audio](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/11-audio.md). It does not expose an absolute audio-frame clock tied to a note call. Correlation can be measured by maintaining a callback-local frame counter and observing recorded output onset, but neither sample accuracy nor a stable offset is currently verified.

### Drawing, repaint, and restoration

**Verified in source:** the SDK changes the Sample-page vtable paint slot. Its handler calls the stock paint first, records the component, then invokes the mod with the live graphics handle: [paint site](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/sites.rs). Public drawing wraps ARGB color, filled rectangle, and stock-font text with a size: [drawing source](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/draw.rs). Text is truncated to a 95-byte UTF-8 boundary; the font constructor uses the stock face index `0`. Repaint helpers target the observed component, and the optional pump retries every 60 ms because busy native work may coalesce/drop a request.

**Inferred:** a final full-screen rectangle can visually replace the completed Sample-page paint during that callback. This makes a distinctive 320×240 NEUROGENIC surface plausible.

**Unknown:** whether child/dialog/toast components paint afterward, whether every desired stock page uses the hooked class, sustained refresh cost, partial redraw behavior, font glyph coverage, and clean restoration under every page/error transition. There is no exposed bitmap/image primitive or custom-font loader. Leaving NEUROGENIC must clear pad-light overrides and request an unoverlaid stock repaint; the SDK's [lighting API](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/platform/lighting.rs) retains observed native pad colors and provides per-pad/all-pad restore, but button-light restoration is outside that API.

### Smallest discriminating stock-trigger experiment

Use one populated pad with transport stopped and no project writes. First forward a physical press/release as a baseline. In a separate run, consume that pad's physical event and, inside the same still-live press/release callbacks, call `send_note_on`/`send_note_off` with the callback arguments. Retain no firmware pointers afterward. An audio-output observer records only an atomic request sequence, callback-local cumulative frame count, and block peaks; offline host analysis compares call-to-onset block and release behavior.

Run successive, separately stoppable phases: one synthetic press/release; two overlapping presses of the same pad followed by releases; physical and synthetic overlap; then repeat with stock transport stopped versus playing. Abort on restart, stale-object indication, stuck audio, unmatched release, project mutation, or inability to restore native input/lights/screen. Passing demonstrates callable, repeatable pad-path audition and measurable block correlation only. It does not pass token-addressable polyphony or sample-accurate scheduling unless distinct releases and stable frame offsets are directly observed.
M0.3b-A2 uses the comparative removable-media identity rather than an autorun
directory heuristic: Am MOD hardware observations identify the user SD data
partition as `/dev/mmcblk1p1`, exFAT, mounted below `/media`; its guarded
launchers independently require the same source/path/filesystem/writeability
shape. NEUROGENIC additionally verifies exFAT filesystem magic and rejects every
ambiguous record. This is source-grounded comparative evidence, not a NEUROGENIC
physical result.

### M0.3b-A2 physical observation and A3 discovery analysis

The owner-run A2 test on firmware 1.3.0.12 produced four same-boot SD reports,
all with `image_discovery=missing` and zero fingerprint reads. Stock UI, pads,
and audio stayed operational, and card-free recovery was normal. This verifies
the preload constructor and guarded SD writer on that unit, not the firmware
base or compatibility gate.

At pinned SDK commit `e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39`,
`Firmware::find()` walks `dl_iterate_phdr`, ignores every entry whose
`dlpi_name` is nonempty, rejects a null or empty program-header table, and
accepts only a `PT_LOAD` relative range covering `APP_PROBE_OFFSET`. No matching
entry, no qualifying segment, and malformed header metadata all collapse to
`None`; the function does not compare `/proc/self/exe` or inspect
`/proc/self/maps`. A3's second patch instruments these branches only under
`probe-only`, retaining the original production finder and the 63-word gate.

### M0.3b-A3 process observation and A4 routing

A3 produced four reports with PID/PPID chains 409/1, 410/409, 411/410, and a
second 411/410 invocation. Loader-image counts were 8, 8, 8, and 7. Every
report classified `/proc/self/exe` as unexpected, found no executable mapping
for exact `/usr/bin/MPC Sample`, rejected the loader image because the probe
offset was outside its load segments, and read zero fingerprint words. This
proves the preload constructor ran in unexpected process contexts. It does not
prove what those executables were or whether the expected application later
inherits the preload.

The SDK package generator writes `Environment=LD_PRELOAD=...` into a transient
`ac50.service` drop-in and restarts the unit. That environment can apply to the
unit's launched commands and descendants, but the reviewed source lacks the
device's actual service definition and `ExecStart` chain. A4 therefore leaves
autorun unchanged and partitions reporting capacity by exact `/proc/self/exe`
identity: unexpected/unreadable contexts cannot consume expected slots.

### M0.3b-A4 physical result and B0 input-site selection

The owner-run A4 test reached the exact expected stock executable, discovered
the image, and matched all 63 words on firmware 1.3.0.12. Stock controls/audio
remained normal and card-free reboot succeeded. This closes the zero-hook
compatibility gate for that unit/run only.

Review of pinned `sites.rs`, `firmware.rs`, and `hook.rs` selects `DATA_HOOK` for
B0. Its SDK ABI is a single `unsafe extern "C" fn(usize, i32)` returning unit,
with no paired-site dependency. NOTE_ON/OFF require a pair; CC and BUTTONS use
firmware event layouts; SURFACE accepts a raw payload pointer. The B0 SDK patch
exposes only DATA preparation while excluding full sites, paint, ALSA, MIDI,
drawing, and platform features. This is source/host evidence, not proof that
the inferred encoder ABI or concurrent patching is safe on hardware.

### B1-P3.2g silent result and P3.2j autorun clarification

The first owner-run P3.2g package produced no report after about one minute;
stock boot and responsiveness remained normal and there was no obvious A4-like
hitch. This does not prove whether autorun was accepted. At pinned minimod
commit `3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25`, reverse-engineering records the
stock app's `sh "%s"` autorun call, automatic `askQuestion:false` behavior, root
`autorun/` placement, and the exact JSON+shell+salt check. The working directory
is not established. The same evidence says SD is `noexec`; P3.2g directly
executed its ELF there, unlike A4's copy-to-`/run` path. P3.2j therefore uses a
separate shell-only, no-clobber SD marker before any collector attempt.
The same physical SD card and volume label successfully published A2--A4
reports. Generic SD identity, exFAT formatting, and basic writability are thus
physical evidence, not leading P3.2g failure hypotheses. Recognition/check
acceptance, wrapper gates, firmware validation, and standalone ELF startup are
the ordered diagnostic boundaries.

The owner subsequently recovered a complete P3.2j marker on firmware 1.3.0.12:
autorun entry, canonical directory resolution, firmware-prefix validation, and
SD marker publication all passed. P3.2k therefore preserves the P3.2f collector
bytes and changes only launch integration: exclusive fixed-directory staging to
volatile `/run`, one foreground invocation, bounded stage reporting, and
post-exit cleanup. This is host-tested and not yet physical evidence.

P3.2k physically passed wrapper guards, RAM staging, invocation return, and
cleanup but returned status 1 without a report. All collector errors were
collapsed to that status before any observable write. Audit identified missing
Linux mount-field escape decoding (`\040`) as a concrete early defect. P3.2l
fixes it without relaxing SD identity and adds an allowlisted `/run` stage token
that the wrapper transfers into a bounded marker; this remains host evidence.

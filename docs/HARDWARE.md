# Hardware and compatibility

## Evidence policy

- **Verified** means demonstrated in reviewed source/documentation, often with an upstream hardware-test statement. It does not mean the NEUROGENIC team reproduced it.
- **Inferred** means supported by multiple observations or implementation structure but not directly demonstrated for NEUROGENIC.
- **Unknown** means no adequate evidence was found.

The HERETICC-era project has one owner-run, zero-hook A2 diagnostic observation on firmware
1.3.0.12. It verified constructor execution, four direct SD reports, normal
stock UI/pads/audio during that boot, and normal card-free recovery. It did not
discover the stock image or validate the firmware fingerprint.

## Known platform facts

The most detailed public hardware record reviewed is MPC Sample Lab at commit [`3d482b9`](https://github.com/gospodindilly-ops/mpc-sample-lab/tree/3d482b94c918b96d174f31c411686f2b18b74ce6). Its author reports these as hardware-verified for firmware 1.3.0.12:

| Area | Current knowledge | Confidence |
|---|---|---|
| CPU/OS | RK3566, four Cortex-A55 cores, 2 GB RAM, Linux with PREEMPT_RT | upstream hardware-verified |
| Display | 320×240 panel driven through DRM/SPI | upstream hardware-verified; SDK also documents 320×240 |
| Control surface | Separate MCU exposed as ALSA raw MIDI over UART | upstream hardware-verified |
| Pads | 16 velocity/pressure pads; channel-10 MIDI-like messages | protocol read from code and checked on hardware |
| Buttons | 24 button inputs; press/release messages on channel 1 | protocol read from code and checked on hardware |
| Continuous controls | MAIN VOLUME, K1–K3, and pad-volume fader are absolute CC; DATA encoder is relative CC; DATA push is a button | upstream hardware-verified |
| Pad LEDs | Per-pad RGB through SysEx; 7-bit color components | protocol and hardware-verified |
| Button/bank LEDs | Button LEDs via CC; some buttons have no LED; exact purpose of several extra LED controls remains open | partly verified |
| Audio | I2S/AK4558 path and ALSA PCM; stock and replacement projects use signed 32-bit streams at 44.1 kHz | upstream hardware-verified for their paths |
| Storage | SD is mounted `noexec`; `/data` is writable; root filesystem is protected by dm-verity | upstream hardware-verified |
| Removable SD data partition | Comparative evidence identifies `/dev/mmcblk1p1` as exFAT at `/media/<safe-name>`; the A2 probe required that identity plus `rw` and exFAT magic and successfully published four reports | HERETICC-era owner-run A2 observation |
| USB composite connection | VID:PID `09e8:205c` exposes USB audio, USB MIDI, and CDC-NCM; Linux bound `cdc_ncm` and received `192.168.155.8/28` from the device at `192.168.155.1` | owner-run observation on firmware 1.3.0.12; one unit |
| USB HTTP service | `192.168.155.1` served the React firmware updater as `AZ0X Web Server/0.5.1+git9826367`; Akai documents this address for updates | owner-run observation plus official vendor documentation; diagnostic access not established |

Primary references: [Sample Lab README](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/README.en.md), [control-surface protocol](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/docs/control-surface-protocol.en.md), and [audio/display notes](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/docs/audio-display.en.md).

Sample Lab also reports hardware use of the separate
`mpc-sample-root-ssh` kit at commit `8334ca0` on firmware 1.3.0.12. That kit
starts the stock OpenSSH server in a transient `/run` unit beside `ac50`, bound
to USB address `192.168.155.1` port 2222 with public-key-only root login. This is
upstream physical evidence, not a NEUROGENIC test. The kit has no license file,
and a root shell is a materially broader security boundary than the planned SDK
mod, so SSH is not part of the normal integration path.

The owner physically validated HIL-0 on firmware 1.3.0.12: its dedicated host
key, exact USB endpoint, firmware/process identity, volatile library transfer,
controlled `ac50.service` restart, M0.3e UI, and stock restoration worked.
HIL-1 initially added repeatable immutable activation and an in-process reducer
bridge under host tests; neither capability is evidence of physical input
simulation.

The owner then physically validated the HIL-1 library activation and restart,
live Level B `QUERY`, and stock restoration on 1.3.0.12. The initial reducer
state was closed, track/step 1, empty bits, forward/no-repaint/no-restore. This
validates in-process state access, not physical input injection or rendering.

The first complete HIL-1.2 owner run subsequently validated the one-command
build/deploy/activate/test/restore/report cycle and restoration after failed
assertions. `RESET` and synthetic `OPEN` passed, but a background SDK callback
closed the reducer before `SELECT 4`; three assertions passed and five failed.
Source inspection proves that visibility checks and non-Sample paint callbacks
are the only asynchronous page-loss closers, although the report cannot identify
which one ran. HIL-1.3 isolates synthetic Level B page ownership in test builds;
it remains host-tested until the same scenario is rerun physically.

M0.4a added an approval-gated firmware-1.3.0.12 Step-editor command adapter. Its
fixed offsets, vtable identities, command factories and object layouts are
source-backed by Am MOD and exercised by an AArch64/QEMU fixture, not by this
MPC. Loading the distinct experiment would add one guarded in-process
Step-screen vtable hook. M0.4c later physically exercised that guarded add path;
the historical host-only qualification no longer applies to insertion.

M0.4b binds arming, observation and removal to pointer-free identities derived
from the same live sequence, track, event-list and Program graph; adds a
bounded arm lease, nonblocking recursive-paint guard and same-pthread check;
and prepares an independently confirmed one-command experiment. Those safety
guards were host/QEMU-tested before the later physical M0.4c insertion.

The first M0.4b physical attempt reached a valid Step context and accepted the
host arm request, but remained armed with no command acceptance until host
timeout; stock restoration passed. This does not establish that the native
adapter or command ran. M0.4c adds direct post-arm paint/stage evidence and a
single operator DATA-detent repaint request while keeping native work on the
stock UI callback.

The owner-run M0.4c retry then confirmed one native add-command acceptance, a
note visible in stock Step Edit, audible playback through stock transport, and
successful automatic stock restoration. Its raw 56-byte lookup did not find
the event, so automated observation and removal did not occur. M0.4d replaces
that incompatible comparison with the stock event comparator and a unique-match
canonical receipt; at the M0.4d checkpoint this correction was host/QEMU-tested
but not yet physical.

The M0.4e owner run physically verified the correction: one unique event was
observed, exact removal completed, absence was observed with zero remaining
events, and stock restoration passed. The runner's failure label came from
missing the transient `remove_accepted` poll, not a device failure.

M0.5a adds a separate experimental build that renders and advances the grid
from that same Step Edit paint callback. Input callbacks publish only copied
cell intents. DATA is temporarily forwarded as an explicit repaint handshake;
no Step or graphics pointer crosses a callback boundary. This artifact is
host/QEMU-tested but has not run on hardware.

M0.5b adds no native operation. Its owner-operated HIL cycle exposes a bounded
`GRID QUERY` snapshot from the already established volatile mailbox, holds the
experimental build active for one physical acceptance test, and restores stock
in `finally`. Hook/counter evidence cannot establish audible output; only the
owner's explicit result is recorded as physical audio evidence.

The first M0.5b physical run confirmed the Step hook and 18 paints but showed
zero presentation activations and zero intents; no musical acceptance test was
possible. M0.5c therefore makes NEUROGENIC the default presentation on the
first valid Step paint. Hardware confirmed native insertion/removal, correct
stock event positions and audible stock-engine playback. It also showed raw
DATA magnitudes moving multiple grid cells, verification redraw waiting for a
later input, and incomplete stock pixels after presentation switching.

M0.5d follows the pinned SDK encoder contract and converts each nonzero raw
callback to one logical direction step. It performs one verification directly
after synchronous native command acceptance, while retaining bounded later
paint retries. SHIFT+DATA is forwarded only after presentation state changes so
the stock controller can schedule a full repaint. These fixes are host/QEMU
evidence until the owner repeats the guarded physical grid test.

These facts are research inputs, not permission to depend directly on raw protocols. An in-process extension should prefer a guarded adapter such as the SDK and retain stock ownership wherever possible.

## Controls relevant to NEUROGENIC

The SDK exposes decoded pad press/release and velocity; buttons with SHIFT/ERASE state; signed DATA encoder deltas; K1–K3 control changes; MIDI note events; and forward/consume behavior. See its [pad](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/03-pads.md), [button](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/02-buttons.md), [encoder](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/04-encoder.md), and [knob](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/05-knobs.md) pages.

Known limitation: the SDK lighting API covers the sixteen pad RGB LEDs, not button LEDs. Raw button-LED control is documented by Sample Lab but is not adopted by NEUROGENIC.

M0.3c is the first NEUROGENIC crate built against SDK v0.7.1. The owner
physically validated its load/restart, overlay, SHIFT+DATA ownership, DATA
cursor, pad selection, PLUS step toggling, close, and stock UI/control/audio
restoration on firmware 1.3.0.12. This does not validate M0.3d stock triggering
or every unusual page-loss path; see
[M0.3c SDK UI](M0-3C-SDK-UI.md).

The owner physically tested M0.3d: the overlay, DATA, and PLUS worked, but pads
did not update selection, a second pad played, and deferred close never armed.
This matches M0.3d forwarding before reducer ownership, so the audible samples
were ordinary stock input, not evidence of explicit SDK dispatch. Stock-only
recovery was normal. M0.3d.1 restores independent pad ownership and exposes
bounded paired-hook/input/properties presence bits for the next controlled run.

M0.3d.1 then physically confirmed `H1 I1 P0`: both note sites and input were
available, while the firmware supplied null properties. Selection/ownership
worked and pads were silent. SDK source confirms that ordinary `Flow::Forward`
preserves that exact null argument, and the earlier M0.3d sound demonstrates
the stock handler accepts it on the physical path. M0.3d.2 adopts this supported
forwarding path instead of violating `send_note_on`'s live-properties contract.

The owner then physically exercised M0.3d.2. The first owned pad selected its
track and played through stock forwarding, and a second held pad was suppressed.
Repeated/random input could temporarily leave pads unresponsive and deferred
close was unreliable. All test samples were one-shots, so continued sound after
release was expected stock behavior. Source review found no SDK bank identity
beyond physical pad 0-15 and found that pressure is a separate `SurfaceEvent`,
not a third `PadAction`. M0.3d.3 uses a bounded pad/input edge ledger, tolerates
press/release source-ID differences, leaves bank controls stock-owned, and
labels the UI as physical pads 01-16. These corrections are host-tested only.

M0.3d.3 then failed physical acceptance on firmware 1.3.0.12: ownership could
remain stale, deferred close was unreliable, pads could temporarily stop
responding, bank A differed from B-D, and flicker persisted. Stock-only recovery
was normal. M0.3e removes the complete M0.3d audition path and returns to the
physically validated M0.3c behavior: the open page consumes physical pad pairs
for 16-track selection and does not trigger samples. This restored package is
host-tested only. One-shot audio tails remain unrelated to input ownership.

## Display and lighting

The SDK provides stock-app paint interception and wrappers for color, rectangle, font, text, toasts, and repaint requests. Graphics handles are valid only during the paint callback. Its pad-lighting layer remembers native colors and can restore them, but overrides are last-writer-wins and restoration is the feature's responsibility. See [drawing](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/08-drawing.md) and [lighting](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/25-lighting.md).

At the pinned SDK revision, the paint hook replaces vtable slot 27 only on the firmware's Sample-page class. It invokes the original paint first and NEUROGENIC-style code afterward. Filling `(0, 0, 320, 240)` can cover that completed paint, but this is an overlay on one page class—not verified exclusive ownership of every child, dialog, toast, or later paint. The public primitives are ARGB color, filled rectangle, and one-line stock-font text with size; no image/bitmap blit, clipping API, custom typeface loader, font-family/style selector, or dirty-rectangle API is exposed. The SDK's repaint helper requests a broad rectangle and a 60 ms pump can retry coalesced/dropped requests. See [paint hook](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/sites.rs), [drawing implementation](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/draw.rs), and [drawing guide](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/08-drawing.md).

Custom bitmap glyphs are architecturally possible using filled rectangles, but usable cost and appearance are unverified; there is no current bitmap upload/blit API. Custom firmware fonts or global stock-screen theming are not established. Required measurements are paint duration, sustained refresh rate, dropped/coalesced repaint behavior, stock overlays after the hook, and restoration after page changes/errors.

## Sample, slice, and audio limits

The SDK documents typed access to the selected sample, pad-to-sample lookup, WAV metadata/audio reads, slice count/positions, stock reset/insert marker calls, Manual chop mode, audio input taps, and output mixing. These are verified at source/documentation level for SDK firmware 1.3.0.12, not by NEUROGENIC.

The SDK additionally exposes unsafe calls to the stock note-on/off handlers. This is a direct stock input path, but it needs live input/property objects and identifies source/pad rather than a persistent voice. It does not select an arbitrary sample or slice independently and does not correlate the call to an audio-frame timestamp. Therefore scheduler-quality stock triggering remains unverified.

Still unknown:

- a supported stock-engine API for arbitrary scheduled voice start/stop;
- whether per-event sample/slice, pitch, envelope, filter, and effects parameters can be changed without corrupting stock state;
- maximum safe polyphony and scheduling jitter while the stock UI is active;
- whether all stereo input/output and stock effects paths remain intact under the required hooks;
- stable sample identity across load, save, project switch, and pool mutation.

M0.4 host tests now cover one selected active grid cell becoming a bounded
current-sequence/current-track request at 960 PPQ. This is not a stock event.
The pinned SDK lacks the live controller/event-list/command/Undo API needed to
implement Am MOD's source-proven tag-3 insertion path safely. No native write,
HIL insertion command, event-list observation, transport playback, or audio was
performed.

## Proposed page-scoped input ownership

Ownership is a state machine, not a global key map. When NEUROGENIC is closed, every event is forwarded. On entry, the adapter captures the current page/surface generation; any mismatch or exit releases ownership, restores pad lights, and requests a stock repaint. A consumed press owns its matching release even if focus changes, preventing stuck stock notes or accidental release actions.

| Control | NEUROGENIC-owned page behavior | Default outside that scope |
|---|---|---|
| 16 pads | Grid/tracker selection, performance gestures, or explicit audition; velocity is note velocity, aftertouch ignored unless mapped | Forward press, release, and pressure |
| DATA turn/push | Cursor/value navigation and confirm when the focused widget declares ownership | Forward |
| K1–K3 | Focused parameter bank with pickup; consume only after pickup/explicit edit focus | Forward |
| Pad-volume fader | Wet/dry or macro morph with pickup in performance/MIX-FX | Forward |
| B1–B3 | Page-local soft actions shown on screen | Forward when no matching displayed action |
| SHIFT | Modifier only; do not consume a lone press until a NEUROGENIC chord is committed | Forward or replay the complete native pair if the chord is not claimed |
| ERASE | Delete/clear only in an editing focus, with press/release pairing | Forward |
| Transport | Observe by default; consume only in a future explicitly validated transport-owner mode | Forward |
| Mode buttons | Proposed exit/page requests only after conflict testing; no permanent activation key is reserved | Forward |

The SDK provides generic raw button codes and consume/forward decisions, but only a subset of codes has named constants. B1/B2, transport, mode-button, and fader mappings must be confirmed against the current page and physical device before use. Surface-pad events can duplicate the higher-level pad path; one router must deduplicate or assign distinct purposes rather than process both blindly.

## Effects and routing evidence

The following statements describe reviewed upstream interfaces, not NEUROGENIC hardware results:

- **Supported by the inspected SDK:** an output callback receives the app's signed-32-bit interleaved block immediately before the ALSA write and may add samples; input callbacks can observe interleaved or planar capture. Stream geometry and estimated input/output latency are exposed. The callback contract forbids allocation, blocking, drawing, and locks. See [SDK audio](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/11-audio.md).
- **Supported by the inspected SDK, offline only:** firmware factories can render reverb, vinyl, and decimator over mono/stereo buffers, with up to six raw parameter indices. This allocates output and is intended for whole-sample rendering, not a live insert: [SDK effects](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/21-effects.md) and [implementation](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/platform/effect.rs).
- **Needs reverse engineering:** safe replacement rather than addition at the final output; live stock-effect lifecycle; named/ranged parameters; transport synchronization; effect latency; tail handling; stock insert/send/master topology; and isolated per-pad/per-track buffers.
- **May require custom DSP:** performance effects, a true master chain, sends, or per-track processing where no suitable stock interface exists. Custom DSP still requires a replaceable audio point; adding a wet signal to an unchanged dry final mix is not a general insert.

Am MOD documents experimental stock mixer/effect work, including native effect catalog and routing tests, but its inspected mixer documents distinguish ARM/QEMU fixtures and adapters from physical-device confirmation and leave several live paths open. Treat this as evidence of promising internal structures, not a supported API or NEUROGENIC capability.

Stereo preservation means two channels remain distinct from stock voice/input source through every enabled NEUROGENIC stage to physical output. Mono-only processors require an explicit stereo policy; silent downmixing is not acceptable.

## Performance control inventory

The real control inventory available to a proposed performance mode is sixteen pressure-capable pads, SHIFT, ERASE, transport and mode buttons, three screen buttons (B1–B3), DATA turn/push, PLUS/MINUS, MAIN VOLUME, K1–K3, and the pad-volume fader. Exact button interception conflicts depend on the current stock page.

NEUROGENIC's default philosophy is: **“Pads decide what happens. Knobs decide how it happens. The fader decides how much.”** Pads engage/release effects; K1–K3 edit explicit parameters; the fader controls wet/dry or a selected morph. Velocity and aftertouch do not alter intensity by default. Aftertouch is optional modulation only.

No shortcut is reserved yet. In particular, MIX/FX entry, PAD BANK, 16 LEVELS, NOTE REPEAT, transport, and stock Knob FX may conflict with existing behavior. A future integration must produce a per-page ownership table and forward every unowned event.

## Firmware compatibility

All five reviewed projects target or were researched against **1.3.0.12**. The pinned SDK implementation checks 63 instruction words (22 hook sites, 20 guards, and 21 code-call entries); older upstream prose saying 59 is stale. The M0.3b-A probe reuses this actual table without calling the SDK installer. Firmware constants and object layouts remain version-specific.

Policy:

- 1.3.0.12 is a research target, not yet a NEUROGENIC-supported release.
- Every supported firmware version requires a separately reviewed address table, fingerprints, integration tests, and physical-device validation.
- Unsupported versions must fail closed and leave the stock application running.
- Do not infer compatibility from a similar version number or a successful load.

Physical A4 evidence confirms that one MPC Sample running 1.3.0.12 reaches exact
`/usr/bin/MPC Sample` and matches the complete 63-word table. The zero-hook
constructor left controls and audio normal and stock-only reboot succeeded. No
input hook has been hardware-tested. B0's DATA encoder ABI and thread/lifetime
behavior remain source-derived assumptions; see [B0 analysis](INPUT-HOOK-B0.md).

Physical B1-P2 evidence confirms a USB CDC-NCM network and stock updater HTTP
service on the same firmware version. The updater's confirmed application calls
are a version read and firmware upload; the latter restarts the device and is
prohibited for diagnostic discovery. Generic object/WebSocket client code in
the frontend and a 404 subtree response do not establish a diagnostic or command
API. See [B1-P2](INPUT-HOOK-B1-P2.md).

The first physical P3.2g zero-hook lifecycle diagnostic on firmware 1.3.0.12
booted normally and left UI/audio responsive, but produced no SD report and no
obvious A4-like startup hitch. The result is inconclusive about autorun entry.
This same physical SD card and volume label previously published A2--A4 reports,
so its identity, exFAT formatting, and basic writability are physically
demonstrated. Diagnosis now prioritizes autorun recognition, early wrapper and
firmware-guard behavior, then P3.2g's direct standalone execution from a
`noexec` SD; see [B1-P3.2j](INPUT-HOOK-B1-P3-2J.md). No hook was involved.

The subsequent physical P3.2j marker passed autorun entry, canonical package
directory, exact firmware-prefix, and SD marker-publication stages. These early
boundaries are now physically confirmed on firmware 1.3.0.12. P3.2k retains the
same reviewed collector but stages it to a fixed volatile `/run` path before one
foreground invocation; that revised executable-start boundary is host-tested,
not hardware-tested.

P3.2k subsequently returned collector status 1 with no report after successful
RAM staging and cleanup. Source audit found that the Rust collector did not
decode `\040` in `/proc/mounts`, unlike the physically successful shell path.
P3.2l corrects this while retaining exact device, exFAT, filesystem-magic,
descriptor-identity, and no-follow checks, and adds bounded volatile stage
evidence. Neither the fix nor collector entry is yet physically confirmed.

## Unknown hardware questions

- Exact end-to-end pad-to-sound and schedule-to-sound latency under stock-engine integration.
- Clock source and precision available from the stock transport/audio engine.
- Safe control and restoration of button/bank LEDs through an extension.
- Reliable recovery procedure if the device itself fails to boot.
- Whether a future firmware release changes autorun, service, security, or object layouts.

## M0.6 playback boundary

Physical firmware-1.3.0.12 evidence now establishes one native tag-3 note
inserted through the stock command service, uniquely observed, exactly removed,
verified absent, and audibly played by stock transport. M0.5c additionally
confirmed several interactive grid edits at their expected stock positions.
This supports native sequence materialization as the first audio backend.

It does not establish safe live batch replacement, a minimum edit-ahead horizon,
transport-correlated MIDI timing, arbitrary direct voice control, or release
behavior across stock pad modes. Those remain explicit M0.6 gates; see
[M0.6 playback architecture](M0-6-PLAYBACK-ARCHITECTURE.md).

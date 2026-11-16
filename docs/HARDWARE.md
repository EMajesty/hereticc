# Hardware and compatibility

## Evidence policy

- **Verified** means demonstrated in reviewed source/documentation, often with an upstream hardware-test statement. It does not mean the HERETICC team reproduced it.
- **Inferred** means supported by multiple observations or implementation structure but not directly demonstrated for HERETICC.
- **Unknown** means no adequate evidence was found.

HERETICC has one owner-run, zero-hook A2 diagnostic observation on firmware
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
| Removable SD data partition | Comparative evidence identifies `/dev/mmcblk1p1` as exFAT at `/media/<safe-name>`; the A2 probe required that identity plus `rw` and exFAT magic and successfully published four reports | HERETICC owner-run A2 observation |

Primary references: [Sample Lab README](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/README.en.md), [control-surface protocol](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/docs/control-surface-protocol.en.md), and [audio/display notes](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/docs/audio-display.en.md).

These facts are research inputs, not permission to depend directly on raw protocols. An in-process extension should prefer a guarded adapter such as the SDK and retain stock ownership wherever possible.

## Controls relevant to HERETICC

The SDK exposes decoded pad press/release and velocity; buttons with SHIFT/ERASE state; signed DATA encoder deltas; K1–K3 control changes; MIDI note events; and forward/consume behavior. See its [pad](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/03-pads.md), [button](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/02-buttons.md), [encoder](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/04-encoder.md), and [knob](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/05-knobs.md) pages.

Known limitation: the SDK lighting API covers the sixteen pad RGB LEDs, not button LEDs. Raw button-LED control is documented by Sample Lab but is not adopted by HERETICC.

## Display and lighting

The SDK provides stock-app paint interception and wrappers for color, rectangle, font, text, toasts, and repaint requests. Graphics handles are valid only during the paint callback. Its pad-lighting layer remembers native colors and can restore them, but overrides are last-writer-wins and restoration is the feature's responsibility. See [drawing](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/08-drawing.md) and [lighting](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/25-lighting.md).

At the pinned SDK revision, the paint hook replaces vtable slot 27 only on the firmware's Sample-page class. It invokes the original paint first and HERETICC-style code afterward. Filling `(0, 0, 320, 240)` can cover that completed paint, but this is an overlay on one page class—not verified exclusive ownership of every child, dialog, toast, or later paint. The public primitives are ARGB color, filled rectangle, and one-line stock-font text with size; no image/bitmap blit, clipping API, custom typeface loader, font-family/style selector, or dirty-rectangle API is exposed. The SDK's repaint helper requests a broad rectangle and a 60 ms pump can retry coalesced/dropped requests. See [paint hook](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/sites.rs), [drawing implementation](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/draw.rs), and [drawing guide](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/08-drawing.md).

Custom bitmap glyphs are architecturally possible using filled rectangles, but usable cost and appearance are unverified; there is no current bitmap upload/blit API. Custom firmware fonts or global stock-screen theming are not established. Required measurements are paint duration, sustained refresh rate, dropped/coalesced repaint behavior, stock overlays after the hook, and restoration after page changes/errors.

## Sample, slice, and audio limits

The SDK documents typed access to the selected sample, pad-to-sample lookup, WAV metadata/audio reads, slice count/positions, stock reset/insert marker calls, Manual chop mode, audio input taps, and output mixing. These are verified at source/documentation level for SDK firmware 1.3.0.12, not by HERETICC.

The SDK additionally exposes unsafe calls to the stock note-on/off handlers. This is a direct stock input path, but it needs live input/property objects and identifies source/pad rather than a persistent voice. It does not select an arbitrary sample or slice independently and does not correlate the call to an audio-frame timestamp. Therefore scheduler-quality stock triggering remains unverified.

Still unknown:

- a supported stock-engine API for arbitrary scheduled voice start/stop;
- whether per-event sample/slice, pitch, envelope, filter, and effects parameters can be changed without corrupting stock state;
- maximum safe polyphony and scheduling jitter while the stock UI is active;
- whether all stereo input/output and stock effects paths remain intact under the required hooks;
- stable sample identity across load, save, project switch, and pool mutation.

## Proposed page-scoped input ownership

Ownership is a state machine, not a global key map. When HERETICC is closed, every event is forwarded. On entry, the adapter captures the current page/surface generation; any mismatch or exit releases ownership, restores pad lights, and requests a stock repaint. A consumed press owns its matching release even if focus changes, preventing stuck stock notes or accidental release actions.

| Control | HERETICC-owned page behavior | Default outside that scope |
|---|---|---|
| 16 pads | Grid/tracker selection, performance gestures, or explicit audition; velocity is note velocity, aftertouch ignored unless mapped | Forward press, release, and pressure |
| DATA turn/push | Cursor/value navigation and confirm when the focused widget declares ownership | Forward |
| K1–K3 | Focused parameter bank with pickup; consume only after pickup/explicit edit focus | Forward |
| Pad-volume fader | Wet/dry or macro morph with pickup in performance/MIX-FX | Forward |
| B1–B3 | Page-local soft actions shown on screen | Forward when no matching displayed action |
| SHIFT | Modifier only; do not consume a lone press until a HERETICC chord is committed | Forward or replay the complete native pair if the chord is not claimed |
| ERASE | Delete/clear only in an editing focus, with press/release pairing | Forward |
| Transport | Observe by default; consume only in a future explicitly validated transport-owner mode | Forward |
| Mode buttons | Proposed exit/page requests only after conflict testing; no permanent activation key is reserved | Forward |

The SDK provides generic raw button codes and consume/forward decisions, but only a subset of codes has named constants. B1/B2, transport, mode-button, and fader mappings must be confirmed against the current page and physical device before use. Surface-pad events can duplicate the higher-level pad path; one router must deduplicate or assign distinct purposes rather than process both blindly.

## Effects and routing evidence

The following statements describe reviewed upstream interfaces, not HERETICC hardware results:

- **Supported by the inspected SDK:** an output callback receives the app's signed-32-bit interleaved block immediately before the ALSA write and may add samples; input callbacks can observe interleaved or planar capture. Stream geometry and estimated input/output latency are exposed. The callback contract forbids allocation, blocking, drawing, and locks. See [SDK audio](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/11-audio.md).
- **Supported by the inspected SDK, offline only:** firmware factories can render reverb, vinyl, and decimator over mono/stereo buffers, with up to six raw parameter indices. This allocates output and is intended for whole-sample rendering, not a live insert: [SDK effects](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/21-effects.md) and [implementation](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/platform/effect.rs).
- **Needs reverse engineering:** safe replacement rather than addition at the final output; live stock-effect lifecycle; named/ranged parameters; transport synchronization; effect latency; tail handling; stock insert/send/master topology; and isolated per-pad/per-track buffers.
- **May require custom DSP:** performance effects, a true master chain, sends, or per-track processing where no suitable stock interface exists. Custom DSP still requires a replaceable audio point; adding a wet signal to an unchanged dry final mix is not a general insert.

Am MOD documents experimental stock mixer/effect work, including native effect catalog and routing tests, but its inspected mixer documents distinguish ARM/QEMU fixtures and adapters from physical-device confirmation and leave several live paths open. Treat this as evidence of promising internal structures, not a supported API or HERETICC capability.

Stereo preservation means two channels remain distinct from stock voice/input source through every enabled HERETICC stage to physical output. Mono-only processors require an explicit stereo policy; silent downmixing is not acceptable.

## Performance control inventory

The real control inventory available to a proposed performance mode is sixteen pressure-capable pads, SHIFT, ERASE, transport and mode buttons, three screen buttons (B1–B3), DATA turn/push, PLUS/MINUS, MAIN VOLUME, K1–K3, and the pad-volume fader. Exact button interception conflicts depend on the current stock page.

HERETICC's default philosophy is: **“Pads decide what happens. Knobs decide how it happens. The fader decides how much.”** Pads engage/release effects; K1–K3 edit explicit parameters; the fader controls wet/dry or a selected morph. Velocity and aftertouch do not alter intensity by default. Aftertouch is optional modulation only.

No shortcut is reserved yet. In particular, MIX/FX entry, PAD BANK, 16 LEVELS, NOTE REPEAT, transport, and stock Knob FX may conflict with existing behavior. A future integration must produce a per-page ownership table and forward every unowned event.

## Firmware compatibility

All five reviewed projects target or were researched against **1.3.0.12**. The pinned SDK implementation checks 63 instruction words (22 hook sites, 20 guards, and 21 code-call entries); older upstream prose saying 59 is stale. The M0.3b-A probe reuses this actual table without calling the SDK installer. Firmware constants and object layouts remain version-specific.

Policy:

- 1.3.0.12 is a research target, not yet a HERETICC-supported release.
- Every supported firmware version requires a separately reviewed address table, fingerprints, integration tests, and physical-device validation.
- Unsupported versions must fail closed and leave the stock application running.
- Do not infer compatibility from a similar version number or a successful load.

## Unknown hardware questions

- Exact end-to-end pad-to-sound and schedule-to-sound latency under stock-engine integration.
- Clock source and precision available from the stock transport/audio engine.
- Safe control and restoration of button/bank LEDs through an extension.
- Reliable recovery procedure if the device itself fails to boot.
- Whether a future firmware release changes autorun, service, security, or object layouts.

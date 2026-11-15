# Hardware and compatibility

## Evidence policy

- **Verified** means demonstrated in reviewed source/documentation, often with an upstream hardware-test statement. It does not mean the HERETICC team reproduced it.
- **Inferred** means supported by multiple observations or implementation structure but not directly demonstrated for HERETICC.
- **Unknown** means no adequate evidence was found.

HERETICC itself has performed **no hardware testing**.

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

Primary references: [Sample Lab README](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/README.en.md), [control-surface protocol](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/docs/control-surface-protocol.en.md), and [audio/display notes](https://github.com/gospodindilly-ops/mpc-sample-lab/blob/3d482b94c918b96d174f31c411686f2b18b74ce6/docs/audio-display.en.md).

These facts are research inputs, not permission to depend directly on raw protocols. An in-process extension should prefer a guarded adapter such as the SDK and retain stock ownership wherever possible.

## Controls relevant to HERETICC

The SDK exposes decoded pad press/release and velocity; buttons with SHIFT/ERASE state; signed DATA encoder deltas; K1–K3 control changes; MIDI note events; and forward/consume behavior. See its [pad](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/03-pads.md), [button](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/02-buttons.md), [encoder](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/04-encoder.md), and [knob](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/05-knobs.md) pages.

Known limitation: the SDK lighting API covers the sixteen pad RGB LEDs, not button LEDs. Raw button-LED control is documented by Sample Lab but is not adopted by HERETICC.

## Display and lighting

The SDK provides stock-app paint interception and wrappers for color, rectangle, font, text, toasts, and repaint requests. Graphics handles are valid only during the paint callback. Its pad-lighting layer remembers native colors and can restore them, but overrides are last-writer-wins and restoration is the feature's responsibility. See [drawing](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/08-drawing.md) and [lighting](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/docs/25-lighting.md).

Full-screen HERETICC ownership while the stock app remains alive is plausible but not yet verified. UI refresh rate, screen-transfer cost, and interaction with long stock operations remain unknown.

## Sample, slice, and audio limits

The SDK documents typed access to the selected sample, pad-to-sample lookup, WAV metadata/audio reads, slice count/positions, stock reset/insert marker calls, Manual chop mode, audio input taps, and output mixing. These are verified at source/documentation level for SDK firmware 1.3.0.12, not by HERETICC.

Still unknown:

- a supported stock-engine API for arbitrary scheduled voice start/stop;
- whether per-event sample/slice, pitch, envelope, filter, and effects parameters can be changed without corrupting stock state;
- maximum safe polyphony and scheduling jitter while the stock UI is active;
- whether all stereo input/output and stock effects paths remain intact under the required hooks;
- stable sample identity across load, save, project switch, and pool mutation.

## Firmware compatibility

All five reviewed projects target or were researched against **1.3.0.12**. The SDK checks 59 firmware fingerprint words and refuses installation on a mismatch. Firmware constants and object layouts must be treated as version-specific.

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


# Dedicated controller protocol

## Transport decision

The first transport is USB CDC ACM serial at 115200 baud. CDC was selected
over USB HID because the controller needs bidirectional capability discovery,
version negotiation, variable contextual labels, diagnostics, and full-state
refreshes. CDC provides one ordered byte stream without encoding those
operations into a fixed HID report descriptor. Linux exposes it through the
standard TTY API, so the host adds no library dependency.

HID remains a possible later transport. Framing, protocol state, semantic
dispatch, and feedback live above `ControllerSerialTransport`; an HID adapter
can carry the same packets without changing the musical engine.

The application opens only the path supplied with `--controller PATH`. It does
not scan for or substitute another serial device. `--controller-id ID` also
requires an exact negotiated identity. Production should use both and a stable
`/dev/serial/by-id/...` path or an explicitly configured udev alias.

## Wire format

All integers are little-endian. Messages are bounded to 256 payload bytes and
268 total bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | magic `0x474e` (`NG` on the wire) |
| 2 | 1 | protocol major, currently 1 |
| 3 | 1 | message type |
| 4 | 2 | payload length, 0–256 |
| 6 | 4 | sender sequence number |
| 10 | N | payload |
| 10+N | 2 | CRC-16/CCITT-FALSE over all prior bytes |

The decoder resynchronizes after malformed bytes, rejects oversized and
CRC-invalid frames, and retains incomplete frames. A major-version mismatch is
fatal for that connection. Protocol 1.1 is current. A 1.0 device remains valid
and receives the original label-only `CONTEXT` payload.

## Messages and control IDs

Stable message IDs are `HELLO=1`, `HELLO_REPLY=2`, `CAPABILITIES=3`,
`BUTTON=4`, `ENCODER=5`, `ANALOG=6`, `LED=7`, `CONTEXT=8`, `STATE_SYNC=9`,
`HEARTBEAT=10`, `ERROR=11`, and `RESET=12`.

- `HELLO`: host major and minor (`u8`, `u8`).
- `HELLO_REPLY`: major, minor, device-ID length, firmware-string length, then
  UTF-8 identity and firmware bytes.
- `CAPABILITIES`: `u32` mask. Initial firmware advertises buttons, relative
  encoders, absolute analog inputs, RGB, contextual feedback and heartbeat.
- `BUTTON`: ID (`u16`) and pressed (`u8`). Both transitions are sent. See the
  stable control table below.
- `ENCODER`: ID (`u16`) and signed delta (`i16`). IDs 0–7 are the smooth upper
  encoders; ID 8 is the detented data wheel. No upper encoder push is assumed.
- `ANALOG`: ID (`u16`) and raw `u16`. IDs 0–3 are faders and ID 4 crossfader.
- `LED`: ID (`u16`), RGB (`0x00RRGGBB`, `u32`), brightness (`u8`).
- `CONTEXT` 1.0: ID (`u16`), active (`u8`), label length (`u8`), label.
- `CONTEXT` 1.1: ID (`u16`), flags (`u8`: active, bipolar, pickup), signed
  pickup direction (`i8`: -1 down, 0 acquired, +1 up), feedback revision
  (`u64`), then five `u8 length + UTF-8 bytes` fields: name, formatted value,
  unit, minimum and maximum. Names are truncated to 24 bytes, values to 16,
  units to 8, and bounds use short numeric formatting.
- `STATE_SYNC`: requests a complete feedback refresh.
- `HEARTBEAT`: opaque bounded payload echoed by the host.
- `ERROR`: bounded diagnostic data; never audio-callback logging.
- `RESET`: device reboot indication; negotiation restarts.

## Host behavior

`ControllerHost` converts packets into `PerformanceGrid`, semantic actions or
parameter operations and submits through `ApplicationService`. It never owns
audio-engine objects. Input and output queues are bounded and expose drop
counters. Serial I/O runs on a controller worker, never the audio callback.

Performance modes remain host-authoritative. RGB updates show selection and
playhead state, and state revisions suppress duplicate feedback. Reconnection
negotiates before a full refresh. Raw analog values stay separate from
normalized values. Soft takeover compares calibrated physical position to the
effective parameter target. Direction is observable; acquisition occurs within
the calibrated dead zone. Scene/automation changes invalidate acquisition when
the control is no longer near the new target. Firmware reports raw detents;
Shift selects 0.1× fine steps and deltas of four or more receive bounded 2×
host acceleration.

Named contexts select explicit parameters for sequencer, sample, instrument,
mixer, routing and modulation pages; they never depend on registry order. Every
control has an explicit target or is visibly inactive. Shift maps the first
lower buttons to Undo/Redo. GUI and serial use `build_controller_context()`.

## Stable control ID table

| IDs | Control |
|---:|---|
| 0–15 | performance grid, physical 2×8 |
| 16–23 | contextual lower buttons 1–8 |
| 24 | Play |
| 25 | Stop |
| 26 | Record |
| 27 | Shift |
| 28 | Select |
| 29 | data-wheel push |
| 30 | STEP mode |
| 31 | PAD mode |
| 32 | 16 LEVELS mode |
| 33 | 16 NOTES mode |
| 34 | previous page |
| 35 | next page |

Encoder IDs 0–7 are upper controls and 8 is the data wheel. Analog IDs 0–3
are faders and 4 is the crossfader. Protocol 1.0 retains its historical 24–27
mode-button interpretation; firmware using this table must negotiate 1.1.

Grid LEDs show programmed steps, playhead, selection, pad assignment and
velocity/note modes. Dedicated LEDs show mode, Play, Record, Shift and Select.
Only changes are sent, brightness is capped at 224, and queue overflow forces a
later full refresh rather than accepting a partial state.

## Simulator and diagnostics

`SimulatedController` uses the same session and application dispatch as serial.
Tests negotiate, toggle a real step, switch modes, exercise fader pickup,
receive RGB/context packets, disconnect and saturate feedback queues.

```sh
./build/dev/neurogenic-diagnostics --controller-protocol-test
./build/dev/neurogenic --gui \
  --controller /dev/serial/by-id/usb-NEUROGENIC_Controller
```

## Firmware requirements

Firmware must implement framing/CRC, bounded receive storage, negotiation,
stable identity, capabilities, press/release events, signed encoder deltas,
raw 16-bit analog values, RGB/brightness output, heartbeat, reset notification
and full output refresh. It should debounce buttons and retain calibration but
must not infer STEP/PAD musical behavior.

USB descriptors, pin mapping, electrical LED limits, bootloader and firmware
update behavior are outside this task. Current limitations: no HID adapter,
calibration is configured through the host API rather than persisted, hotplug
uses bounded path polling rather than udev notifications, and physical USB
latency/reliability is unmeasured.

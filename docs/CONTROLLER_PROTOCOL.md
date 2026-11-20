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
fatal for that connection. Minor versions may add ignorable message types.

## Messages and control IDs

Stable message IDs are `HELLO=1`, `HELLO_REPLY=2`, `CAPABILITIES=3`,
`BUTTON=4`, `ENCODER=5`, `ANALOG=6`, `LED=7`, `CONTEXT=8`, `STATE_SYNC=9`,
`HEARTBEAT=10`, `ERROR=11`, and `RESET=12`.

- `HELLO`: host major and minor (`u8`, `u8`).
- `HELLO_REPLY`: major, minor, device-ID length, firmware-string length, then
  UTF-8 identity and firmware bytes.
- `CAPABILITIES`: `u32` mask. Initial firmware advertises buttons, relative
  encoders, absolute analog inputs, RGB, contextual feedback and heartbeat.
- `BUTTON`: ID (`u16`) and pressed (`u8`). Both transitions are sent. IDs 0–15
  are the 2×8 performance grid, 16–23 contextual buttons, and 24–27 request
  STEP, PAD, 16 LEVELS and 16 NOTES. Dedicated controls receive later IDs.
- `ENCODER`: ID (`u16`) and signed delta (`i16`). IDs 0–7 are the smooth upper
  encoders; ID 8 is the detented data wheel. No upper encoder push is assumed.
- `ANALOG`: ID (`u16`) and raw `u16`. IDs 0–3 are faders and ID 4 crossfader.
- `LED`: ID (`u16`), RGB (`0x00RRGGBB`, `u32`), brightness (`u8`).
- `CONTEXT`: ID (`u16`), active (`u8`), label length (`u8`), UTF-8 label.
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
normalized values. Initial soft takeover arms near midpoint; a page-specific
target policy is a documented follow-up. Encoder acceleration belongs in the
host mapping layer; firmware reports unaccelerated detents.

The first eight registered parameters provide upper labels. Eight lower labels
describe host navigation. The GUI renders matching labels on its edges. The MCU
does not interpret parameter IDs or musical meaning.

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
context payloads omit formatted values/ranges, pickup is provisional, hotplug
uses bounded path polling rather than udev notifications, and physical USB
latency/reliability is unmeasured.

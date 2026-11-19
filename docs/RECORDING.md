# Audio recording and internal resampling

External processors add `external-fx-return` for an isolated return and
`external-fx-mix` for the compensated wet/dry result, with loop index as
`source_id`. Existing track, aux, physical-input and master sources are
unchanged. A loop need not reach the physical master to be recorded.

NEUROGENIC's first recording path uses one reusable `RecordingService`. A
preallocated stereo-capable buffer is the bounded real-time producer target;
the audio callback only copies samples and updates lock-free counters. The
application/control thread creates the immutable `Sample`, publishes it through
`SampleManager`, writes WAV files, and updates projects. There is no callback
allocation, lock, logging, filesystem access, decoding, or destruction.

The default capacity is 60 seconds at 48 kHz stereo (about 23 MiB). Embedders
may choose another capacity. When full, accepted audio is retained, excess
frames are counted, recording stops, and existing data is never overwritten.
State distinguishes Idle, Armed, Recording, Completed, Publication Pending,
Audio Active, Canceled, and Error. Snapshots expose captured/dropped frames,
capacity, peaks, overflow count, finalization time, and reserved memory.

## Signal flow and sources

Stable source identifiers currently select:

- `external-mono` and `external-stereo`: miniaudio duplex capture input;
- `master-pre`: the mixed sample/synth signal before master gain/effects;
- `master-post`: output after master gain and stereo delay.
- `track` plus stable track ID: post-fader timeline channel;
- `aux` plus bus index: post-gain aux before return gain;
- `physical-input` plus channel index: one device input independently.

Pre-effects excludes master gain; post-effects includes it and current master
processing. Both internal taps include samples, synths, and timeline playback.
Recording does not feed its destination slot back into the current callback;
the immutable sample becomes triggerable only after callback acknowledgement.

The adapter opens one miniaudio duplex device at 48 kHz with one or two capture
channels, giving input and output a shared device clock. Independent devices
with unrelated clocks are not supported; they need asynchronous drift
compensation. Output-only operation is unchanged. Use `--list-devices` to see
inputs and `--input NAME` to open duplex mode. Physical input operation has not
been verified in CI.

Track and aux sources do not need to be routed to the physical master. The
multichannel adapter accepts one to six inputs and outputs, but physical
multichannel behavior remains hardware-unverified.

Input monitoring is Off, On, or Auto. On mixes the current input block into the
output; Auto does so only while armed/recording. Latency is the backend duplex
buffer plus device latency. Internal-source monitoring is ignored to avoid a
feedback loop; the monitor mix occurs after the post-effects recording tap, so
it cannot be recursively resampled. There is no monitoring mixer yet.

## Transport and completion

Start can be immediate, next beat, or next bar. Quantized recording requires a
running authoritative transport. Fixed bars are converted once at arm time
using current tempo and time signature; later tempo changes do not resize that
recording. Manual stop completes at the next matching-source callback boundary.
Cancel discards captured frames. Project switching cancels active recording;
device stop reports an error. Overflow completes the retained prefix.

## Application, WAV, and projects

The action registry exposes source/destination/channel selection, arm,
start/stop/cancel, quantization, fixed bars, monitoring, WAV save, and pad
assignment. Pad assignment creates or reuses a full-sample slice after audio
activation, using ordinary slice persistence.

For a headless internal resample, run `neurogenic --interactive --headless` and
enter:

```text
record-start 20 master-post
record-status
record-stop
record-save recording.wav float32
```

External capture uses `--input NAME` with `external-mono` or
`external-stereo`. WAV export supports PCM16 and IEEE float32 with exact frame
counts; the in-memory capture stays float. Write failure leaves the audio-active
sample intact.

Saving a project with an unnamed recording writes float32
`recordings/slot-ID.wav`, then stores the usual relative reference. PCM is never
embedded. Missing recordings use normal inspectable/relinkable asset behavior.

## Current limitations

Only mono/stereo, one recorder, one duplex device, and master pre/post taps are
implemented. Six-channel routing, track/aux taps, separate-device clock
correction, streamed waveform display, input pair selection, overdub/looping,
and physical latency validation remain future work.

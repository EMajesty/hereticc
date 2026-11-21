# Live loop recording

Live looping is a control-side session layer over the existing bounded
`RecordingService`, immutable `SampleManager` publication, and arrangement
audio clips. It does not add a callback recorder or playback engine.

## Sessions and lifecycle

Up to eight stable-ID sessions may exist; one may capture at a time. States are
idle, armed, waiting, recording, finalizing, playing, overdubbing, replacing,
canceled, and error. Status includes source, destination, requested musical
boundary, actual callback frames, length, iteration, clip ID, overflow and
diagnostics.

Immediate recording starts at the callback position. Subdivision, beat and bar
quantization choose the next strict boundary in authoritative transport time.
This remains authoritative under external MIDI clock. Quantized capture
requires running transport. A fixed musical length is converted when armed;
tempo changes do not resize an in-progress pass.

Signed input-latency compensation adjusts the requested capture frame. It is
user supplied; no hardware latency is invented. Start/stop offsets inside a
callback use the existing bounded recorder.

## Publication and first playback

After capture, PCM is prepared on the control thread, published as an immutable
sample, and acknowledged before the arrangement changes. The first pass creates
a looping audio clip transaction at the first loop-length boundary still in the
future. First playback is deterministic but not zero-gap: publication and
timeline activation may defer it by up to one loop duration.

## Overdub and replace

Overdub creates a new version using `old * feedback + captured`; format and
frame count must match. Multiple passes stay phase aligned and never mutate
active PCM. Replace publishes the new pass without mixing. Both preserve clip
identity and musical position. Existing voices retain old immutable versions.

Clip changes participate in timeline history, but PCM version history is not
yet exposed as Undo/Redo. WAV saving remains explicit. Sessions and recording
buffers are transient; completed clips persist after their asset is saved.

## Sources and safety

Existing external, physical-input, track, group, aux and master taps are
supported. Monitoring reuses off/on/automatic policy. Capture or replace from
the destination track's own tap is rejected; intentional layering requires
overdub mode. Overflow completes the bounded pass and reports diagnostics.

Discovery capability `live-looping` version 1 is presentation neutral.

Known limitations: one simultaneous capture, explicit WAV save for durable
assets, no zero-gap first pass, no PCM-version Undo/Redo, and fixed pass length
after arming.

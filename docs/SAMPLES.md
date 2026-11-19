# Live sample management

`SampleManager` is the hardware-independent command/state boundary for sample
slots. Its stable semantic actions are `sample.load.v1`, `sample.replace.v1`,
`sample.unload.v1`, and `sample.query.v1`. A desktop UI, hardware adapter, Lua
binding, MIDI mapping, or remote protocol can call the same interface without
exposing audio-engine objects.

Loading is asynchronous. A bounded control queue feeds one preparation worker;
that worker performs file I/O, WAV decoding, allocation, and resampling. Slot
state is queryable as `empty`, `queued`, `preparing`, `prepared`,
`pending-activation`, `loaded`, or `error`. `prepared` is normally brief:
publication immediately follows preparation when capacity is available.
`pending-activation` means the immutable update is in the bounded audio queue;
`loaded` or `empty` means the audio callback has acknowledged the install or
unload. Progress is currently zero or one.

The primary source path/rate, engine rate, channels, frame count, and duration
always describe the audio-active sample. `pending_source_path` separately
describes the newest requested load while it is queued, preparing, prepared, or
awaiting activation; an unload has no pending path. Every accepted command has
a monotonic revision, and every accepted publication has a monotonic audio
sequence. Older completions may update active metadata only after their own
audio acknowledgement; they cannot replace the state, error, or pending
metadata of a newer revision. This makes polling deterministic for controller
feedback and UI state replication.

A failed replacement reports an error for the newest revision but retains the
previous audio-active sample and all of its metadata. If an earlier successful
publication was already pending, its later callback acknowledgement is still
reflected as active without erasing the newer failure.

Prepared samples enter a bounded version pool (64 versions by default and
configurable at engine construction). Its publication queue is allocated to
the same configured capacity. At callback boundaries,
the audio thread atomically changes which immutable version a stable slot ID
resolves to. Existing voices retain a pointer and version index for the old
sample and finish normally. New triggers resolve the replacement. Unload
removes the slot from new-trigger lookup but also lets active voices finish.
The callback only marks unreferenced old versions retired; the control thread
alone destroys them. Consequently the callback performs no allocation,
deallocation, file I/O, decoding, resampling, locking, logging, or scripting.
If long-lived voices plus rapid replacements exhaust the pool, the command
fails observably and the current sample remains intact.

The publication queue has one serialized control-side producer path (normally
`SampleManager`) and the audio callback is its sole consumer. A control-side
mutex also serializes direct engine publications and version reclamation; the
audio callback never takes it. Status queries only read an atomic
applied-sequence acknowledgement; they never inspect sample objects. Shutdown
drains preparation commands, but a prepared update remains
`pending-activation` until a subsequent callback consumes it. Destroy the audio
device before the engine so no callback can outlive the immutable version pool.

The WAV decoder supports mono and stereo PCM16, PCM24, PCM32, and IEEE float32
RIFF/WAVE files. Other containers and compressed formats are rejected with a
path-specific error. Source rates must be positive. Files already at the engine
rate are not converted. Other rates, including 44.1, 48, 88.2, and 96 kHz, are
converted during preparation with a replaceable 32-tap Hann-windowed sinc
converter. Output length is rounded to preserve duration, and the low-pass
cutoff follows the conversion ratio to limit downsampling aliasing. This is a
general-purpose offline preparation path, not time stretching: extreme ratios,
mastering-grade conversion, streaming very large files, and metadata chunks are
not yet specialized.

For a live CLI demonstration, audio may start empty:

```sh
./build/dev/neurogenic --interactive
```

Commands on standard input are:

```text
load 0 samples/kick.wav
replace 0 samples/another-kick.wav
status
status 0
unload 0
quit
```

The existing positional WAV, repeatable `--slot ID=FILE`, and `--map NOTE=ID`
forms remain supported. Local files under `samples/` are deliberately ignored
except for `.gitkeep`; tests generate temporary deterministic WAV fixtures.

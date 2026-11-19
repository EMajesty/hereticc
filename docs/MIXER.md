# Mixer and audio routing

NEUROGENIC uses a bounded mixer between instruments and the existing master
effects. GUI, controller, MIDI and future remote clients submit semantic
actions through `ApplicationService`; none mutate callback state directly.

## Signal flow

```text
timeline track -> sample/synth -> track channel --+-> master pre tap
                                 |                 |
                                 +-> aux -> return-+
physical input -> input channel ------------------+

master pre tap -> master gain -> master effects -> master post tap -> output
```

Direct track or aux assignments bypass the master and target a physical pair.
Bus-to-bus routing is not supported, so feedback cycles cannot be constructed.
Legacy pad, preview and compatibility-pattern voices without a timeline track
continue to the master compatibility path.

## Ownership and publication

`MixerConfiguration` is prepared and validated on the control thread. It holds
up to 32 track channels, six input channels, two stereo aux buses and six
physical outputs. A complete copy crosses a bounded SPSC queue and becomes
active at a callback boundary. Queue exhaustion rejects the entire change and
increments `mixer_publication_failures`.

The callback owns fixed 1024-frame track, input and aux buffers. It performs no
allocation, locking, file access or graph construction. Control-visible
configuration is a separate copy; meters use atomics.

## Channel and bus semantics

- Gain is linear `0.0..2.0`; pan is `-1.0..1.0` stereo balance.
- Mute removes a channel from master and post-fader sends.
- If any track is soloed, only soloed non-muted tracks reach post-fader paths.
- Pre-fader sends are before gain/mute/solo. Post-fader sends follow all three.
- Two aux buses provide gain, return gain, mute, optional native stereo delay
  insertion and optional direct physical output pairs.
- Master pre-effects recording is before master gain. Master post-effects
  recording includes master gain and effects.
- Peak meters use fixed callback-rate decay and sticky clip indication.

## Physical I/O

The miniaudio adapter accepts one to six capture and playback channels through
`--input-channels` and `--output-channels`. It opens one duplex stream, so input
and output share the backend/device clock. Output-only stereo remains default.
Device open fails if the requested layout is unavailable. Stored assignments
are retained and are never silently redirected to another channel.

`AudioEngine::process_io` and automated tests simulate six inputs and outputs.
Separate devices with unrelated clocks are not combined because that requires
drift compensation. Actual six-channel hardware has not been verified.

## Recording taps

Recording sources include `track` plus stable track ID, `aux` plus bus index,
and `physical-input` plus channel index, in addition to external mono/stereo
and master pre/post. Track taps are post-fader. Aux taps are after bus gain and
before return gain. A source need not be audible at the physical master.

## Persistence and controls

Projects store `mixer.ngm` in each committed generation. Older projects without
it receive unity channels generated from timeline track IDs. Settings follow
stable IDs when tracks reorder. Unavailable physical assignments remain visible
and silent.

The `mixer.*.v1` action family covers selection, gain, pan, mute/solo, sends,
direct outputs, inputs, aux buses and master routing. `mixer.master.gain`,
`mixer.selected.gain` and `mixer.selected.pan` also use the generic parameter
registry. The Mixer GUI supports pointer and non-touch selection, gain and
mute/solo editing.

The fixed capacities are real-time memory budgets rather than workflow
assumptions: 32 simultaneously mixed timeline tracks, six physical inputs and
outputs, two aux buses and 1024 frames per backend chunk. There is no arbitrary
graph, surround panner, automation recording or feedback routing.

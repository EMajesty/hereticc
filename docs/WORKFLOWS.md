# End-to-end workflows

These procedures are complemented by the four-tier
[integrated engine validation](INTEGRATION_VALIDATION.md), which uses production
engine and persistence paths headlessly and emits machine-readable timing and
queue diagnostics.

## Record a live pattern performance

Arm arrangement recording, choose replace or overdub, optionally enable
automation/count-in, and start at the desired transport position. Launch
patterns or a chain; transitions are timestamped at actual activation. Stop,
inspect pending counts/overflow, then commit or discard. Commit creates ordinary
editable clips and automation; Undo removes the whole pass. Save the project to
persist it. See `ARRANGEMENT_RECORDING.md`.

## Build a live set or song

Create reusable patterns through arrangement actions, edit their track events
transactionally, then either launch a pattern in live mode or place clips in
song mode. Queue live transitions with launch quantization. Saving the project
restores pattern and clip IDs, loop range, transforms, and launch settings.

The bounded `neurogenic_workflows` executable validates complete application
service paths without physical devices. It uses the production timeline,
sample manager, audio callback, mixer, recorder, MIDI scheduler, project I/O,
controller grid and GUI model. Deterministic PCM16 fixtures and simulated
six-channel audio/MIDI events are created in a temporary directory and removed
after each run.

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/neurogenic_workflows all
./build/dev/neurogenic_workflows beat-project
ctest --preset dev -R neurogenic_workflows --output-on-failure
```

Output is JSON Lines with schema `neurogenic.workflow.v1`; a rejected action,
wrong state, silent expected output, persistence mismatch, queue failure or
cleanup failure exits nonzero. The suite is intentionally short enough for
ASan/UBSan and CI.

## Create a beat

1. Create a project and add sample tracks with `project.new.v1` and
   `track.create.v1`.
2. Load WAV files into stable slots, select a slot, create equal or transient
   slices, and assign slices to semantic pads.
3. Select a track and use `step.toggle.v1`. Select the event separately before
   changing velocity, probability, condition, ratchets or parameter locks.
4. Change tempo with `transport.tempo-set.v1`; it edits the authoritative
   timeline and participates in project dirty state.
5. Save As, then reopen the directory project. Sample references, slices, pad
   assignments, timeline events and tempo are restored before playback.

The automated beat scenario loads two samples, creates and maps slices,
programs two tracks, applies a ratchet and sample-gain lock, asserts non-silent
audio, saves, constructs a fresh engine, reloads and asserts audio again.

## Use synth tracks and modulation

Create a performance macro with project, track, group or master ownership, add
one or more numeric destinations and choose their ranges/curves. Capture macro
values as a scene, then recall immediately or with a seconds/tempo-based morph.
When arrangement automation is armed, macro gestures remain a single editable
macro lane rather than duplicated destination lanes.

Create tracks with instrument `synth.subtractive.v1` and distinct stable
instance IDs. Use `instrument.instance-parameter-set.v1` for instance-local
values. A track-scoped modulation route must name both its track and intended
instance; ambiguous routes are rejected. Step notes, locks and automation use
the same authoritative timeline as sample tracks.

The automated synth scenario plays two independent instances with different
cutoffs, a track-local retriggering LFO, validates non-silent output and checks
instance parameters and scoped routes in the saved project.

## Mix, resample and slice

Select a mixer channel, set gain/pan/sends, choose `master-pre` or
`master-post` as the recording source, select a destination slot, then
arm/start/stop. Completed audio is published as an immutable sample version;
assign it to a pad and use the ordinary slice workflow. Existing voices retain
their old sample versions.

The automated workflow records master-post audio while a sample track plays,
checks the bounded recorder for frames and overflow, assigns the result to a
pad, then validates a simulated six-channel external return. Filesystem WAV
export and project-contained recording assets remain covered by focused tests.

## External effects

Configure a disabled loop first, select explicit send/return channels, validate
the routing, then enable it. Adjust send, return, wet/dry and manual latency;
save a named routing scene only after the configuration is valid. Scene recall
publishes one complete mixer snapshot. Never connect a physical return to its
own send without understanding the hardware feedback path.

The workflow suite configures a track loop, saves a scene and feeds deterministic
six-channel input through the production mixer. Actual cabling levels,
round-trip latency, feedback safety and scene-switch clicks require hardware.

## External MIDI and synchronization

Create a track, select its logical output port and 1-based channel with
`track.midi-destination-set.v1`, and sequence notes normally. Enable clock only
on explicit logical ports. Select external clock ownership before following
Start/Stop/Continue/SPP; clock forwarding remains opt-in and excludes its input
port.

The automated scenario asserts generated 24-PPQN clock and channel-remapped
notes, acquires simulated external clock, and round-trips port/channel, clock
source and forwarding preferences. Physical wire jitter, hotplug identity and
device latency remain hardware tests.

## Performance controls and text

STEP, PAD, 16 LEVELS and 16 NOTES resolve to semantic commands independent of
MIDI note numbers or button placement. A minimal controller can navigate with a
data wheel, Enter and Back. Text entry uses the shared `TextEntryModel`: direct
keyboard text, encoder character/action selection and the touch/pointer
on-screen keyboard all edit the same buffer. In Display settings, Enter opens
project-name editing; select `DONE` using the wheel or touch to commit.

## Coverage

| Workflow | Headless integration | Focused tests | Hardware-dependent remainder |
| --- | --- | --- | --- |
| Beat creation/save/reload | Implemented | Samples, slices, steps, locks, project I/O | Subjective playback |
| Independent synth tracks | Implemented | DSP, voice ownership, modulation | Controller feel/listening |
| Mixer and master resampling | Implemented | Recording, taps, WAV export | Physical input latency |
| External FX and scenes | Simulated six-channel path | Validation, scenes, compensation | Cabling, levels, round trip |
| External MIDI/clock | Simulated packets and timestamps | Dropout, SPP, panic, overflow | Wire jitter and hotplug |
| Grid/minimal navigation/text | Model-tested at 1024×600 | GUI/controller/hit testing | 8-inch touch and encoder QA |

The integrated benchmark combines eight sample tracks, four synth instances,
two aux sends, modulation, internal recording, MIDI clock and GUI state/layout
synchronization. Offline figures are regression data only and do not establish
Raspberry Pi 5, N150 or physical-device real-time performance.

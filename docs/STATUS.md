# Implementation status

Implemented and host-tested: PCM16/24/32 and float32 WAV loading, mono/stereo sample playback,
miniaudio callback output, device enumeration/selection, libremidi input
enumeration/selection, MIDI note/action dispatch, genuine Lua 5.4 execution,
validated versioned pattern APIs, sample-accurate frame offsets, rational
fractional-tempo scheduling, next-bar replacement, bounded real-time queues,
overflow reporting, variable callback sizes, and offline WAV rendering.

The local gain stage remains original NEUROGENIC code with an Airwindows-facing
identifier. It is not Airwindows DSP; exact upstream integration remains a
separate reviewed task.

Verified on this host before the Nix environment was added: dependency-enabled
build, automated tests, miniaudio null-device enumeration, continuous callback
playback to the null device, clean SIGINT shutdown, and graceful absence of
MIDI. That build lacked ALSA development headers and MIDI ports.

Task 003 adds a locked x86_64-linux Nix package, development shell, application,
and build/test check; a dependency-enabled CMake preset; direnv integration; and
GitHub Actions. Nix builds confirm that ALSA is found and libremidi compiles its
ALSA sequencer backend. In the headless verification environment, miniaudio
correctly fell back to Null and ALSA sequencer was compiled but unavailable at
runtime. Physical devices remain untested.

Task 004 retains `libasound` as an explicit runtime dependency so miniaudio and
libremidi can resolve their dynamically loaded ALSA symbols on NixOS without
`LD_LIBRARY_PATH`. The dependency-enabled Linux tests verify resolution with
that variable unset. On the development host, the unpackaged and `nix run`
executables enumerated PipeWire-compatible ALSA outputs, SSL 12 audio/MIDI, and
an A-Series MIDI keyboard without environment changes. Enumeration is verified;
audio playback and MIDI event delivery on physical hardware were not tested.
`--headless` explicitly requests null audio for CI.

Task 005 adds bounded configurable sample polyphony, deterministic oldest-voice
stealing, complete burst consumption with frame-offset ordering, and a dedicated
control dispatcher that isolates MIDI callbacks from mappings and Lua. Host
tests cover overlap, retriggering, simultaneous events, carried offsets, voice
and queue exhaustion, threaded dispatch, draining shutdown, variable buffer
sizes, and offline rendering. Sample replacement during live processing remains
intentionally unsupported; stop and rebuild the engine to load another sample.

Task 006 replaces the single-sample renderer with dynamically sized stable
sample slots. MIDI and sequencer notes share a configurable mapping, initially
notes 36–51 to slots 0–15, and voices retain immutable per-slot sample
references. The CLI accepts repeatable `--slot ID=FILE` and `--map NOTE=ID`
arguments while preserving positional single-WAV behavior. Host tests cover
multi-slot playback, same-slot polyphony, missing mappings, cross-slot stealing,
sequencer and MIDI selection, WAV rejection, Lua, offline rendering, timing,
and bounded queue overload.

Task 007 adds a UI-independent 960-PPQ musical timeline with stable track/event
identifiers, arbitrary per-track loop lengths, duration and explicit note
events, parameter-event representation, mute state, tempo/time-signature
metadata, and deterministic absolute tick-to-frame scheduling. Editable vectors
are compiled on the control thread into bounded callback-safe plans. The audio
engine plays multiple timeline tracks through existing sample slots and stops
track-owned voices on note-off. The original Lua/16-step scheduler remains a
next-bar compatibility lane and has an explicit general-timeline adapter.
Host tests cover multiple tracks, simultaneous ordering, arbitrary lengths,
mute, loop boundaries, tempo conversion, long-run variable-buffer timing,
duration note-off, sample slots, and legacy patterns. Parameter destinations,
tempo maps, arrangement, recording, and quantized timeline-plan replacement
remain future work.

Task 008 makes the audio engine transport authoritative for both the general
timeline and legacy Lua workflow. Host tests cover stop/play/restart, voice
cleanup, active-note replacement, shared tempo/position, durations crossing
loops, overlapping note identity, staging overload, scalable compiled plans,
short loops, large-position saturation, and versioned file round trips.
`NEUROGENIC_TIMELINE 1`, `--timeline`, and a three-track example provide
application playback without C++. Compiled plans no longer impose 32-track or
512-event musical limits; callback staging remains configurable and observable.
MIDI clock, tempo maps, beat-continuous live tempo changes, and physical-device
playback verification remain future work.

Task 009 replaces the fallible retired-plan queue with four control-owned plan
slots; audio only publishes state transitions and never frees plans. Per-track
cursors and a preallocated next-event heap remove full-pattern callback scans.
Voice ownership now includes track, target note-on ID, and loop occurrence, so
overlap, loop crossing, stealing, stop/restart, mute, and replacement cannot
release unrelated voices. Staging overflow defers events instead of discarding
note-offs and reports callback overruns. Host regression and leak-sanitizer
tests cover rapid replacement/reclamation, saturation, exact explicit targets,
stolen owners, dense backlogs, and deferred cleanup. The offline benchmark is
documented in `docs/BENCHMARKS.md`; it is not hardware real-time evidence.
Latency under load, hotplug, physical ARM audio/MIDI devices, and
controller-specific behavior also remain untested.

Task 010 adds live load, replacement, and unload through the asynchronous
hardware-independent `SampleManager` command/state boundary. Immutable sample
versions allow existing voices to finish while new triggers resolve the new
slot contents; callback-side work is bounded and reclamation remains on the
control thread. Host tests cover active-voice replacement/unload, independent
slots, rapid reclamation, failure preservation, concurrent playback/loading,
shutdown, PCM decoding, mono/stereo conversion, 44.1/96 kHz duration and pitch,
and observable state. WAV support now includes PCM16/24/32 and float32 with
preparation-time windowed-sinc conversion. Streaming decode, non-WAV formats,
mastering-grade selectable resampler profiles, hardware listening tests, and
sample-management Lua/MIDI bindings remain future work.

The sample-state consistency follow-up adds explicit queued, preparation,
publication-pending, and callback-acknowledged states. Active metadata and the
newest pending request are reported separately. Deterministic host tests cover
rapid same-slot replacement, mixed unload/load ordering, delayed callback
activation, failed replacement preservation, concurrent playback, and shutdown
with an unapplied publication.

The AArch64 enablement task exposes the complete Nix package, app, check, and
development-shell outputs for both `x86_64-linux` and `aarch64-linux`. The code
and dependency audit found no x86 SIMD baseline; scheduler arithmetic relies on
GCC/Clang 128-bit integers supported by both targets. CI is configured with a
native AArch64 hosted runner and an x86-hosted AArch64 compilation-only check.
A successful native AArch64 CI run is host-runtime evidence only; physical ARM
audio/MIDI devices, latency, and subjective playback remain unverified.

Task 011 adds the control-side `TimelineService`, detached revisioned
transactions, deterministic SplitMix64 streams, reusable transformations and
Euclidean generation, thin `neurogenic.v1.timeline` Lua bindings, and four
generative examples. Commits activate at the next audio callback through the
existing immutable plan pool. Musical-boundary commit scheduling, multiple
patterns per track, parameter destinations, and hostile-script sandboxing remain
future work.

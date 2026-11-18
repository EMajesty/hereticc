# Implementation status

Implemented and host-tested: PCM16 WAV loading, mono/stereo sample playback,
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
Latency under load, hotplug, ARM64, and controller-specific behavior also remain
untested.

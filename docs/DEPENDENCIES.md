# Dependencies and licenses

Pins live in `cmake/Dependencies.cmake`; downloads are opt-in. The Nix flake
locks the same upstream versions and supplies their source trees directly to
CMake, so sandboxed package builds do not download during compilation.

| Dependency | Pin | Purpose | License / note |
|---|---|---|---|
| miniaudio | 0.11.21 | audio device and WAV support | Public domain or MIT-0 |
| libremidi | v5.4.2 | native MIDI backend | BSD-2-Clause and MIT |
| Lua | 5.4.8 + SHA-256 | scripting VM | MIT |
| sol2 | v3.5.0 | C++/Lua binding | MIT |
| Airwindows | upstream review required | DSP collection | MIT |

Airwindows has no small conventional library target and its repository is very
large. This foundation therefore does not fetch or vendor it. `PurestGain`
provides the stable `airwindows.purest_gain.v1` integration point and a small
independently written smoothed-gain implementation. Importing the exact upstream
DSP source and notice is a separate reviewed dependency task; until then this
must not be described as bit-identical Airwindows DSP.

libremidi 5.x supports C++20 and fixed-size messages, but Linux backend choice
(ALSA, JACK, or PipeWire) affects system dependencies. That choice remains at
the adapter layer and must not leak into engine state. This slice enables its
ALSA sequencer backend when CMake finds the ALSA library and development
headers (for example Debian/Ubuntu's `libasound2-dev`); absence is handled as
no available MIDI input rather than an audio-engine failure.

The Nix shell/package supplies `alsa-lib` headers and libraries. miniaudio uses
its native ALSA backend, while libremidi uses ALSA sequencer; JACK and direct
PipeWire backends remain disabled and non-mandatory. PipeWire hosts normally
expose ALSA compatibility endpoints at runtime. Build inputs do not guarantee
that a host service is running or that `/dev/snd` is accessible.

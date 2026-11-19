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
| SDL3 | 3.4.10 from pinned nixpkgs | optional window, input and 2D rendering | zlib |
| Airwindows | upstream review required | DSP collection | MIT |

Sample-rate conversion is currently original NEUROGENIC code: a replaceable
32-tap Hann-windowed sinc preparation-time converter. It adds no dependency and
is covered by the repository MIT license. miniaudio's converter and
libsamplerate were evaluated, but using either would couple the dependency-free
engine target to a live-backend library or add another package for this initial
offline preparation path. The `SampleRateConverter` interface keeps a future
quality/performance replacement localized.

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
that a host service is running or that `/dev/snd` is accessible. Both adapters
load ALSA by soname, so the Linux live adapter also retains a direct ALSA ELF
dependency. This lets Nix record the pinned store path in RUNPATH while using
the normal system loader paths on non-Nix Linux.

The pinned miniaudio, libremidi, Lua 5.4, and sol2 sources build through their
portable Linux paths on x86_64 and AArch64. libremidi and miniaudio use ALSA on
both; no JACK or PipeWire development dependency is added. Lua's `LUA_USE_LINUX`
configuration selects POSIX/Linux facilities rather than an ISA-specific VM.
NEUROGENIC does not pass CPU-specific optimization flags to these dependencies.

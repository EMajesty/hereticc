# NEUROGENIC

NEUROGENIC is an open-source, hardware-independent C++20 audio and MIDI
workstation engine. Its first live vertical slice plays a WAV through miniaudio,
receives MIDI through libremidi, executes Lua 5.4 through sol2, applies generated
16-step patterns at bar boundaries, and retains deterministic offline rendering.

This is foundation software, not a DAW or a finished groovebox. The engine does
not assume a UI, controller, or hardware layout. Linux x86_64 is the initial
host; portable C++ and narrow backend interfaces keep AArch64 viable.

The sequencing foundation uses a UI-independent 960-PPQ timeline with stable
events, arbitrary pattern lengths, independent tracks, mute state, duration
notes, and deterministic sample-accurate loop scheduling. The 16-step Lua API
is retained as a compatibility workflow rather than the fundamental data model.

## Build and run

### Nix development environment

The committed flake supports x86_64 Linux and pins nixpkgs, miniaudio,
libremidi, Lua, and sol2 in `flake.lock`:

```sh
nix develop
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
./build/dev/neurogenic_benchmark
./build/dev/neurogenic --list-devices
./build/dev/neurogenic --headless --list-devices
./build/dev/neurogenic path/to/sample.wav
./build/dev/neurogenic --interactive
```

With direnv installed, run `direnv allow` once; `.envrc` enters the same flake
shell. Build the installable package with `nix build`, run it with
`nix run . -- --list-devices`, and run all locked build/test checks with
`nix flake check`.

### Non-Nix CMake

The existing workflow remains supported. Install a C++20 compiler, CMake,
Ninja, pkg-config, and ALSA development files first, then:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/neurogenic --list-devices
./build/dev/neurogenic --audio "DEVICE NAME" --midi "PORT NAME" \
  --script examples/generate.lua path/to/sample.wav
./build/dev/neurogenic --slot 0=kick.wav --slot 1=snare.wav \
  --slot 2=hihat.wav --map 52=2
./build/dev/neurogenic --timeline examples/multitrack.ngt \
  --slot 0=kick.wav --slot 1=snare.wav --slot 2=hihat.wav
```

Omit `--audio` or `--midi` to select the first/default device. Selectors are
the numeric index or a case-sensitive name substring printed by `--list-devices`. MIDI note
36 triggers the sample and executes [`examples/generate.lua`](examples/generate.lua),
which queues a randomized pattern for the next bar. Other note-on messages
trigger the sample. Ctrl-C stops playback. If MIDI is absent, audio continues;
an audio initialization failure exits with a diagnostic.

The positional WAV form remains a compatibility mode: it installs slot 0 and
maps every note to it. Multi-sample mode uses repeatable `--slot ID=FILE`
assignments. Stable slot IDs are unsigned integers and are not limited to 16;
the initial bank policy maps notes 36–51 to slots 0–15. Use repeatable
`--map NOTE=ID` assignments for slots or controller layouts outside that bank.
WAV input supports mono/stereo PCM16, PCM24, PCM32, and float32. Common source
rates are converted to the 48 kHz engine rate during loading; matching-rate
files bypass conversion.

`--interactive` may start without a sample and accepts `load ID FILE`,
`replace ID FILE`, `unload ID`, `status [ID]`, and `quit` on standard input.
Loading and windowed-sinc conversion run on a preparation worker while audio
continues. Existing voices finish on their old immutable sample after replace
or unload. See [live sample management](docs/SAMPLES.md).

`--timeline` loads the versioned, line-oriented `NEUROGENIC_TIMELINE 1`
interchange format before audio starts. It carries transport metadata, stable
track/event IDs, loop lengths, mute state, notes, velocities, and durations.
Sample paths and routing remain separate `--slot`/`--map` configuration. See
[`examples/multitrack.ngt`](examples/multitrack.ngt) and the
[timeline reference](docs/TIMELINE.md).

The engine provides 32 polyphonic sample voices shared across all slots by default. Embedders may set a
different fixed capacity when constructing `AudioEngine`; exhaustion steals the
oldest active voice deterministically. MIDI transport callbacks only enqueue
messages. Controller actions and Lua run on a dedicated control thread.

One transport frame position drives both general timelines and legacy Lua
patterns. Stop freezes scheduled time and cleans voices, play resumes, and
restart cleans voices and returns to musical position zero. Tracks loop at
their own pattern lengths.

`--headless` explicitly selects miniaudio's null backend. It is intended for
CI and diagnostic enumeration without an audio device, not normal playback.

The dependency-free build remains useful for core/offline development, but live
audio, MIDI, and genuine Lua require `NEUROGENIC_FETCH_DEPENDENCIES=ON`.

### Linux audio and MIDI troubleshooting

`--list-devices` reports the selected miniaudio and libremidi backends. A Nix
shell supplies headers and libraries at build time. The executable retains a
direct `libasound` dependency so Nix records its store path in ELF RUNPATH; no
manual `LD_LIBRARY_PATH` is required. Nix does not start host
audio services, create `/dev/snd` devices, grant permissions, or expose devices
inside containers. On a PipeWire desktop, NEUROGENIC uses PipeWire's ALSA
compatibility devices; PipeWire itself is not a build dependency.

- A null-backend diagnostic reports whether ALSA was linked but unavailable.
  Use `--headless` when null audio is intentional; otherwise check host audio
  services, device access, and the complete diagnostic.
- `ALSA sequencer compiled, but unavailable` commonly means `/dev/snd/seq` is
  absent or inaccessible. Check the `snd_seq` kernel module and user permissions.
- `(none discovered)` with an active backend means the backend works but has no
  visible device or port.
- An open error names the backend and distinguishes discovery from failure to
  open or start the selected device.

GitHub Actions runs `nix flake check`, `nix build`, and an explicit headless
enumeration smoke test. It verifies compilation, a resolvable retained ALSA
runtime dependency, deterministic tests, and graceful device absence, not
physical hardware compatibility.

See [architecture](docs/ARCHITECTURE.md), [live sample management](docs/SAMPLES.md), [musical timeline](docs/TIMELINE.md), [dependencies](docs/DEPENDENCIES.md),
[benchmarks](docs/BENCHMARKS.md), [Lua API](docs/LUA.md), and [controller mappings](docs/CONTROLLERS.md).

## License

Original NEUROGENIC contributions are MIT-licensed. Dependencies retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

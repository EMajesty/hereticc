# NEUROGENIC

NEUROGENIC is an open-source, hardware-independent C++20 audio and MIDI
workstation engine. Its first live vertical slice plays a WAV through miniaudio,
receives MIDI through libremidi, executes Lua 5.4 through sol2, applies generated
16-step patterns at bar boundaries, and retains deterministic offline rendering.

This is foundation software, not a DAW or a finished groovebox. The engine does
not assume a UI, controller, or hardware layout. Linux x86_64 is the initial
host; portable C++ and narrow backend interfaces keep AArch64 viable.

## Build and run

### Nix development environment

The committed flake supports x86_64 Linux and pins nixpkgs, miniaudio,
libremidi, Lua, and sol2 in `flake.lock`:

```sh
nix develop
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/neurogenic --list-devices
./build/dev/neurogenic path/to/48000Hz-pcm16.wav
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
  --script examples/generate.lua path/to/48000Hz-pcm16.wav
```

Omit `--audio` or `--midi` to select the first/default device. Selectors are
the numeric index or a case-sensitive name substring printed by `--list-devices`. MIDI note
36 triggers the sample and executes [`examples/generate.lua`](examples/generate.lua),
which queues a randomized pattern for the next bar. Other note-on messages
trigger the sample. Ctrl-C stops playback. If MIDI is absent, audio continues;
an audio initialization failure exits with a diagnostic.

The dependency-free build remains useful for core/offline development, but live
audio, MIDI, and genuine Lua require `NEUROGENIC_FETCH_DEPENDENCIES=ON`.

### Linux audio and MIDI troubleshooting

`--list-devices` reports the selected miniaudio and libremidi backends. A Nix
shell supplies headers and libraries at build time; it does not start host
audio services, create `/dev/snd` devices, grant permissions, or expose devices
inside containers. On a PipeWire desktop, NEUROGENIC uses PipeWire's ALSA
compatibility devices; PipeWire itself is not a build dependency.

- `Null (ALSA compiled, unavailable at runtime)` means native support was built
  but ALSA/PipeWire was inaccessible, as is normal in headless CI.
- `ALSA sequencer compiled, but unavailable` commonly means `/dev/snd/seq` is
  absent or inaccessible. Check the `snd_seq` kernel module and user permissions.
- `(none discovered)` with an active backend means the backend works but has no
  visible device or port.
- An open error names the backend and distinguishes discovery from failure to
  open or start the selected device.

GitHub Actions runs `nix flake check`, `nix build`, and a headless enumeration
smoke test. It verifies compilation, ALSA backend inclusion, deterministic
tests, and graceful device absence, not physical hardware compatibility.

See [architecture](docs/ARCHITECTURE.md), [dependencies](docs/DEPENDENCIES.md),
[Lua API](docs/LUA.md), and [controller mappings](docs/CONTROLLERS.md).

## License

Original NEUROGENIC contributions are MIT-licensed. Dependencies retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

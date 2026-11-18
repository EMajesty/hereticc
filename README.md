# NEUROGENIC

NEUROGENIC is an open-source, hardware-independent C++20 audio and MIDI
workstation engine. Its first live vertical slice plays a WAV through miniaudio,
receives MIDI through libremidi, executes Lua 5.4 through sol2, applies generated
16-step patterns at bar boundaries, and retains deterministic offline rendering.

This is foundation software, not a DAW or a finished groovebox. The engine does
not assume a UI, controller, or hardware layout. Linux x86_64 is the initial
host; portable C++ and narrow backend interfaces keep AArch64 viable.

## Build and run

Build the complete Linux application and tests:

```sh
cmake -S . -B build -DNEUROGENIC_FETCH_DEPENDENCIES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/neurogenic --list-devices
./build/neurogenic --audio "DEVICE NAME" --midi "PORT NAME" \
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

See [architecture](docs/ARCHITECTURE.md), [dependencies](docs/DEPENDENCIES.md),
[Lua API](docs/LUA.md), and [controller mappings](docs/CONTROLLERS.md).

## License

Original NEUROGENIC contributions are MIT-licensed. Dependencies retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

# NEUROGENIC

NEUROGENIC is an open-source, hardware-independent C++20 audio and MIDI
workstation engine. The first vertical slice is a small sample sequencer with a
16-step clock, boundary-safe pattern changes, simulated MIDI, a versioned
scripting boundary, an Airwindows-identified gain stage, and deterministic
offline rendering.

This is foundation software, not a DAW or a finished groovebox. The engine does
not assume a UI, controller, or hardware layout. Linux x86_64 is the initial
host; portable C++ and narrow backend interfaces keep AArch64 viable.

## Build and run

The dependency-free host core and offline example build without network access:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/neurogenic path/to/pcm16.wav
```

The CLI simulates MIDI note 36, queues the generative rhythm from
[`examples/generate.lua`](examples/generate.lua), and writes four seconds to
`neurogenic-demo.wav`. Omitting the WAV exercises graceful silent mode.

`-DNEUROGENIC_FETCH_DEPENDENCIES=ON` fetches pinned miniaudio, Lua 5.4, and
sol2. The native backend wiring is intentionally incomplete in v0.1; see
[`docs/STATUS.md`](docs/STATUS.md).

See [architecture](docs/ARCHITECTURE.md), [dependencies](docs/DEPENDENCIES.md),
[Lua API](docs/LUA.md), and [controller mappings](docs/CONTROLLERS.md).

## License

Original NEUROGENIC contributions are MIT-licensed. Dependencies retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

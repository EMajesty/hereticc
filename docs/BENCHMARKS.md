# Headless performance benchmark

`neurogenic_benchmark` is a reproducible offline microbenchmark built with the
test targets. It uses `std::chrono::steady_clock`, performs 4,000 scheduler or
render callbacks per case (1,000 for replacement), requires no audio/MIDI
device, and prints average wall time. Run it with:

```sh
cmake --preset dev
cmake --build --preset dev --target neurogenic_benchmark
./build/dev/neurogenic_benchmark
```

The cases vary stored-but-far event count, track count, callback frames, 1,024
simultaneous events with a 512-event staging buffer, 32 active sample voices,
and repeated compile/queue/callback replacement. The checksum prevents removal
of measured work. Results are sensitive to compiler flags, CPU frequency,
thermal state, and other host load; compare results from the same build and host.

Reference run on 2026-11-18, dependency-enabled Debug preset, AMD Ryzen 7
7800X3D, no CPU affinity or real-time scheduling:

| Case | Average |
|---|---:|
| 1 track, 64 far events, 256 frames | 6 ns/callback |
| 1 track, 65,536 far events, 256 frames | 7 ns/callback |
| 1 / 16 / 64 tracks, 64 events each | 8 / 22 / 100 ns/callback |
| 16 tracks, 64 / 1,024 frames | 38 / 41 ns/callback |
| 1,024 simultaneous events | 11,115 ns/callback |
| 32 sample voices, 256 frames | 44,625 ns/callback |
| rapid 8-track/64-event replacement | 10,587 ns/operation |

The far-event comparison demonstrates that steady callback scheduling does not
scale with total events away from the playhead. These are offline measurements,
not worst-case execution-time analysis and not evidence of glitch-free hardware
performance. Device callbacks, OS scheduling, cache interference, DSP growth,
and release compiler settings require separate measurement.

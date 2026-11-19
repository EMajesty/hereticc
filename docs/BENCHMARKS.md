# Performance monitoring and benchmarks

NEUROGENIC targets comfortable full-workstation operation on Raspberry Pi 5,
while the personal reference build is an Intel N150 with 16 GB RAM. These are
design targets, not measured claims. Faster machines provide feature headroom;
they do not justify baseline overhead or an x86-specific implementation.

## Live metrics

`AudioEngine` records one bounded callback sample using a monotonic
`steady_clock`: current, exponentially smoothed average (1/64 update weight),
peak duration, frame/rate-derived deadline, count, measured deadline overruns,
and active voices. The callback performs two clock reads, bounded voice
counting, and relaxed atomic updates. Monitoring can be disabled through
`AudioEngine::set_performance_monitoring(false)`. It performs no allocation,
locks, logging, file access, or waits.

A deadline overrun means measured engine processing exceeded
`frames / sample_rate`; it is not proof that the audio backend experienced an
XRUN. Backend XRUN reporting is currently unavailable. Callback load is engine
processing time divided by its deadline. Linux process CPU is process CPU time
divided by wall time and can exceed 100% when multiple cores are busy; system
CPU is aggregate utilization across all cores.

`SystemMonitor` reads `/proc` on a control-side thread at most twice per second.
It publishes availability-tagged process CPU, aggregate CPU, resident memory,
and total memory atomically. Unsupported platforms report unavailable values.
`ApplicationState::performance` combines these metrics with sample rate,
buffer size, voice capacity, transport and existing bounded-queue counters.

Run one headless machine-readable snapshot with:

```sh
./build/dev/neurogenic --stats
```

The schema is `neurogenic.performance.v1`. A one-shot snapshot normally reports
CPU unavailable because two periodic samples are required.

## Deterministic benchmark workloads

```sh
cmake --preset dev
cmake --build --preset dev --target neurogenic_benchmark
./build/dev/neurogenic_benchmark > benchmark.jsonl
```

Each JSON Lines record uses `neurogenic.benchmark.v1` and reports scenario,
CPU model, architecture, OS/kernel, compiler/build type, sample rate, buffer
size, preparation time, processing nanoseconds per buffer, real-time factor,
resident memory, and iteration count. Workloads cover idle, 8/16/32 voices,
44.1/48 kHz playback, dense scheduling, rapid publication, 44.1-to-48 kHz
preparation, and GUI frame construction at 1920×1200, 1024×800 and 1024×600.
Preparation is excluded from audio processing measurements. GUI measurements
cover snapshot synchronization, layout and draw-command construction; GPU
execution and presentation latency are not measured.

An optional dedicated benchmark machine can enforce a per-buffer ceiling:

```sh
NEUROGENIC_BENCHMARK_BUDGET_NS=100000 ./build/dev/neurogenic_benchmark
```

No budget is enabled in ordinary CI because shared-runner timings are not
comparable. Retain JSONL from both commits and use the same machine, power mode,
compiler, build type, and background load. Compare matching scenario records.
The checksum/schema test verifies deterministic workload execution and output,
not identical elapsed time.

Example Debug output from the 2026-11-19 development host included roughly
8.8 µs idle, 9.6/14.0/23.1 µs for 8/16/32 voices, and 3.2–3.7 µs for GUI frame
construction. These offline averages are neither worst-case analysis nor
evidence of Pi 5, N150, physical audio, GPU, XRUN, or thermal performance.

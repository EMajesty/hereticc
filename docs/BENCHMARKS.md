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
preparation, native synth at 1/8/16/32 voices, filter and delay enabled/bypassed,
mixed sample/synth playback, and GUI frame construction at 1920×1200, 1024×800
and 1024×600.
Task 019 adds sparse and dense parameter-lock scheduling, multiple automated
destinations, mixed sample/synth automation, and rapid automation publication.
Compilation/preparation remains reported separately from callback processing.
On the 2026-11-19 x86-64 Debug host, these scenarios averaged approximately
12.0 µs per 256-frame buffer for both sparse and dense locks, 11.9 µs for the
mixed automation case, and 9.5 µs for rapid publication. Dense-plan compilation
was 18.6 µs. These offline figures are not Pi 5/N150 or hardware real-time
claims.
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

Task 018 Debug measurements on the same x86-64 development host were about
7.7/30.7/54.7/104.9 µs for 1/8/16/32 filtered synth voices, 49.4 µs for 16
voices with the filter bypassed, 1.1 µs for enabled delay processing, and
110.6 µs for mixed sample/synth playback. These are scenario examples, not
portable thresholds or ARM measurements.
Task 020 adds `probability_conditions` and `maximum_ratchets_fill` scheduler
scenarios. Their reported preparation time is timeline compilation; processing
time measures bounded scheduling only. Offline figures are regression data, not
claims about Pi 5 or N150 real-time hardware behavior.

Task 021 adds `slice_triggers_16`, `slice_voices_32_overlapping`,
`slice_rapid_retrigger`, `waveform_peak_generation`, and
`transient_detection`. Playback reports callback work; analysis reports
preparation time separately. These host averages establish neither Pi 5/N150
limits nor physical-device latency.
On the 2026-11-19 Ryzen 7 7800X3D Debug run, the three slice playback cases
averaged about 21.1, 29.7 and 48.9 µs per 256-frame buffer; four-second waveform
peaks and transient analysis took about 0.14 and 0.74 ms respectively.

Task 022 adds `pitched_slice_voices_16`, `pitched_slice_voices_32`,
`reverse_slice_playback`, `choke_heavy_sample_playback` and
`gate_release_envelopes`. They measure callback playback only; setup and
callback-queue publication are outside the timed region. Results are offline
regression measurements, not physical Pi 5, N150 or XRUN claims.
On the 2026-11-19 Ryzen 7 7800X3D Debug run, these averaged about 19.2, 26.6,
19.7, 13.5 and 23.3 µs per 256-frame buffer respectively. The comparable
16/32-voice legacy slice cases measured 25.1/36.1 µs in the same run; workload
lifetimes differ, so this is regression context rather than a claimed speedup.

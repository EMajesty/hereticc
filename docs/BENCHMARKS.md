# Performance monitoring and benchmarks

Task 039 adds bounded 8-, 32- and 64-instance scenarios; the last uses 128
dense routes. They measure callback work after control-side preparation and do
not constitute Raspberry Pi 5 measurements.

On the 2026-11-20 x86-64 Debug host (Ryzen 7 7800X3D, GCC 15.3, 48 kHz,
256 frames, 200 iterations), 8/16, 32/64 and 64/128 instance/route cases
averaged 19.1, 35.4 and 68.7 microseconds per buffer respectively. Resident
process memory was 76.4 MB. These short offline regression measurements are not
physical Raspberry Pi 5 or Intel N150 results.

Native-chain scenarios cover four tracks with two processors each, and sixteen
tracks with one processor each plus both aux chains and master. Results remain
offline measurements rather than Raspberry Pi 5 hardware claims.

Task 038 debug-build sample (Ryzen 7 7800X3D, Linux 7.3, GCC 15.3, 48 kHz,
256 frames, 50 iterations): four tracks/two effects averaged 24.9 us per
buffer; sixteen tracks/one effect plus two aux and master averaged 46.5 us.
Resident process memory was approximately 51.5 MB. Run the benchmark on target
hardware before assigning a device budget.

Device-backed bring-up uses `neurogenic-diagnostics --stress SECONDS
--realtime` and emits `neurogenic.hardware-test-report.v1`. Callback deadline
misses are not backend XRUN claims. See
[hardware testing](HARDWARE_TESTING.md).

Mixer scenarios cover 4, 8 and 16 channels, both stereo aux buses and simulated
six-input/six-output routing. These host measurements do not establish physical
Raspberry Pi 5 or Intel N150 performance.
`mixer_two_external_fx_latency` adds eight tracks, two stereo sends/returns,
simulated six-channel I/O, wet/dry mixing and 128-frame compensation. Routing
preparation and snapshot publication remain outside the measured callback path.
On the 2026-11-19 x86-64 Debug host it averaged about 40.7 us per 256-frame
buffer (roughly 131x offline real-time factor). This is an offline regression
measurement, not physical I/O, Raspberry Pi 5, or Intel N150 evidence.

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

The executable uses 2,000 iterations per timed scenario by default. Pass
`--iterations COUNT` to choose a different workload. CTest deliberately uses
100 iterations: its `neurogenic_benchmark_schema` test exercises every scenario
(including under ASan/UBSan) and validates the JSONL summary, but is a bounded
correctness smoke test rather than a performance measurement. Run the command
above without an override when collecting comparable benchmark results.

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

Task 023 adds mono/stereo realtime stretch at 0.5×, 1.5× and 2×, 8/16
simultaneous voices, mixed ordinary/stretched playback and voice-state setup.
On the 2026-11-19 Ryzen 7 7800X3D Debug host, 1.5× mono/stereo measured about
24.3/26.1 µs, while 8/16 voices at 2× measured about 138.7/264.2 µs per
256-frame buffer (real-time factors 38.5×/20.2×). The mixed 8+8 case measured
140.7 µs. Voice setup was below useful timer resolution and allocates no cache.
These are average offline callback costs, not Pi 5/N150, worst-case or XRUN
claims; ratio-dependent voice lifetime also affects averages.
# Project metadata I/O

`neurogenic_benchmark` reports `project_save_metadata` and
`project_load_metadata` preparation time separately from callback processing.
These deterministic cases cover metadata only; sample decode/resampling time is
already measured by the existing preparation benchmarks and depends on asset
size and storage. The figures do not establish crash durability or hardware
real-time performance.

`internal_master_recording` measures post-effect callback capture separately
from the control-side immutable-sample copy reported as preparation time. It
uses deterministic silence and a preallocated buffer; WAV filesystem throughput
and physical-device latency are excluded.

Task 026 adds mixer scenarios for 4 channels, 8/16 channels with two active
sends, simulated six-channel I/O and rapid routing publication. On the
2026-11-19 Ryzen 7 7800X3D Debug host these averaged approximately 9.4, 29.8,
50.1 and 62.5 µs per 256-frame buffer. Rapid publication preparation averaged
1.2 µs and its idle 16-channel processing pass 12.5 µs. Resident process memory
was about 35.8 MiB. These are deterministic offline averages, not worst-case,
physical-device, Pi 5, N150 or XRUN claims.

Task 028 adds bounded scenarios for four active LFOs with 16 routes, 32 routes,
and tempo synchronization. Processing covers callback modulation and numeric
destination delivery; configuration preparation remains outside the timed
region. These are offline regression measurements, not Pi 5/N150 or XRUN data.
On the 2026-11-20 Ryzen 7 7800X3D Debug host, the 16-route, 32-route, and
tempo-sync scenarios averaged approximately 13.5, 15.9, and 13.9 µs per
256-frame buffer. They are averages, not worst-case or hardware claims.

Task 029 adds 4/8/16-instance synth workloads and 8/16-instance per-voice
LFO/envelope workloads. On the 2026-11-20 Ryzen 7 7800X3D Debug host, plain
instances averaged about 50.3/104.8/201.7 µs and the 8/16-instance voice
modulation cases averaged 105.1/206.1 µs per 256-frame buffer. Idle instances
are skipped. These are offline averages, not Pi 5/N150 or hardware guarantees.

Task 030 broadens the 16/32-route scenarios across synth, delay, sample, mixer
and external-FX numeric destinations. Timed work is callback dispatch and
effective-value application; configuration preparation remains excluded. A
20-iteration Debug run on the 2026-11-20 Ryzen 7 7800X3D host measured about
16.0/18.1/16.2 µs for 16 routes, 32 routes and 16 tempo-synchronized routes,
with approximately 38.8 MiB process RSS. These short offline averages are not
worst-case, Pi 5/N150 or physical-audio results.

Task 031 adds `midi_four_ports_dense_clock`: four explicit routes forward 16
note messages per iteration while four-port 24-PPQN clock generation runs and
the outbound queue is drained. It measures control/scheduling and queue
dispatch overhead only; libremidi, kernel scheduling, USB/DIN transmission and
physical jitter are excluded.
On the 2026-11-20 Ryzen 7 7800X3D Debug host, a 100-iteration run averaged
approximately 0.95 microseconds per 256-frame iteration (about 0.05% of one
5.33 ms buffer deadline). This is an offline average, not a wire-latency claim.

Task 032 adds `midi_external_clock_tracking`, which feeds deterministic jittered
pulses through the median/frequency/phase follower and advances one 1000-frame
interval per iteration. It measures only bounded tracking arithmetic, not
libremidi or physical transmission.
On the 2026-11-20 Ryzen 7 7800X3D Debug host, a 100-iteration run measured
approximately 27 ns per follower update; the combined four-port dense routing
and internal-clock case measured approximately 1.03 microseconds per 256-frame
iteration. These short offline averages are not worst-case hardware results.

Task 033 adds `integrated_8_sample_4_synth_mod_aux_record_midi` and
`integrated_gui_state_sync_1920x1200`. The first times only callback processing
for eight sample tracks, four synth instances, active modulation, two aux sends,
internal master recording and MIDI clock; MIDI dispatch and GUI work stay
outside that timer. The second reports remaining per-iteration GUI/state,
MIDI-dispatch and harness cost. A 100-iteration Debug run on the 2026-11-20
Ryzen 7 7800X3D host measured 218.8 microseconds per 256-frame callback
(24.4x realtime, approximately 4.1% of one callback deadline), a 373.9
microsecond observed callback peak, zero deadline overruns, MIDI drops and
recording overflows, about 15.0 microseconds of GUI/state overhead, and about
40.4 MiB RSS. This is a short offline regression
run, not N150, Raspberry Pi 5, GPU-presentation or hardware-XRUN evidence.

# Integrated live-performance validation

`neurogenic_integration_validation` contains a bounded production-path scenario
for the live-performance stack. It complements focused subsystem tests and does
not replace physical audio/MIDI testing.

## Covered workload

The normal run executes four 48 kHz workload tiers and one integrated session.
Together they cover sample and synth patterns, stretched and overlapping audio
clips, equal-power crossfades, group/aux/master routing, native FX, modulation,
macro morphing, MIDI clock input/output, internal recording, coordinated scene
launches, deterministic follow transitions, performance capture, transactional
arrangement commit, immutable sample publication, and gapless live-loop handoff.

Callback partitions repeat 256/127/65/191 frames in the tier workloads. The
session additionally renders 31/127/99-frame partitions and checks finite PCM,
scene-frame equality, resolved follow identity, capture history, and live-loop
handoff frame equality. Focused tests retain exact PCM envelope, no-gap,
no-duplicate-frame, seeking, overdub, and ownership assertions.

```sh
cmake --preset dev
cmake --build --preset dev -j2
./build/dev/neurogenic_integration_validation
```

## Optional soak

The soak workload is excluded from CTest. It repeatedly creates, runs, and
destroys the medium workload and integrated session, including project
round-trips, sample publication, scenes/follows, capture, and live-loop handoff.
It reports completed passes and process RSS growth.

```sh
./build/dev/neurogenic_integration_validation --soak-seconds 3600 \
  | tee live-performance-soak.jsonl
```

Any nonzero exit indicates an assertion failure. RSS growth is diagnostic rather
than a leak verdict; compare long runs under sanitizers and allow for allocator
caching.

## Timing guarantees

- Coordinated scene members use one prepared activation frame.
- Follow actions resolve to one bounded prepared transition at the source end.
- Performance capture stores acknowledged activation frames, not request time.
- Gapless live-loop playback begins at the completed capture boundary.
- Physical MIDI transmission, backend latency, and XRUN timing require hardware
  measurement and are not inferred from these tests.

## Real-time audit

The audit covered callback entry points, timeline scheduling, clip/stretch
processing, live-loop handoff, recording, modulation, FX, and mixing. Timeline
vectors and scheduler heap storage are reserved during control-side compilation.
Live-loop sample allocation occurs while arming, before capture. Publication
acknowledgements are bounded queues; sample destruction and arrangement edits
remain control-side. No new callback allocation, lock, logging, filesystem call,
or unbounded traversal was found.

## Results and limitations

On the 2026-11-21 Ryzen 7 7800X3D development host, a Debug run observed about
0.061/0.140 ms light, 0.208/0.406 ms medium, 1.273/2.163 ms heavy, and
1.281/2.121 ms stress average/worst callback time, with no deadline misses, MIDI
drops, or recording overflow. These are short workstation regression results,
not N150 or Raspberry Pi 5 measurements.

The scenario uses deterministic simulated audio/MIDI. Physical USB clock jitter,
duplex drift, XRUN reporting, latency, thermal behavior, and hour-scale stability
remain unverified.

On N150, retain this simulated result separately from the physical 64/128/256/
512-frame profiles in [N150_BRINGUP.md](N150_BRINGUP.md). Only device-backed
runs establish backend stability; neither substitutes for loopback measurement.

A three-second lifecycle soak completed 289 passes. RSS rose about 44 MiB during
initial allocator/cache warm-up and then matched the one-second run's plateau;
this is not a substitute for the documented hour-scale hardware soak.

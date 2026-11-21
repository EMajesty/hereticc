# Integrated engine validation

`neurogenic_integration_validation` is a deterministic headless workload using
the production audio engine, timeline compiler, mixer, native effects,
modulation graph, MIDI scheduler, recorder and project persistence paths. It
exits nonzero on invalid output, silence, excessive levels, queue loss,
recording overflow or persistence identity loss. Each tier emits JSON using
schema `neurogenic.integration-validation.v1`.

Task 064 adds a fifth integrated live-session scenario and an opt-in soak mode;
see [LIVE_PERFORMANCE_VALIDATION.md](LIVE_PERFORMANCE_VALIDATION.md).

## Workload tiers

| Tier | Tracks | Groups | Routes | Purpose |
|---|---:|---:|---:|---|
| light | 4 | 1 | 8 | baseline publication and instrument path |
| medium | 16 | 4 | 32 | normal workstation-sized mix |
| heavy | 32 | 8 | 64 | maximum track/group topology |
| stress | 32 | 8 | 128 | configured modulation-route limit |

Every tier runs at 48 kHz with repeating 256/127/65/191-frame callbacks. It
combines sample and synth tracks, group routing, aux sends, serial native master
effects, control-rate modulation, a prepared macro morph, six-channel I/O, MIDI
clock dispatch and internal recording. Configuration is saved and loaded before
playback, checking stable track identities and mixer/modulation counts.

Audio must remain finite and non-silent, with bounded peak level. Reports
include average and worst callback duration, deadline misses, MIDI drops,
recording overflow and resident memory. Project I/O and preparation are outside
callback timing.

```sh
cmake --preset dev
cmake --build --preset dev -j2
./build/dev/neurogenic_integration_validation
```

The executable is also a bounded native and sanitizer CTest. Existing focused
tests cover voice-local sample modulation, cross-owner and modulator graph
ordering, external clock, pattern/follow transitions, arrangement capture,
external-FX taps, immutable sample retirement, failed publication, legacy
migration, malformed files and appliance shutdown.

## Current workstation result

On the 2026-11-21 Ryzen 7 7800X3D development host, Debug average/worst times
were approximately 0.061/0.140 ms (light), 0.208/0.406 ms (medium),
1.273/2.163 ms (heavy), and 1.281/2.121 ms (stress). No callback deadline
misses, MIDI drops or recording overflows occurred; peak RSS was about 48.1
MiB. These short runs are regression data, not N150, Raspberry Pi 5,
physical-device or XRUN measurements.

## Hardware procedure and risks

Record the commit, run `neurogenic-diagnostics --headless --json`, run this
suite, then run the longer real-time procedure in
[HARDWARE_TESTING.md](HARDWARE_TESTING.md) against the actual device. Retain the
JSON reports for comparison. Physical tests remain necessary for backend XRUNs,
USB/DIN MIDI jitter, external-loop latency, duplex clocks, multichannel mapping
and touch behavior.

Callback allocation safety is structural and sanitizer-stressed, but this test
does not interpose every platform allocator. Its bounded duration does not
prove thermal stability or hours-long reclamation. UI Studio-facing versioned
snapshots expose stable identities, descriptors, capacities, effective values
and validation errors across subsystems, although no single aggregate discovery
document exists yet.

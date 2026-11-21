# Intel N150 bring-up

This procedure collects repeatable evidence on the Intel N150 reference host.
It does not contain N150 performance results: run it on the target and retain
the generated JSON. The same commands are portable to Raspberry Pi 5 unless a
host tool is explicitly noted.

## Install and inventory

Use either the Nix package or an ordinary dependency-enabled CMake build:

```sh
nix build
./result/bin/neurogenic-diagnostics --json > n150-inventory.json

cmake --preset dev
cmake --build --preset dev -j2
./build/dev/neurogenic-diagnostics --json > n150-inventory.json
```

No desktop is required for headless operation. The user must have access to
`/dev/snd` and the ALSA sequencer. Select devices by name or reported index;
NEUROGENIC never substitutes a different named device. Confirm that discovery
advertises 48000 Hz and the required input/output count.

The inventory's `realtime` object reports policy and priority,
`RLIMIT_RTPRIO`, `RLIMIT_MEMLOCK`, online CPUs, governor, and warnings. Normal
operation does not require root. Distribution-specific mechanisms such as a
realtime group, PAM limits, systemd user-unit limits, or a realtime portal may
grant an ordinary user bounded RT priority and memory locking. Configure those
outside NEUROGENIC, log out/in if required, and re-run the inventory. Do not run
the application as root merely to obtain scheduling priority.

CPU affinity is optional. Establish a baseline without affinity first. If
isolating a core, record the exact `taskset` or service configuration and keep
device IRQ behavior observable. Use a repeatable AC power profile and record
governor and temperature; NEUROGENIC does not tune the host automatically.

## Device and buffer profiles

Run each supported profile for output, then duplex. The requested period is a
hint; JSON records both requested and negotiated frames.

```sh
for frames in 64 128 256 512; do
  ./build/dev/neurogenic-diagnostics --realtime --stress 60 \
    --audio "OUTPUT DEVICE" --buffer "$frames" \
    --sample-voices 16 --synth-voices 16 \
    > "n150-output-${frames}.json"
done

for frames in 64 128 256 512; do
  ./build/dev/neurogenic-diagnostics --realtime --stress 60 \
    --audio "OUTPUT DEVICE" --input "INPUT DEVICE" \
    --input-channels 2 --output-channels 2 --buffer "$frames" \
    --sample-voices 16 --synth-voices 16 \
    > "n150-duplex-${frames}.json"
done
```

Repeat with six channels only when discovery reports support. A rejected rate,
channel count, selector, or period is a failed configuration, not a reason to
silently resample or reroute. `deadline_overruns` measures engine callback
duration. `backend_xruns_available=false` means this adapter cannot provide a
hardware XRUN counter; collect ALSA/PipeWire evidence separately and do not
replace it with zero.

## Integrated and stability workloads

```sh
./build/dev/neurogenic_integration_validation > n150-integrated.jsonl
./build/dev/neurogenic_benchmark > n150-benchmark.jsonl

# Optional; never part of ordinary CI.
./build/dev/neurogenic_integration_validation --soak-seconds 14400 \
  > n150-soak.jsonl
```

During the soak, cycle transport and scenes, record/overdub live loops, publish
samples, and test matching-device reconnect between runs. Monitor RSS,
temperature, frequency, callback overruns, external XRUN diagnostics, recording
overflow, MIDI drops, and audible discontinuities. A disconnect notification
is diagnostic; automatic physical-device reopen remains backend-dependent.

## Audio latency loopback

1. Reduce monitor/output gain and disconnect speakers when appropriate.
2. Patch one output to one input with a known-safe cable.
3. Record device, rate, requested/negotiated period, channels and direct-monitor
   state.
4. Explicitly emit a conservative impulse from a disposable test project and
   record its return on the physical-input tap.
5. Measure sample displacement at 48 kHz. Repeat at least 20 times and report
   median, minimum, maximum and failures.
6. Repeat recording alignment and live-loop tests with configured compensation.

The displacement is measured round-trip latency. Configured buffering is not a
latency measurement. Input and output latency cannot be separated without an
independent reference; NEUROGENIC does not invent either component.

## MIDI loopback

Connect MIDI out to MIDI in and record timestamped note, clock and transport
round trips while audio stress is active. Test velocity 1, 64 and 127, 24-PPQN
clock, Start/Stop/Continue/SPP, reconnect and panic. Report median and tail
latency, clock interval jitter, dropped messages and logical port identity.
Fixed-velocity arcade input remains an input policy; external MIDI velocity is
preserved.

## Result checklist

Retain commit/build type, CPU/kernel/distribution, governor, scheduling limits,
interface/firmware, backend, requested/negotiated rate and period, channels,
duration, callback average/peak/overruns, externally observed XRUNs, RSS/CPU/
thermal data, MIDI results, loopback measurements, warnings, observations, and
pass/fail criteria. Never label a simulated or Null result as physical evidence.

## Troubleshooting

- Non-48-kHz negotiation: select/configure a 48 kHz hardware profile.
- Period differs from request: record and test the negotiated value.
- No RT permission: configure user RT limits; do not run as root.
- Low memlock: increase the user limit or record the page-fault risk.
- Device disappears: stop, verify stable identity, and reopen only that device.
- Deadline overruns with no XRUN data: retain both facts independently and use
  backend tooling or listening/recording evidence for physical XRUNs.

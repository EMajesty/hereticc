# MIDI routing, clock, and external sequencing

NEUROGENIC's MIDI 1.0 subsystem keeps device ownership outside the musical
engine. `MidiInput` and `MidiOutput` are libremidi adapters. Logical ports
(`1..8`) remain stable in timelines and projects; a device selector binds a
logical port to a host port. Missing devices remain missing and are never
silently replaced by another port.

The input callback copies messages into the bounded controller queue. The
control dispatcher preserves controller mappings and passes raw messages into a
second bounded queue owned by `AudioEngine`. At the next audio block the engine
evaluates validated thru routes, received transport, and clock state. Outbound
messages enter a 1024-message SPSC queue. A non-real-time dispatch thread alone
calls libremidi. Queue exhaustion drops the newest message and increments the
observable counter; Stop and Panic enqueue CC 123 (All Notes Off) and CC 120
(All Sound Off) for every channel.

## Clock

Internal clock uses the authoritative transport tempo and an integer rational
phase accumulator to emit 24 PPQN without cumulative fractional-BPM drift.
Start resets phase; Stop halts it; Continue preserves phase. Tempo changes
rescale the fractional phase rather than restarting it. Physical transmission
occurs after the audio callback, so frame timestamps express intended engine
time, not a claim of sample-accurate cable timing.

External reception recognizes Clock, Start, Continue, and Stop. A bounded
five-pulse interval filter estimates tempo, rejects isolated implausible
intervals, and reports dropout after 500 ms. Selecting external clock makes its
transport messages authoritative and disables generated clock. Current external
sync is transport-message synchronization plus tempo measurement: continuous
phase/tempo slaving, Song Position Pointer seeking, and backend timestamp
latency compensation remain future work.

## Tracks and routes

Set a track's instrument to `midi.external.v1`, then set its logical output
port and zero-based channel. Note on/off, CC, program change, and 14-bit pitch
bend use the ordinary compiled timeline, including probability and ratchets.
Routing rules explicitly name input/output logical ports, optional input/output
channels, and enabled message classes. There is no implicit broadcast. Direct
same-port/channel feedback configurations are rejected.

Native launch examples:

```sh
./build/dev/neurogenic --list-devices
./build/dev/neurogenic --midi "Keyboard" --midi-out "Synth" sample.wav
./build/dev/neurogenic --midi 0 --midi 1 --midi-out 0 sample.wav
```

Multiple `--midi` and `--midi-out` options bind logical ports in command-line
order. Projects persist logical selectors, routes, track assignments, clock
source/output settings, and latency-offset metadata. Automatic reconnect and
hotplug recovery are not implemented yet.

## Hardware limits

Automated tests use simulated packets and deterministic timestamps. They verify
scheduling, routing, clock drift, jitter filtering, overflow, panic, and
persistence, but do not measure physical DIN/USB MIDI jitter, backend latency,
hotplug behavior, or device interoperability.

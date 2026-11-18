# Implementation status

Implemented and host-tested: PCM16 WAV loading, mono/stereo sample playback,
16-step integer scheduling, frame-offset events, next-bar pattern replacement,
simulated MIDI mapping, script success/error paths, an allocation-free gain
effect, silent operation, and deterministic offline WAV rendering.

Stubbed: native miniaudio callback/device startup, libremidi device discovery,
the actual Lua/sol2 bindings, and exact upstream Airwindows DSP import.

Untested: physical audio/MIDI hardware, ARM64 builds, latency under load, device
hotplug, and all controller-specific behavior. No hardware behavior is claimed.

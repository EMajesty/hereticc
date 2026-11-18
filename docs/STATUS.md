# Implementation status

Implemented and host-tested: PCM16 WAV loading, mono/stereo sample playback,
miniaudio callback output, device enumeration/selection, libremidi input
enumeration/selection, MIDI note/action dispatch, genuine Lua 5.4 execution,
validated versioned pattern APIs, sample-accurate frame offsets, rational
fractional-tempo scheduling, next-bar replacement, bounded real-time queues,
overflow reporting, variable callback sizes, and offline WAV rendering.

The local gain stage remains original NEUROGENIC code with an Airwindows-facing
identifier. It is not Airwindows DSP; exact upstream integration remains a
separate reviewed task.

Verified on this host: dependency-enabled build, automated tests, miniaudio null
device enumeration, continuous callback playback to the null device, clean
SIGINT shutdown, and graceful absence of MIDI. This environment lacked ALSA
development headers and MIDI ports, so physical audio/MIDI, ALSA libremidi
backend operation, latency under load, hotplug, ARM64, and controller-specific
behavior remain untested.

# Real-time stretch engine

NEUROGENIC uses one bounded overlap/add stretcher for sample instruments,
regions and arrangement audio clips. Stretch is opt-in; ordinary playback does
not execute this path. Processing owns no files or PCM and retains the immutable
sample version held by its `SampleVoice`.

## Algorithm and latency

The processor is a compact WSOLA-style grain overlap/add implementation without
correlation search. Economy, balanced and smooth quality use 256, 512 and
1024-frame analysis/synthesis windows with a 50% hop. Reported algorithmic
latency is therefore 128, 256 and 512 frames. Economy uses a triangular window,
balanced a smoothed triangle and smooth a squared-sine window. Two neighboring
grains are normalized during overlap. Mono is read into both output channels;
stereo channels share one timing state.

Time and pitch ratios are bounded to 0.25–4.0. Grain-anchor advance controls
duration while within-grain read advance controls pitch, so pitch does not alter
musical duration. State is deterministic across callback partitions and has
constant size. There is no transient correlation search: percussion is the
primary use case, while sustained, vocal and extreme-ratio material can exhibit
phasing, flamming or grain coloration.

## Loops, seek and transport

Loop boundaries are source-frame coordinates. Analysis reads wrap at those
boundaries while the synthesis clock continues, avoiding a reset and cumulative
phase drift on every repetition. The optional equal-power loop crossfade is
bounded to half the loop length. Very short legal loops can color strongly when
shorter than the selected window.

A seek initializes output time and deterministically derives source phase. Grain
history is reconstructed from immutable PCM with bounded work; no asynchronous
warm-up or callback pre-roll is required. Arrangement song-loop reactivation
uses this mapping. The compiled scheduler stops and reactivates clips at a song
boundary instead of carrying state across an unrelated transport discontinuity.

Source BPM or beat length resolves against the authoritative internal tempo or
the external MIDI follower's filtered tempo. Ratio changes first rebase the
source-time origin, then use a 20 ms per-sample ramp. They therefore neither
retroactively jump the source nor accumulate timing drift. Transport seek stops
old voices and reconstructs new clip phase under the existing ownership policy.

Forward and reverse stretching use the same path. Reverse reads and reverse
loop wrapping are explicit and never silently fall back to playback-rate timing.
Region envelopes, choke/release, voice-local pitch modulation, mixer routing and
recording taps remain in the shared sample-voice implementation.

## Real-time ownership and limits

Configuration and sample decoding are prepared off the callback. Runtime state
contains numeric phase, origin and window fields preallocated with the bounded
voice pool. Loop wrap, ratio rebasing and seek allocate nothing, take no locks,
log nothing and perform no filesystem work.

This is not an offline-quality vocal stretcher. It has no spectral phase model,
transient classification or formant preservation. Workstation benchmarks are
not Raspberry Pi 5 or Intel N150 claims; use `HARDWARE_TESTING.md` on those
machines.

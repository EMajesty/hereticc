# Audio clips

Audio clips are stable 64-bit arrangement objects that reference an immutable
sample slot rather than copying PCM. A clip owns a parent timeline track,
asset/region reference, musical start and duration, source-frame range, gain,
pan, direction, loop and stretch settings. Runtime sample-publication numbers
are not persisted; restored clips bind to the audio-active sample in their
stored slot, while the asset reference remains available for diagnostics.

## Scheduling and transport

Control-side compilation emits at most one start and one stop edge per clip per
song iteration. Source looping remains inside the existing bounded sample
voice, so a long loop does not expand into thousands of trigger events. Starts,
stops and adjacent transitions use integer musical ticks and the existing
128-bit tick-to-frame conversion. Stop edges sort before starts at one frame.

Seeking or publishing a plan in the middle of a clip synthesizes a start at the
first callback frame and restores native playback to the elapsed clip-relative
phase. Song-loop iterations are calculated arithmetically. Stop and restart use
the existing transport voice cleanup.

## Playback and routing

A clip creates an ordinary timeline-owned sample voice. It reuses immutable
sample-version lifetime, interpolation, reverse playback, voice modulation and
the realtime stretcher. The voice enters its parent mixer track, so track FX,
gain/pan, aux sends, groups, master routing and recording taps need no parallel
clip mixer.

Native loops use region-style source boundaries without PCM duplication. Clip
gain and pan are voice bases; modulation remains additive. Source replacement
affects new activations only, while active voices retain the old version.

Tempo sync reuses the realtime WSOLA-style stretcher and requires positive
source BPM. Pitch remains independent. Stretched looping is rejected in version
1 because the current stretcher cannot safely wrap its analysis state; native
looping and non-looping stretch are supported. Reverse stretch retains the
existing native-rate fallback.

## Editing and overlap

Create, update, duplicate and delete are timeline transactions and share
revision conflicts and Undo/Redo. Adjacent clips are allowed. Enabled audio
clips on one track may not overlap in this initial serial-lane model. Pattern
notes and audio clips may coexist and mix on a track. General clip crossfades
are deferred; source region fades and native loop crossfades remain available.

## Persistence and discovery

Timeline format version 8 stores identity, ownership, source reference, frame
and musical ranges, gain/pan, direction, looping, stretch and enabled state.
Versions 1–7 load with no audio clips and unchanged sound. Missing assets remain
visible instead of being silently substituted.

Capability `audio-clips` version 1 exposes clip objects, collection state,
validation diagnostics and typed edit actions. It contains no PCM, UI geometry
or controller assumptions. Current limits are 1,024 clips per arrangement, the
global sample-voice budget, single-lane same-track overlap, and existing
stretcher quality and latency.

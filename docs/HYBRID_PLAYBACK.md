# Hybrid arrangement and session playback

Hybrid playback uses one authoritative transport, timeline compiler and audio
scheduler. `PerformanceLauncher` owns only control-side launch state; every
transition publishes a complete prepared timeline plan.

## Per-track ownership

Each track reports `arrangement`, `transitioning_to_session`, `session`, or
`transitioning_to_arrangement`. Ownership uses stable `TrackId`. A slot or
follow affects its destination track. A partial scene affects exactly its
listed tracks and all members share one prepared frame. Omitted tracks and
their voices continue.

At takeover, timeline-owned sample, synth and external-MIDI notes on affected
tracks use the existing release policy. Other tracks are excluded from cleanup.
A rejected preparation/publication is never acknowledged, so ownership remains
unchanged. Reserved live-loop handoff voices remain protected.

## Publication and return

The control thread copies the song arrangement, suppresses arrangement pattern
and audio material only for session-owned tracks, inserts selected session
sources, validates the result, then queues it. The callback receives a fixed
affected-track array and does no allocation or graph construction.

`performance.return-arrangement.v1` accepts an optional track ID (absent means
all session tracks) and `immediate`, `quarter`, or `bar` quantization. The
replacement plan restores original arrangement material. Scheduler seek at the
absolute activation frame reconstructs pattern position and audio clip
loop/stretch phase without restarting the song. Pending follows for returned
tracks are canceled.

Start/Continue, seek, tempo changes, external MIDI clock and song loops retain
the authoritative transport frame. Stop retains existing note cleanup.
Ownership and queues are transient; only the existing playback-mode preference
is persisted. Activation acknowledgements provide capture with actual takeover,
follow and return frames.

## Limits

- One publication can change 32 tracks, matching mixer capacity.
- Session source phase is song-cycle based unless existing legato seek applies;
  return to arrangement is phase-correct.
- Source crossfades and arbitrary same-track arrangement/session summing are
  not implemented.
- Physical backend latency and N150/Pi 5 timing remain unmeasured.

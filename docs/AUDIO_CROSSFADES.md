# Audio clip overlaps, fades and crossfades

Timeline metadata version 9 permits enabled audio clips to overlap on one
track. Clips retain stable 64-bit identities and the compiler orders equal-time
activations by track and clip ID. Each clip owns one ordinary sample voice, so
audio continues through the existing track, group, aux, master and recording
paths.

Clip fades are musical ticks relative to the clip. They remain aligned when a
clip is stretched, reversed, looped, moved, resized or resumed after a seek. A
zero duration disables that fade. Linear and equal-power curves are available.
Existing sample-region fades remain source-relative and multiply with the clip
envelope.

An explicit crossfade has a stable ID, outgoing and incoming clip IDs, duration
and curve. Its interval begins at the incoming clip start. The clips must be on
the same track and overlap; duration cannot exceed the overlap or incoming clip.
A clip may have at most one incoming and one outgoing enabled crossfade. Plain
overlaps sum normally. Linear crossfades use complementary gains; equal-power
fades use cosine/sine coefficients.

The compiler converts musical envelopes to output-frame ramps. The callback
does no allocation, topology construction or per-sample event expansion.
Seeking restores envelope phase from clip-relative output time. Song-loop
reactivation starts a fresh deterministic clip voice.

The compiled plan reports peak simultaneous clip voices and publication is
rejected when it exceeds the sample-voice pool. Clip triggers never steal an
existing sample voice; runtime contention increments the bounded dropped-event
diagnostic. Provision headroom beyond the static clip peak for pads/patterns.

Deleting a clip transactionally deletes referencing crossfades. Duplication
copies fades but not external crossfade relationships. Version 8 metadata loads
with clip fades disabled and no crossfades.

Discovery capability `audio-clips` version 2 exposes fade state and crossfade
objects. Current limitations: there is no automatic crossfade creation and
already-active voices retain their originally compiled envelope.

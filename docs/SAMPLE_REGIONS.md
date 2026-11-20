# Nondestructive sample regions

Sample regions are stable, project-persistent views into immutable sample PCM.
A region owns an ID, sample slot/version reference, source-frame start and
exclusive end, name, direction, gain, fades, and an optional loop. Editing a
region never rewrites or copies PCM.

## Coordinates and playback

All boundaries are source-sample frames. `start_frame` is inclusive and
`end_frame` is exclusive; a one-frame region is valid. Loop boundaries use the
same domain and must be contained by the region. Forward and reverse voices
start at the corresponding edge and are bounds checked before every read.

Fade lengths are source frames. Linear and equal-power curves are available.
If fades overlap, the lower instantaneous coefficient wins, keeping gain
bounded. Loop crossfades blend the end and start of the loop over a bounded
number of frames. Gate note-off leaves the loop and enters normal release;
one-shot and toggle loops continue until their usual stop action.

Realtime stretching supports region boundaries and fades, with pitch remaining
independent of duration. Looping is intentionally disabled in the realtime
stretcher path in this version. Non-stretched forward and reverse playback
support loops and crossfades.

## Ownership and editing

`SliceService` remains the shared control-side service. It validates revisions
and sample bounds, creates, duplicates, renames and deletes regions, converts
slices, edits fades/gain/loops/direction, reports usage and publishes pad
targets. An assigned region must be unassigned before deletion.

Publication copies the complete target into a bounded audio command. Existing
voices retain that copy and their immutable sample version; new triggers use
the update. No region lookup, allocation, locking, filesystem access or
destruction occurs in the callback.

## Persistence and compatibility

`NEUROGENIC_SLICES 4` adds `region` and `region-pad` records. Versions 1–3
continue to load with an empty region collection and unchanged playback.
Sample-instrument and track-sound presets reuse this metadata. Missing or
replaced samples invalidate control-side regions for that slot while active
voices retain the old sample version.

Application discovery exposes capability `sample-regions` version 1, semantic
editing actions and `sample.regions`/`sample.region-assignments` bindings. This
is presentation-neutral groundwork; no waveform-editor layout is defined.

## Limits

Region metadata does not duplicate PCM. Work is bounded by the voice pool.
Destructive rendering, stretch-loop integration, automatic region detection,
and a single Undo/Redo entry for a multi-edit region transaction are deferred.

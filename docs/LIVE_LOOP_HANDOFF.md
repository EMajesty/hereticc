# Gapless live-loop handoff

The first pass uses the normal recorder, sample-version pool, sample voices and
arrangement scheduler. It does not introduce a second playback engine.

## Prepared ownership

Arming reserves a fixed-size `Sample` capture buffer, a queued sample-version
slot and one voice token. `RecordingService` writes directly into the buffer.
At the exact stop frame the callback seals it, makes the queued version active
and starts the reserved looping voice. Ordinary triggers cannot consume the
voice reservation.

The control thread then publishes the same immutable allocation through
`SampleManager` and creates the ordinary arrangement clip. The scheduler
replaces the handoff voice by stable clip ID at the current phase. Old sample
versions remain alive while referenced and are reclaimed only off the audio
thread.

Arming fails before capture if buffer allocation, sample-version capacity or a
voice reservation is unavailable. No allocation, copying, locks, logging or
filesystem work occurs at the boundary.

## Timing

Fixed-length first passes start playback at the requested stop frame. A callback
containing that frame is split into bounded sub-blocks: the first completes
capture and the second begins at source frame zero. Duplex and interleaved I/O
split input and output at the same frame. The boundary sample is neither omitted
nor duplicated.

Musical length is resolved to frames while arming. Input-latency compensation
changes capture placement, not the prepared loop length. Physical converter and
backend latency remain hardware-dependent and require measured compensation.

## Failure and limits

Capture overflow, cancellation and device failure invalidate successful
handoff. Publication or timeline failure is reported and never presented as a
completed arrangement activation. One capture handoff is supported at a time.
Overdub and replace retain their immutable control-side mixing/publication path.

Session state exposes buffer and voice reservation, readiness, first-playback
frame, completion and failure diagnostics through discovery capability
`live-looping` version 2.

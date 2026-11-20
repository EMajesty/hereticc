# Patterns, clips, and arrangements

NEUROGENIC stores patterns and arrangements inside the authoritative
revisioned `Timeline`. They are edited through `TimelineTransaction`, use the
same bounded history as step edits, and compile into the existing immutable
`PlaybackPlan`; there is no second sequencer or callback-side graph.

## Model and identity

- `PatternId` and `ClipId` are stable 64-bit identifiers, never collection
  indices or UI positions.
- A pattern is reusable multi-track musical content. Each `PatternTrack` has
  its own loop length, so polyrhythms remain possible. Pattern metadata carries
  a name, nominal length, and time signature.
- A clip references a pattern and supplies start, duration, loop enable,
  transpose, velocity scaling, and enable state. Clips do not duplicate their
  source event vectors.
- A song owns a start/end range and optional loop region. The initial engine
  loops that compiled range through the existing scheduler.

Collections are discoverable and have no fixed bank size. Validation budgets
are 256 patterns, 4,096 clips, and 4,096 pattern-track records, in addition to
the existing 65,536-event timeline budget.

## Playback and launch

`legacy` plays original per-track patterns exactly as older releases did.
`live_pattern` plays one selected reusable pattern. `song` expands enabled
clips into the selected song or loop range before publication.

Pattern launches can be immediate or queued to quarter-note, bar, two-bar, or
current-pattern boundaries. The application observes the authoritative
transport tick and commits the queued transition on the control thread at the
first observed boundary. It becomes audible on the next audio callback. This
is bounded by one control-state refresh plus one callback; the callback never
compiles or allocates. Transport phase and already-active voices are retained.

Pattern chains and their position are persisted and validated. Automatic chain
advancement is deferred.

## Compilation and precedence

Enabled clip events repeat up to clip duration and are then transformed.
Transpose applies to notes and velocity scaling is clamped to 0..1.
Probability, conditions, ratchets, parameter destinations, MIDI routing,
instrument identity, and deterministic seeds are retained. Placement IDs are
deterministic functions of clip ID, source event ID, and repetition.

At an equal tick the established scheduler order remains note-off,
parameter/MIDI control, note-on, then remaining notes, with stable track/event
tie-breakers. Parameter locks therefore precede notes. Base values remain
separate from automation and modulation.

The initial serial-lane arrangement rejects overlapping enabled clips that
address the same track. This avoids ambiguous note and parameter precedence.
Clips on disjoint tracks may overlap.

## Editing, persistence, and migration

Pattern and clip edits commit complete validated snapshots and participate in
undo/redo, revision conflict detection, publication failure handling, and safe
playback-plan reclamation. Timeline format version 5 stores definitions,
per-track content, clips, launch settings, chains, and song/loop ranges.
Versions 1–4 load with an empty arrangement in `legacy` mode, preserving sound
without duplicating events.

## Limitations

- Song playback loops the compiled song/loop range; stop-at-song-end is not yet
  available.
- Automatic chain advancement and follow actions are deferred.
- Seeking uses the bounded `arrangement.seek.v1` transport command and shares
  the existing transport frame and MIDI SPP timing domain.
- Audio-file clips, warping, and same-track clip overlap are unsupported.
- Queued activation is control-refresh bounded, not a sample-exact plan swap.

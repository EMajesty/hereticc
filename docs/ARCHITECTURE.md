# Architecture

The control thread owns files, projects, scripts, mappings, and editable state.
It compiles edits into bounded pattern snapshots. The audio thread consumes
only fixed-size events and sample memory prepared in advance.

```text
CLI/UI -> command API -> project / Lua / MIDI mapping
                         | boundary-safe Pattern snapshot
                         v
musical scheduler -> frame-offset events -> sample voice -> DSP -> backend
                                                    \-> offline WAV
```

- `core`: stable command identifiers and application-domain boundary.
- `sequencer`: integer frame clock, deterministic ordering, next-bar swaps.
- `audio`: prepared sample voices, offline renderer, future backend interface.
- `midi`: timestamped messages and stable action mappings; simulated input is
  always available.
- `scripting`: versioned `neurogenic.v1` API boundary. The fallback parser lets
  host tests run without Lua; the native sol2 binding remains to be completed.
- `dsp`: allocation-free effect interface and first gain stage.
- `project`: deliberately tiny versioned format; not a promised final schema.

No callback operation performs I/O, locks, allocation, logging, or Lua work.
The current single trigger mailbox is bounded but can coalesce triggers; a
fixed-capacity SPSC command queue is the next real-time hardening step.

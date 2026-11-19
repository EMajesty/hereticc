# Project persistence

NEUROGENIC project format version 1 is a directory, independent of SDL, Lua,
and controller layouts:

```text
My Set/
  manifest.ngp
  generations/
    generation-.../
      timeline.ngt
      samples.ngs
      parameters.ngp
```

`manifest.ngp` names exactly one committed generation. A save validates a
control-side snapshot, writes a new temporary generation, renames that
generation into place, and replaces the manifest last. A failed write leaves
the previously referenced generation readable. Old and unreferenced
generations are retained; version 1 does not claim durability across every
filesystem/power-loss combination and does not automatically prune history.
Metadata is human-inspectable and bounded to 16 MiB per file. Versions newer
than the application understands are rejected.

The timeline file stores transport, generation seed, stable track/event IDs,
routing, notes, automation and parameter locks, probability, conditions, and
ratchets. The sample metadata stores slot references, stable slices, pad
assignments, expressive playback/envelope settings, and time-stretch settings.
The parameter file stores registered native synth/effect parameter values.
Transient voices, playback position, monitoring counters, GUI caches, focus,
and controller state are deliberately excluded.

## Assets and relocation

Sample PCM is not embedded or copied. External references are the default.
Relative paths are resolved from the project directory, so a project and its
relative `assets/` directory can move together. Absolute paths remain absolute.
Relative paths that escape the project with `..` are rejected. Automatic asset
copying is deferred; users may place a sample under the project themselves and
store a relative reference.

Missing samples do not make a project unreadable. Their intended paths, slices,
pad assignments, and settings remain in metadata and are exposed in
`ApplicationState::missing_assets`. `project.relink-sample.v1` accepts the old
and replacement paths, prepares the replacement off the audio thread, waits for
callback acknowledgement, and restores the associated slice state. Saving an
unresolved project preserves that information.

## Lifecycle and activation

The shared C++ application service provides `project.new.v1`, `open`, `save`,
`save-as`, `close`, `relink-sample`, and `query` actions. State snapshots expose
the name/path, dirty flag, loading/saving flags, last error, and missing assets.
Timeline, slice, sample, pad, and registered parameter edits mark the project
dirty; transport position, monitoring, and GUI redraws do not.

Opening is staged on the control thread: metadata is parsed and validated,
available samples are decoded/resampled, audio-callback publication is
acknowledged, then slices, the complete timeline, routing, and parameters are
restored. Project switching stops transport and never starts playback
automatically. Missing slots remain silent while unaffected tracks are usable.
The audio callback performs no project I/O, parsing, allocation, locking, or
destruction.

Start explicitly with:

```bash
./build/dev/neurogenic --project /path/to/My\ Set --gui
```

`--project` cannot be combined with standalone sample or timeline inputs. In
the Display view the current project, dirty marker, path, and missing-asset
count are visible; Shift+Enter invokes Save. Open, Save As, and relinking are
available through semantic actions, while a complete embedded filesystem
browser is still deferred.

Legacy `.ngt` timelines and version 1-3 slice sidecars remain accepted by their
existing loaders. They can be assembled into a project through the shared C++
services; there is not yet a one-click GUI migration wizard.

An audio-active recording without a filename is retained during Save as a
float32 `recordings/slot-ID.wav` asset and then referenced relatively. Failure
to write that asset aborts the new generation instead of silently dropping the
capture. Missing recorded assets use the same reporting and relinking path as
other samples.

# Project persistence

Mixer metadata version 3 persists native FX instances and serial track, aux and
master chains. Older versions load empty chains. Unsupported types are reported,
not substituted.

`midi.ngm` stores the selected clock source, logical input/output selectors,
clock output ports, external forwarding preference, route filters/remapping,
and output-latency metadata. Task 031 version-1 MIDI metadata remains readable;
version 2 adds forwarding without persisting PLL phase or timestamps.
Logical selectors are retained when devices are missing; loading never binds an
unrelated physical port automatically. Track MIDI destinations remain in the
timeline data.

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
      mixer.ngm
      scenes.ngr
      scene-<stable-id>.ngm
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
The optional mixer file stores track/input/master channels, two aux buses and
physical assignments. Older projects without it receive unity channels derived
from timeline IDs. Unavailable outputs remain assigned and silent rather than
being redirected.
Mixer metadata v2 adds external-loop mappings and latency while retaining v1
loading. The optional scene index names up to 32 stable routing snapshots, each
stored in the same validated mixer format. Older generations load with no
scenes.
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

Version-1 projects may include `modulation.ngm`. Its current metadata version 3
stores source definitions, deterministic seeds, explicit scopes, and stable
routes, including numeric sample/mixer/FX identities and follower settings.
Versions 1 and 2 remain readable. Absence means an empty matrix for backward compatibility. Unsafe or
unavailable destinations and unsupported voice destinations are rejected before
publication.

Timeline version 3 stores track-to-instance assignments and modulation metadata
version 3 stores destination identities and follower coefficients. Older projects and timelines migrate
their former singleton synth routing to shared instance 1. Instance parameter
overrides use stable `instance.N.synth.*` entries in parameter metadata. Active
voices, phases, envelopes, and random runtime state are never persisted.

Legacy `.ngt` timelines and version 1-3 slice sidecars remain accepted by their
existing loaders. They can be assembled into a project through the shared C++
services; there is not yet a one-click GUI migration wizard.

An audio-active recording without a filename is retained during Save as a
float32 `recordings/slot-ID.wav` asset and then referenced relatively. Failure
to write that asset aborts the new generation instead of silently dropping the
capture. Missing recorded assets use the same reporting and relinking path as
other samples.

## Appliance preferences and unsaved work

Appliance preferences are not project data. Versioned `appliance.conf` stores
reopen-last, last project path, preferred audio/MIDI device names,
channel/buffer preferences, fullscreen, and UI scale. `--project` overrides the
preference. A missing remembered project is a recoverable startup issue and
opens an empty session.

`project.new.v1`, `project.open.v1`, and `project.close.v1` reject a dirty
project unless the client saves first or explicitly passes `discard=true`.
Shutdown applies the same Save/Discard/Cancel choice. Save uses the existing
generation-based writer; no stronger power-loss guarantee is implied.

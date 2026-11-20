# Reusable sound presets

NEUROGENIC presets are independent of projects. `PresetLibrary` is the typed,
control-side API for saving, loading, enumerating, validating, duplicating,
deleting, exporting and importing them. Preset work never runs in the audio
callback.

## Format

Each preset is a directory named by a stable library ID:

```text
library/<preset-id>/
  manifest.ngp
  generations/generation-*/
    parameters.ngp
    samples.ngs          # when present
    effects.ngp          # when present
    modulation.ngm
```

The human-readable manifest begins with `NEUROGENIC_PRESET 1` and identifies
the preset type (`synth`, `sample`, `fx-chain`, or `track-sound`), descriptive
metadata, dependencies, active generation and track gain/pan. Subsystem files
retain their own versions. Unknown top-level versions are rejected.

A save validates first, writes a temporary generation, renames it into place,
and replaces the small manifest last. Failure before manifest replacement
leaves the previous generation active. Old generations are retained. This is
generation-atomic on one filesystem, not a claim of power-loss durability
beyond the filesystem's guarantees.

## Contents and ownership

- Synth patches contain base synth/voice parameters and compatible local
  modulation definitions. Active voices and oscillator phases are excluded.
- Sample instruments contain sample references, slices, pad assignments,
  playback/stretch/envelope settings, and compatible local modulation.
- FX-chain presets contain ordered processors, parameters, wet/dry, bypass and
  local modulation.
- Track sounds combine those compatible parts with track gain/pan. Project
  routing, group/master state, physical I/O and global scenes are excluded.

Preset IDs identify library entries. Effect, modulator and route IDs inside a
preset are template identities. `prepare_preset_import` allocates fresh runtime
IDs, rewrites internal references, validates the full prepared configurations,
and returns them without changing the active engine. External references are
rejected unless an importing client explicitly maps them. Unrelated instances
are never overwritten.

## Assets and portability

PCM is never embedded in metadata. Sample references may be absolute explicit
library dependencies or relative to the preset directory. Relative paths are
normalized and paths beginning with `..` are rejected. Missing assets are
reported by `validate_preset`; `require_assets=false` keeps metadata inspectable
for relinking.

`export_package` copies the versioned preset directory. `import_package`
validates it, assigns a caller-provided library ID and saves a new generation.
For a self-contained package, clients copy assets deliberately and store
relative paths; the library never silently copies a large sample collection.

## Validation and limits

Validation rejects malformed IDs, non-finite parameters, unsupported native
processors, invalid modulation graphs, unsafe paths and metadata files larger
than 16 MiB. Import checks the real-time budgets: 32 FX instances, four effects
per chain, 64 modulators and 128 routes. Capacity failure or a missing internal
reference leaves the supplied active configurations unchanged.

Metadata, dependencies, compatible targets and diagnostics are
presentation-neutral and suitable for discovery/UI clients. A visual preset
browser is outside this subsystem.

## Current limitations

The library does not automatically bundle PCM, garbage-collect retained preset
generations, translate third-party processor types, or provide cloud sharing.
Atomic multi-subsystem engine publication remains the responsibility of the
application transaction applying the prepared result.

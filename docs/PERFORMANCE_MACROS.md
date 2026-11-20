# Performance macros

Performance macros are persistent, normalized control sources compiled into the
existing modulation graph. A macro has a stable 64-bit identity, a distinct
stable modulator-instance identity, a project/track/group/master owner, a base
and default value, descriptive metadata, and bounded parameter assignments.
The default project budget is 32 macros, 128 assignments and 32 scenes.

## Value composition and mappings

Each assignment names an existing numeric parameter destination and maps the
macro's 0–1 value into a native-unit minimum/maximum range. Linear,
exponential (`x²`), logarithmic (`sqrt(x)`), smooth S-curve, stepped and
inverted ranges are supported. Assignment output is expressed as a delta from
the macro default, so the existing rule remains:

`effective = clamp(automation-or-base + sum(modulation and macro deltas))`

Multiple macros targeting one destination sum in stable route order. They do
not overwrite base values, parameter locks or automation. Topology, files,
processor types and resource-owning parameters are not valid destinations.
Track-local assignments require an unambiguous instance-aware destination.

Macros are also discoverable `native.performance-macro.v1` modulator instances.
Consequently they can feed DSP or modulator parameters and can receive
descriptor-approved modulator-to-modulator input through the same acyclic
graph. Timeline automation addresses a macro as
`macro.<source-instance-id>.value`; only semantic macro values are recorded,
not every downstream assignment.

## Scenes and morphs

A performance scene has a stable ID, name, captured macro values, and a default
morph preference. Recall interpolates normalized macro values on the control
side and sends bounded value commands to the audio engine. Durations may be
seconds or musical quarter-note units. A new recall replaces the active morph;
stop leaves values at their current positions. Deleted or missing macro targets
are ignored while remaining scene targets continue.

Scene definitions are separate from existing routing scenes, so loading an old
routing scene does not silently change macro values. Morph runtime progress,
oscillator phases and command-queue state are transient and are not persisted.

## Discovery and persistence

`ApplicationState::performance_macros` is a versioned copied snapshot containing
identities, owners, names, current/default values, assignments, curves, scenes,
morph progress, capacities and validation diagnostics. This is presentation
neutral: UI Studio may render the same macro as a slider, knob, button, XY axis
or custom SVG widget.

Project format 5 adds `macros.ngm` to each committed generation. Older projects
load an empty macro collection without changing their sound. Invalid owners,
destinations, duplicate IDs and over-budget data are rejected before audio
publication.

Current limitations: scene capture stores macro values (the optional numeric
parameter list is reserved), assignment-specific smoothing metadata is
persisted but DSP destinations currently use their existing smoothing, morphs
are advanced at application-state synchronization rate, and Undo/Redo remains
timeline-oriented rather than a general project-object history.

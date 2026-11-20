# Modulation graph

NEUROGENIC compiles dynamic modulators and connections into a bounded directed
acyclic control-rate graph. Editable configuration stays on the control side;
the callback receives fixed arrays and a prepared evaluation order.

## Model and identity

Connections have stable IDs and reference a stable source instance and numeric
output port. A destination is either a normal numeric DSP parameter or a stable
modulator instance plus descriptor-approved parameter. Depth, polarity and
enabled state belong to the connection. Array positions, names and UI order are
never identities.

Modulator destinations include continuous rate, synchronized beat length,
ADSR attack/decay/sustain/release, and follower attack/release/gain/threshold.
Seeds, waveform choice, ownership, tap selection and resource sizes are not
modulatable.

## Ownership and visibility

Instances retain track, aux or master ownership. Cross-owner access requires a
persisted `shared` flag and an explicit destination owner. Validation rejects
unshared cross-owner edges. This supports track-to-track, track/bus, bus/track
and master/track routing without turning a local source into a global source.

Track deletion is rejected while the track owns modulators. Clients must
delete or reassign them first, making dependent connection handling explicit.
Group ownership remains reserved.

## Validation and evaluation

`validate_modulation_graph()` checks budgets, instances, output ports,
continuous parameter eligibility, visibility and direct/indirect cycles. The
diagnostic identifies the stable offending connection. A bounded Kahn sort
chooses the lowest stable instance ID among ready nodes, making ordering
independent of insertion order.

At each update (32 samples by default), incoming modulator-parameter values are
composed before the receiver advances. Normal destinations retain:

`effective = clamp(base-or-automation + sum(modulation contributions))`

Stored bases are never overwritten. Sources may fan out and destinations may
sum many sources. Disabled edges do not affect ordering. Limits remain 64
instances and 128 connections, with no callback allocation, locking, logging
or topology construction.

## Audio-derived modulation

Followers select stable master, track, aux or physical-input taps. Mixer blocks
are observed after mixing and affect the following block. This explicit
one-block latency breaks audio/control ordering cycles. Stereo taps use peak
absolute amplitude across both channels; observation uses fixed mixer buffers.

## Voice boundary, persistence and discovery

Existing synth voices retain independent preallocated envelope, velocity, LFO
and random state. The shared graph never promotes voice-local state. Dynamic
voice graph nodes remain deferred.

`modulation.ngm` version 5 persists destination kinds, destination owners,
sharing, follower taps and parameter bases. Version 4 loads unchanged with
implicit local visibility; versions 1-3 retain Task 039 migration.

Application snapshots expose configuration, values, descriptors, capacities,
compiled order and validation diagnostics. Semantic actions edit connections,
sharing and taps or query graph discovery data, allowing UI Studio to build a
patcher without engine-specific hardcoding.

Limitations: no feedback, recursive or audio-rate graph, dynamic per-voice
nodes, group buses or plugin destinations. Physical-input taps use configured
mixer input indices and require hardware verification.

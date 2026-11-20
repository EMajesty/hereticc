# Dynamic modulator instances

NEUROGENIC uses a bounded collection of stable modulator instances rather than
a user-visible fixed source bank. The legacy twelve-source bank remains a
compatibility adapter. New clients discover native types with
`modulator_type_registry()` and create instances through `modulator.*.v1`.

## Discovery and types

The registry exposes LFO, ADSR, sample-and-hold, smooth random, envelope
follower, XY/manual and MIDI CC. Descriptors provide stable versioned type IDs,
categories, parameters, ports, polarity, supported owners, trigger behavior,
update rate and estimated state size. Presentation metadata is separate from
compact DSP dispatch, so future UI Studio and remote clients need no hardcoded
algorithm knowledge.

## Identity, ownership and routing

Every instance has an independent positive 32-bit ID, type, optional name,
enabled flag, parameters and owner. Active owners are stable timeline track
IDs, auxiliary bus 0 or 1, and master (owner ID 0). Group/mixdown ownership is
reserved and rejected until those buses exist.

Routes reference `(source instance ID, output ID)` and a stable numeric
destination. Track and aux sources remain local to their owner; master sources
use validated global destinations. Cross-track and modulator-to-modulator edges
are deferred to Task 040.

Voice-definition ownership is represented for the existing synth architecture,
but dynamic definition routing is deferred. Active voices retain preallocated
per-voice envelope, velocity, LFO and random state; no persisted instance is
created for an active note.

## Runtime and capacity

Each instance owns independent phase, gate/stage and deterministic random
state. Runtime uses preparation-time array slots, never UI order or stable-ID
lookup in a per-sample loop. Elapsed-frame updates are callback-partition
independent. Values compose as:

`effective = clamp(base-or-automation + sum(modulation contributions))`

The global budget is 64 instances and 128 routes. Runtime state is about 32
bytes per instance plus fixed configuration and route records. Capacity failure
rejects the new configuration and preserves existing instances. Preparation,
validation, publication and reclamation happen off the audio callback.

## Persistence and migration

`modulation.ngm` version 4 stores instance IDs, types, owners, names, settings,
outputs and routes. Versions 1–3 migrate deterministically, preserving source
settings, scope, seed, depth and destination. Legacy note velocity remains on
the compatibility path because it is note data. Unsupported or over-capacity
migration fails rather than dropping a route. Phase, envelope stage, random
position and active voice state are not persisted.

Current limitations are the absence of cross-track graphs, group buses,
modulator input routing, dynamic persisted per-voice definitions and audio-rate
processing. XY currently exposes one scalar output per instance.

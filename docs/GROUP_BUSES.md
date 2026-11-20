# Group buses

NEUROGENIC provides eight bounded stereo group/mixdown buses. A group has a
stable 32-bit ID independent of its array slot, display name, or future UI
order. Tracks select exactly one primary destination: master, a physical pair,
or a group. Groups route to master, a physical pair, or another group. The
control side rejects missing destinations, self-routes, cycles, duplicate IDs,
invalid channel layouts, and capacity overflow before publication.

## Signal order

```text
instrument/sample -> track FX -> track gain/pan
                  -> selected group sum -> group FX -> group gain/pan
                  -> group aux sends -> parent group or master
                  -> master gain -> master FX -> physical output
```

Track aux sends retain their existing pre/post-fader point. Group sends can
also be pre-fader (after the group FX and pan, before group gain/mute/solo) or
post-fader. Routing is exclusive: assigning a group does not retain an implicit
parallel dry path to master. Child groups are processed before parents in a
stable topological order, with group ID as the deterministic tie-breaker.

Mute silences a group and its post-fader sends. Group solo admits the selected
group's source subtree and every ancestor needed to reach master; unrelated
groups and their directly assigned tracks are silent. Track solo remains
independent and is applied before group summing.

## Ownership and real-time publication

`MixerConfiguration` contains the stable group definitions and a prepared
topological order. Validation and preparation occur on the control thread. A
complete bounded snapshot crosses the existing SPSC publication queue and is
activated only at a callback boundary. The callback uses preallocated buffers
for group pre-effects, post-effects, and post-fader signals. It performs no
allocation, locking, filesystem access, logging, or topology construction.

The default capacity is eight groups, 32 tracks, two aux buses, and 1024 frames
per processing chunk. Group buffer storage is fixed at mixer construction.
Prepared group audio and numeric-modulation lanes reserve approximately 320
KiB per mixer instance, excluding small configuration/runtime objects.
Deletion is rejected while tracks, child groups, or modulator instances still
reference the group.

## Effects, modulation, recording, and discovery

Each group owns one bounded serial native FX chain. Effect instances retain
their stable IDs; the `effect.*.v1` actions address a group chain using
`chain="group"` and the stable group ID as `index`.

Groups are first-class `ModulatorOwnerKind::group` owners. The common graph can
route group-local or explicitly shared sources to group gain, pan, aux sends,
or group FX parameters. `FollowerTapKind::group` observes the post-fader group
signal with the modulation engine's documented block latency. Stored base and
automation values are unchanged by numeric modulation overlays.

Recording sources use the stable group ID and expose three distinct taps:

- `group-pre`: summed input before the group FX chain;
- `group-post-effects`: after the FX chain, before group gain;
- `group-post-fader`: after pan, gain, mute, and solo.

Application snapshots expose group definitions, routing, meters, capacity,
effect chains, modulator ownership, and validation errors without exposing
callback objects. Semantic `mixer.group-*.v1` and
`mixer.track-destination.v1` actions serve headless, UI Studio, remote, and
future controller clients.

## Persistence and compatibility

Mixer metadata version 4 stores group IDs/names, routing, mixer values, aux
sends, physical output assignment, and group FX chains. Modulator metadata
version 8 stores group owner, follower-tap and destination identities. Older
mixer versions load with zero
groups and therefore retain their original direct-to-master sound. Prepared
orders, meters, follower values, and active recording state are transient.

Current limitations are eight fixed-capacity groups, stereo groups only,
serial native chains, no feedback routing, and no group-to-external-device
clock compensation. External loops may source a direct-to-master/physical
group, but not a nested group or a track already assigned to a group; inserting
external processing inside a group graph is deferred. Group routing is not a
general modular audio graph.

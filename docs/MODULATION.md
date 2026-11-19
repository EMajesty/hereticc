# Modulation matrix

The modulation matrix is a bounded control-rate client of the existing numeric
DSP destinations. It does not replace stored parameters or timeline events.
For each destination the engine computes:

`effective = clamp(automation-or-base + sum(source × bipolar depth))`

The sum is performed in the destination's normalized range and converted back
to native units. A step lock or automation event replaces the base layer until
another event changes it; routes remain active. Snapshots expose stored base
values, effective values, route source values, and a generation counter.

## Sources, scope, and timing

The source bank contains four sine/triangle/saw/square/random LFOs, an ADSR
modulation envelope, sample-and-hold, smooth random, X/Y, note velocity, and
MIDI CC. Every source declares `global`, `track`, or `voice` scope. Global
sources have one state. Track sources create fixed-capacity independent state
for every routed track, including phase, envelope gate/stage, retrigger state,
and a random stream derived deterministically from `(seed, track ID)`.

Voice scope is represented in the public model and persistence format, but is
rejected during validation in this release. The synth's existing velocity
response remains per voice; fully routable polyphonic envelopes and velocity
modulation require a future voice-destination path.

The engine updates at a bounded interval (32 samples by default). Phase advances
from elapsed frames, so callback partitioning does not alter frequency. Free
rates use hertz; synchronized rates use beats per cycle and authoritative
transport tempo. Restart resets phase and automation. Retrigger-enabled global
sources restart on any routed note; track sources only restart for their track.
The global envelope is currently a monophonic gate; every track envelope has an
independent gate. Runtime phase is not persisted.

Random sources use a fixed integer generator with an explicit seed, never
process randomness. Reconfiguration produces repeatable output for identical
settings and track IDs. X/Y and MIDI CC enter through bounded commands.

## Routes and destinations

Routes have stable IDs, source, explicit scope/track ID, numeric destination,
bipolar depth, polarity, and enabled state. Sources may feed multiple
destinations and contributions to one destination sum before clamping. Initial
real-time destinations are subtractive-synth pitch, oscillator, filter,
envelope and gain controls, plus native-delay time, feedback, mix, and gain.

Sample pad settings, mixer configuration, external-FX routing, discrete values,
file paths, and topology are rejected because they require prepared snapshots
or control actions. Additional numeric DSP destinations can opt in through the
parameter registry once they have a bounded callback path.

## Ownership and clients

`ApplicationService` owns editable source/route configuration and validates
`modulation.*.v1` actions. It publishes complete fixed-size snapshots through a
bounded SPSC queue. The callback resolves no strings and performs no allocation,
locks, logging, or graph construction. Limits are 32 routes and 32 routed track
state banks. Atomic snapshots provide read-only UI/controller feedback.

The GUI `MOD` view selects routes with Up/Down, adjusts depth with Left/Right,
and toggles enable with Enter. The same actions support pointer clients, eight
encoders, a data wheel, XY, MIDI, and remote clients without Lua.

Projects write optional `modulation.ngm`. It preserves source settings, seeds,
route IDs, scopes, track IDs, depths, and destinations. Older projects load an
empty matrix. Forward versions and unsafe destinations are rejected.

Current limitations: no per-voice route execution, audio-rate modulation,
recursive graphs, modulation curves, per-track MIDI CC/XY lanes, or modulation
of prepared sample/mixer/routing state. Delay destinations remain block-applied.

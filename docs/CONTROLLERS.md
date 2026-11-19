# Application control and controller API

The control architecture is deliberately client-neutral:

```text
hardware / touchscreen / desktop / MIDI / Lua / remote
                         |
        C++ actions, parameters, commands and snapshots
                         |
     timeline / samples / transport / audio engine
```

All types described here run on application or control threads. They never
expose editable engine objects to frontends and are not called by the audio
callback. Lua remains an optional adapter over C++ services; hardware and UI
clients do not require a Lua runtime.

## Actions and commands

`ActionRegistry` publishes `ActionDescriptor` records with a stable versioned
string ID, display name, description, category, typed argument schema,
availability and an explicit unavailable reason. `ApplicationValue` supports
booleans, 64-bit integers, doubles, text and normalized X/Y values.

Implemented actions cover transport, track selection/navigation/mute and note
targeting, pattern/step selection, toggling/resolution/pages, sample slot selection/load/replace/unload, pad modes,
focus/confirm/cancel, text input, modifiers and X/Y input. Undo, redo, grouped
timeline editing, and selected-event property editing are implemented. Direct
underspecified generation/mutation requests remain unavailable.
This is intentional: discovery never implies an operation is
implemented.

`ApplicationService` accepts `ApplicationCommand` values through a bounded
256-command queue by default. A mutex gives multiple application producers one
monotonic sequence; the control-side consumer executes commands in that order.
Overflow rejects the newest command and increments `rejected_commands()`.
Commands carry a source ID and source sequence for correlation. They only call
control-side `TimelineService`, `SampleManager`, and `AudioEngine` command
methods; no command mutates callback-owned data.

## Observable state

`ApplicationState` is a value snapshot containing transport state, frame and tick,
tempo and time signature, selected track/sample/pattern/step, step page, pad
mode, parameter page, focus, modifier state, currently available action IDs,
sample loading snapshots and timeline revision. Frontends receive copies, not
mutable pointers.

The application revision increases for accepted state changes and for observed
transport, timeline or sample-state changes. Timeline and sample revisions are
also retained so a client can correlate subsystem updates precisely. Rapid
commands remain ordered; asynchronous sample state is refreshed from
`SampleManager` without allowing an old completion to replace newer metadata.
Polling snapshots is the initial notification mechanism. Subscription and
delta transport can be added without changing command semantics.

## Parameters

`ParameterRegistry` holds stable IDs and widget-independent metadata: name,
type, range, default/current value, unit, step size, continuous/discrete flags,
automation support and modulation support. Boolean, integer, floating-point and
enumerated values are supported.

The registry provides set, normalized set, relative adjustment, query,
enumeration and reset. Normalized input maps an absolute fader or potentiometer
from 0..1 into the declared range. Relative encoders apply `steps * step` and
clamp. This is application parameter infrastructure, not a DSP parameter or
audio-rate modulation system.

## Capabilities and profiles

Controllers declare only the controls and feedback they actually possess.
Input types include momentary/toggle buttons, relative encoders, absolute pots,
faders, crossfaders, joysticks, touch X/Y, keyboards and pointers. Feedback
types include RGB/single LEDs, text, parameter values and a future motorized
fader capability.

`ControllerProfile` maps control IDs to semantic action or parameter IDs with
independent Shift and Select layers, labels, absolute/relative behavior and
feedback destinations. `ControllerMapper` validates profiles, resolves a
control event against effective momentary/latched modifiers, and produces a
semantic command or parameter update. Feedback includes the application
revision and origin; a destination matching the origin is suppressed to avoid
echo loops.

The version-1 line format is intentionally small and dependency-free:

```text
# NEUROGENIC_CONTROLLER 1
controller|controller.id|Display name
control|play|momentary|Play
control|tempo|encoder|Tempo
bind|play|action|transport.play.v1|feedback=led.play
bind|tempo|parameter|transport.tempo|shift|label=Fine tempo
```

Unknown records, control types, modifier options, controls and actions fail
validation. The parser does not yet persist feedback capability declarations
or static action arguments. Complete examples are under
[`examples/controllers`](../examples/controllers):

- `full-performance.ngc`: representative performance controls, encoders,
  fader and transport bindings. The C++ built-in capability profile additionally
  declares 16 RGB grid buttons, eight encoders, four faders, a crossfader,
  Shift/Select, mode buttons and optional touchscreen.
- `minimal.ngc`: data encoder, Enter, Back and optional joystick/display.
- `generic-midi.ngc`: generic buttons, knob and fader mappings.

These profiles describe capabilities; they do not claim physical hardware has
been tested or implement MIDI learn.

## Performance grid

`PerformanceGrid` converts any virtual or physical index 0..15 to semantic
events independently of MIDI notes or button placement:

- STEP normally toggles `page * 16 + pad`; Select produces selection only.
  `grid_application_command` maps both to shared application actions. Pages
  permit arbitrary pattern lengths.
- PAD resolves an initial pad/sample semantic event. Sample/instrument triggers
  are supported by the engine; slicing is not.
- 16 LEVELS targets the selected slot with 16 velocity values. The semantics
  are ready, but pitched-instrument behavior is not implemented.
- 16 NOTES emits a configurable chromatic note layout. Scale layouts and
  pitched sample playback remain future work.

Changing `PadMode` only changes application state and never sends a transport
command. A controller without 16 buttons can present the same grid virtually.

## Navigation, text and X/Y

Focus next/previous, relative adjust, confirm and cancel are semantic actions.
The minimal profile uses Enter to move Transport → Tracks → Steps and the
encoder to select a track or step. Enter on a step selects its note (creating
the target note when empty) and opens properties. Encoder turns adjust the
active property, Enter cycles velocity/duration/pitch/timing, and Back returns
one level. Full controllers can map Select+grid to event selection, encoders to
property actions, and buttons to Undo/Redo.
Shift and Select each
have momentary and latched state. Mapping layers resolve before an action is
emitted, preventing a mode change from implicitly firing the base binding.

`TextEntryModel` supports direct text, encoder character selection, insertion,
Back deletion/cancel, cursor movement, commit and discard. It has no rendering
dependency; host tests enter and edit text using encoder, Enter and Back
semantics.

`XYControl` accepts normalized coordinates from touch, joystick, paired
encoders, MIDI, Lua or remote adapters. Metadata covers spring return,
latching, relative movement, inversion and per-axis scaling. Updates can travel
through the bounded application queue; no modulation or audio callback work is
performed here.

## Current limitations

An unbound Shift or Select layer falls back to its base binding by default;
profiles can disable this with `fallback_to_base`. Step selection and step
toggling remain distinct semantic actions.

There is no device firmware, MIDI learn, project persistence for note targets
or profiles, state-delta subscription, parameter locks, modulation matrix,
scale engine, slicing or pitch shifting. The profile files are validated
examples, not a commitment to a particular master-controller layout.

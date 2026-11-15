# HERETICC

HERETICC is an open-source custom firmware extension for the Akai MPC Sample. It aims to add a 16-track polyphonic sequencer, independent track patterns, scenes, song arrangements, parameter locks, tracker-style commands, non-destructive slicing, performance controls, and MIDI integration while preserving the stock sampling workflow and hardware experience wherever practical.

## Status

HERETICC is in **M0: research and architecture**. No firmware, device binary, installer, or deployment tool has been implemented. Nothing in this repository has been tested on MPC Sample hardware.

The initial technical direction is:

1. build a deterministic, device-independent Rust sequencer core with host-side tests;
2. validate a narrow integration spike against [`mpc-sample-sdk`](https://github.com/ioma8/mpc-sample-sdk) on the supported firmware;
3. choose the final stock-engine integration only after measuring event timing, trigger control, persistence, and coexistence with the stock sequencer.

This is approach **C first**, with **A as the leading integration candidate**. No external dependency has been added yet. See [Architecture](docs/ARCHITECTURE.md) and [Research](docs/RESEARCH.md).

## Design goals

- Sixteen independent polyphonic tracks, corresponding to the sixteen pads.
- A pad-bound default sound plus freely assignable sample and slice events.
- Independent track patterns, lengths, timing, and polymetric playback.
- Scenes that reference track patterns; songs that arrange scenes.
- Quantized transitions, parameter locks, retriggers, pitch slides, and sample offsets.
- Grid and tracker editing over one event model.
- Non-destructive slicing and live slice capture.
- A logical mixer with track inserts, shared sends, master inserts, performance effects, macros, temporary track overrides, and MIDI control.
- Reversible SD-card deployment, with no partition or firmware flashing.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — boundaries, data flow, and technical recommendation
- [Hardware](docs/HARDWARE.md) — verified controls, display/audio facts, and compatibility limits
- [Sequencer](docs/SEQUENCER.md) — musical model and behavior
- [Effects](docs/EFFECTS.md) — logical routing, parameter model, performance controls, and feasibility gates
- [Research](docs/RESEARCH.md) — source-based review of related projects
- [Roadmap](docs/ROADMAP.md) — milestones and acceptance criteria
- [Safety](docs/SAFETY.md) — development, deployment, and recovery rules
- [AGENTS.md](AGENTS.md) — conventions for future Codex work

## Scope and legal notice

HERETICC is not affiliated with Akai Professional or inMusic Brands. Akai and MPC are trademarks of their respective owners.

Do not commit proprietary firmware binaries, extracted firmware code, keys, identifiers, or copyrighted firmware assets. External code may be reused only after its license and provenance are recorded. Research notes describe third-party behavior; they do not import third-party code.

# HERETICC

HERETICC is an open-source custom firmware extension for the Akai MPC Sample. It aims to add a 16-track polyphonic sequencer, independent track patterns, scenes, song arrangements, parameter locks, tracker-style commands, non-destructive slicing, performance controls, and MIDI integration while preserving the stock sampling workflow and hardware experience wherever practical.

## Status

HERETICC is in **M0: host core and integration research**. M0.2a hardens the hardware-independent Rust sequencer and project decoder and adds a host-only semantic event adapter. No firmware, device binary, audio engine, installer, or deployment tool has been implemented. Nothing in this repository has been tested on MPC Sample hardware.

The initial technical direction is:

1. build a deterministic, device-independent Rust sequencer core with host-side tests;
2. validate a narrow integration spike against [`mpc-sample-sdk`](https://github.com/ioma8/mpc-sample-sdk) on the supported firmware;
3. choose the final stock-engine integration only after measuring event timing, trigger control, persistence, and coexistence with the stock sequencer.

This is approach **C first**, with **A as the leading integration candidate**. The Rust workspace has no third-party crate dependencies. See [Architecture](docs/ARCHITECTURE.md) and [Research](docs/RESEARCH.md).

## Host development

The workspace contains [`hereticc-core`](crates/hereticc-core), a standard-library-only Rust crate, and [`hereticc-host`](crates/hereticc-host), a host-only event consumer and demonstration. A Nix development shell supplies Cargo, rustc, rustfmt, and Clippy:

```sh
nix develop
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
cargo run -p hereticc-host --bin demo
```

The core currently implements integer musical time, validated sixteen-track projects, independent patterns, scenes, basic note/parameter scheduling, overlapping runtime voices, phase-reset scene transitions, transactional capacity/arithmetic errors, and a bounded versioned host-side codec. The host adapter verifies event order and voice lifecycle and produces deterministic text traces. Neither crate is real-time safe or processes audio.

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
- [Device adapter contract](docs/ADAPTER.md) — future clock, voice, capacity, and recovery boundary
- [AGENTS.md](AGENTS.md) — conventions for future Codex work

## License

HERETICC's original code and documentation are available under the [MIT License](LICENSE), copyright 2026 Lassi Laitinen. This license does not cover Akai/inMusic firmware, SDK components, trademarks, or third-party projects. Any future dependency or imported third-party material requires a separate provenance and license review, with upstream notices preserved.

## Scope and legal notice

HERETICC is not affiliated with Akai Professional or inMusic Brands. Akai and MPC are trademarks of their respective owners.

Do not commit proprietary firmware binaries, extracted firmware code, keys, identifiers, or copyrighted firmware assets. External code may be reused only after its license and provenance are recorded. Research notes describe third-party behavior; they do not import third-party code.

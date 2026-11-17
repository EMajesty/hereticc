# NEUROGENIC

<p align="center">
  <img src="assets/images/vibeslop.png" alt="Absolute vibeslop" width="512">
</p>

NEUROGENIC is an open-source custom firmware extension for the Akai MPC Sample. It aims to add a 16-track polyphonic sequencer, independent track patterns, scenes, song arrangements, parameter locks, tracker-style commands, non-destructive slicing, performance controls, and MIDI integration while preserving the stock sampling workflow and hardware experience wherever practical.

## Status

NEUROGENIC is in **M0: host core and guarded integration feasibility**. The M0.2/M0.2a host sequencer foundation is complete. M0.3b-A4 validated the firmware fingerprints, and M0.3c's SDK v0.7.1 page/input prototype was physically validated on one MPC Sample running firmware 1.3.0.12. M0.3d.3 failed physical input-stability acceptance; M0.3e restores the M0.3c editor model. M0.4 host-tests one bounded stock-event request, but native insertion remains blocked on an SDK-owned UI-thread sequence-command API. The B1 diagnostic models remain separate and B1 hook testing is unauthorized.

Project website: [neurogeni.cc](https://neurogeni.cc). Source repository: [EMajesty/neurogenic](https://github.com/EMajesty/neurogenic).

NEUROGENIC was previously developed under the name HERETICC. Historical experiment artifacts retain their original identifiers for traceability.

The initial technical direction is:

1. build a deterministic, device-independent Rust sequencer core with host-side tests;
2. validate a narrow integration spike against [`mpc-sample-sdk`](https://github.com/ioma8/mpc-sample-sdk) on the supported firmware;
3. choose the final stock-engine integration only after measuring event timing, trigger control, persistence, and coexistence with the stock sequencer.

This is approach **C first**, with **A as the leading integration candidate**. The Rust workspace has no third-party crate dependencies. See [Architecture](docs/ARCHITECTURE.md) and [Research](docs/RESEARCH.md).

## Host development

The workspace contains [`neurogenic-core`](crates/neurogenic-core), [`neurogenic-host`](crates/neurogenic-host), and the isolated standard-library-only [`neurogenic-b1-relay-model`](crates/neurogenic-b1-relay-model). The relay model performs deterministic parsing and state validation only; it has no network or device integration. A Nix development shell supplies Cargo, rustc, rustfmt, and Clippy:

```sh
nix develop
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
cargo run -p neurogenic-host --bin demo
```

To enter the same development shell automatically with
[direnv](https://direnv.net/), allow the repository's checked-in `.envrc` once:

```sh
direnv allow
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
- [Reuse-first integration](docs/REUSE-FIRST-INTEGRATION.md) — upstream disposition and next SDK prototype
- [M0.3c SDK UI](docs/M0-3C-SDK-UI.md) — first page/input implementation and physical-test procedure
- [M0.3d stock trigger](docs/M0-3D-STOCK-TRIGGER.md) — bounded native pad audition candidate and test procedure
- [M0.3d.1 pad ownership](docs/M0-3D1-PAD-OWNERSHIP.md) — ownership correction and bounded dispatch diagnosis
- [M0.3d.2 native properties](docs/M0-3D2-NATIVE-PROPERTIES.md) — null-properties contract and controlled native forwarding
- [M0.3d.3 input stability](docs/M0-3D3-INPUT-STABILITY.md) — release ledger, bank scope, repaint, and restoration correction
- [M0.3e sequencer integration](docs/M0-3E-SEQUENCER-INTEGRATION.md) — stable editor restoration and reuse-first stock sequence plan
- [M0.4 native sequencer](docs/M0-4-NATIVE-SEQUENCER.md) — bounded one-step request and verified native insertion blocker
- [Roadmap](docs/ROADMAP.md) — milestones and acceptance criteria
- [Safety](docs/SAFETY.md) — development, deployment, and recovery rules
- [Device adapter contract](docs/ADAPTER.md) — future clock, voice, capacity, and recovery boundary
- [UI architecture](docs/UI.md) — page model, rendering targets, focus, and restoration
- [Integration experiments](docs/INTEGRATION-EXPERIMENTS.md) — gated physical-device feasibility plan
- [SDK hook safety](docs/SDK-HOOK-SAFETY.md) — pinned loader audit and zero-hook design
- [Device testing](docs/DEVICE-TESTING.md) — package inspection and separately authorized physical procedure
- [AGENTS.md](AGENTS.md) — conventions for future Codex work

## License

NEUROGENIC's original code and documentation are available under the [MIT License](LICENSE), copyright 2026 Lassi Laitinen. This license does not cover Akai/inMusic firmware, SDK components, trademarks, or third-party projects. Any future dependency or imported third-party material requires a separate provenance and license review, with upstream notices preserved.

## Scope and legal notice

NEUROGENIC is not affiliated with Akai Professional or inMusic Brands. Akai and MPC are trademarks of their respective owners.

Do not commit proprietary firmware binaries, extracted firmware code, keys, identifiers, or copyrighted firmware assets. External code may be reused only after its license and provenance are recorded. Research notes describe third-party behavior; they do not import third-party code.

# NEUROGENIC

<p align="center">
  <img src="assets/images/vibeslop.png" alt="Absolute vibeslop" width="512">
</p>

NEUROGENIC is an open-source custom firmware extension for the Akai MPC Sample. It aims to add an independently owned sequencer with 16 audio and 16 MIDI tracks initially, extensible logical track identities, independent patterns, scenes, song arrangements, parameter locks, tracker-style editing, non-destructive slicing, performance controls, and MIDI integration while preserving the stock sampling workflow and hardware experience wherever practical.

## Status

NEUROGENIC is in **M0: guarded native-sequencer integration**. The M0.2/M0.2a host sequencer foundation is complete, and M0.3c's SDK v0.7.1 page/input prototype was physically validated on one MPC Sample running firmware 1.3.0.12. M0.4e physically verifies native insertion, unique observation, exact removal, absence, audible stock-engine playback, and stock restoration. M0.5c physically validates the Step Edit replacement and direct native grid editing. M0.6 selects a NEUROGENIC-owned timeline with bounded native stock-sequence materialization for audio and a later transport-correlated MIDI backend; live playback-window replacement remains unverified. The B1 diagnostic models remain separate and B1 hook testing is unauthorized.

Project website: [neurogeni.cc](https://neurogeni.cc). Source repository: [EMajesty/neurogenic](https://github.com/EMajesty/neurogenic).

NEUROGENIC was previously developed under the name HERETICC. Historical experiment artifacts retain their original identifiers for traceability.

The current technical direction is:

1. build a deterministic, device-independent Rust sequencer core with host-side tests;
2. compile bounded audio event windows into disposable native stock sequence data so the stock transport, sampler, routing, and effects remain in use;
3. schedule MIDI and higher-level scenes/songs from the same NEUROGENIC timeline after transport correlation is measured.

This is a hybrid with one musical source of truth, not a mirrored stock project. Direct firmware voice calls remain a targeted fallback, not the primary scheduler. The Rust workspace has no third-party crate dependencies. See [M0.6 playback architecture](docs/M0-6-PLAYBACK-ARCHITECTURE.md), [Architecture](docs/ARCHITECTURE.md), and [Research](docs/RESEARCH.md).

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

- Sixteen audio and sixteen MIDI tracks initially, with extensible logical identities.
- Tracks independent of pads; one audio track may contain a complete drum-kit performance.
- Independent track patterns, lengths, timing, and polymetric playback.
- Scenes that reference track patterns; songs that arrange scenes.
- Quantized transitions, parameter locks, retriggers, pitch slides, and sample offsets.
- A vertically scrolling tracker editor with fixed selectable row resolution, expanding note columns, exact timestamps, and non-destructive resolution changes.
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
- [M0.4 native sequencer](docs/M0-4-NATIVE-SEQUENCER.md) — first physically confirmed stock event and audible playback
- [M0.4d event identity](docs/M0-4D-NATIVE-EVENT-IDENTITY.md) — unique native observation/removal gate for the playable grid
- [M0.4e/M0.5 grid boundary](docs/M0-4E-M0-5-NATIVE-GRID.md) — verified lifecycle and commit-on-proof grid coordinator
- [M0.5a playable native grid](docs/M0-5A-PLAYABLE-NATIVE-GRID.md) — Step Edit overlay and first physical acceptance procedure
- [M0.5c Step Edit presentation](docs/M0-5C-STEP-EDIT-PRESENTATION.md) — automatic grid presentation and guarded physical test
- [M0.5d DATA and redraw](docs/M0-5D-DATA-AND-REDRAW.md) — encoder normalization and complete presentation redraw candidate
- [M0.6 playback architecture](docs/M0-6-PLAYBACK-ARCHITECTURE.md) — independent sequencer ownership and stock-engine playback decision
- [M0.6a static materialization](docs/M0-6A-STATIC-MATERIALIZATION.md) — bounded playback-window compiler and guarded batch experiment
- [M0.6b live-window feasibility](docs/M0-6B-LIVE-WINDOW-FEASIBILITY.md) — transport-clock evidence and fail-closed replacement decision
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

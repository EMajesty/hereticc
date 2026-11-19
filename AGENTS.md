# AGENTS.md

- Use C++20 and CMake. Keep engine state independent of UI and hardware.
- Keep file I/O, allocation, locks, logging, Lua, and blocking work out of audio callbacks.
- Publish live samples as immutable bounded versions. Audio callbacks may mark
  versions retired but must never destroy them; reclamation belongs to a
  non-audio thread, and active voices must retain valid old sample data.
- Keep frontend sample metadata callback-acknowledged: active fields describe
  the audio-active version, pending fields describe the newest command, and an
  older completion must never overwrite a newer revision's visible state.
- Put editing, generation, mutation, validation, revisions, and observable
  state in reusable C++ application services. Lua is a thin client adapter;
  hardware, UI, MIDI, and remote operations must not require Lua execution.
- Route controller inputs through stable semantic actions or parameters and
  expose copied revisioned state. Adapters must not mutate engine internals or
  encode a particular physical layout in the core.
- Use integer musical time and deterministic offline tests.
- Put audio/MIDI/platform details behind narrow adapters.
- Preserve dependency pins, licenses, notices, and stable external identifiers.
- Keep portable x86_64/AArch64 implementations as the baseline. Do not add
  `-march=native`, AVX/AVX2 requirements, or unconditional ISA-specific code;
  optimized paths require feature selection and a portable fallback.
- Distinguish host tests from physical audio/MIDI hardware tests.
- Do not rely on a global `LD_LIBRARY_PATH` for Nix runtime dependencies; retain
  required ELF dependencies and verify they resolve in a clean environment.
- Before committing build, backend, or Nix changes, run the dependency-enabled
  preset build and CTest suite, `nix build`, `nix flake check`, and the explicit
  headless device-enumeration smoke test. Report any command that was not run.
- For every behavioral bug fix, add a deterministic regression test. Before any
  commit, run the dependency-enabled build and CTest suite plus the ASan/UBSan
  preset where supported. Fix known failures; never knowingly commit one.
- Run `nix build`, `nix flake check`, and the headless smoke test before every
  commit that changes C++, CMake, dependencies, adapters, Nix, or CI. Only defer
  tests requiring physical hardware or subjective listening, and document them.

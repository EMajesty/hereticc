# AGENTS.md

- Use C++20 and CMake. Keep engine state independent of UI and hardware.
- Keep file I/O, allocation, locks, logging, Lua, and blocking work out of audio callbacks.
- Use integer musical time and deterministic offline tests.
- Put audio/MIDI/platform details behind narrow adapters.
- Preserve dependency pins, licenses, notices, and stable external identifiers.
- Distinguish host tests from physical audio/MIDI hardware tests.
- Do not rely on a global `LD_LIBRARY_PATH` for Nix runtime dependencies; retain
  required ELF dependencies and verify they resolve in a clean environment.
- Before committing build, backend, or Nix changes, run the dependency-enabled
  preset build and CTest suite, `nix build`, `nix flake check`, and the explicit
  headless device-enumeration smoke test. Report any command that was not run.

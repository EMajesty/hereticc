# AGENTS.md

- Use C++20 and CMake. Keep engine state independent of UI and hardware.
- Keep file I/O, allocation, locks, logging, Lua, and blocking work out of audio callbacks.
- Use integer musical time and deterministic offline tests.
- Put audio/MIDI/platform details behind narrow adapters.
- Preserve dependency pins, licenses, notices, and stable external identifiers.
- Distinguish host tests from physical audio/MIDI hardware tests.

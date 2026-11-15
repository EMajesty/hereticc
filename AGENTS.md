# AGENTS.md

These instructions apply to the entire repository.

## Project state

HERETICC is currently research and architecture. Read `README.md` and the relevant files in `docs/` before changing scope. Keep planned, implemented, emulator-tested, and hardware-tested behavior explicitly distinct.

## Engineering conventions

- Prefer Rust for the custom sequencer core and integration code when technically appropriate.
- Keep the sequencing engine independent of device-specific interfaces, firmware objects, operating-system APIs, UI, and audio backends.
- Use host-side automated tests for timing, pattern handling, transitions, persistence, and event scheduling.
- Use integer musical time and deterministic event ordering. Do not use wall-clock sleeps as sequencer tests.
- Put firmware-specific behavior behind small HERETICC-owned adapter traits so it can be replaced and faked.
- Never assume undocumented firmware interfaces are stable or safe. Record firmware version, evidence, thread/lifetime contract, and failure behavior for each use.
- Avoid blocking operations, memory allocation, file I/O, drawing, and contended locks in real-time audio callbacks. Preallocate bounded storage and define overload behavior.
- Validate unsupported firmware before installing hooks and fail closed.
- Document external dependencies, exact revisions where relevant, and their licenses. Do not add or copy external code without a separate reviewed task.
- Do not claim hardware functionality is tested unless it has actually been tested on physical hardware. Identify host and emulator results as such.
- Keep documentation concise, technical, linked to primary sources, and maintainable. Pin research links to commits when practical.

## Prohibited repository content and operations

- No proprietary Akai/inMusic firmware binaries, extracted firmware code, keys, device identifiers, partition images, root filesystems, or copyrighted firmware assets.
- Do not implement firmware flashing, control-surface firmware updates, bootloader changes, partition modification, or device restore tooling.
- Prefer reversible SD-card-based deployment. Device disk writes require a new, explicit project decision; they are not authorized by the current roadmap.
- Do not include secrets or unreviewed device logs/captures.
- Do not copy code or assets from a project without a compatible explicit license and required notices. Public source without a license is not reusable.

## Workflow

1. Inspect the working tree and preserve user changes.
2. State whether the task is documentation, host core, emulator integration, or physical-device work.
3. For firmware-facing work, update `docs/RESEARCH.md`, `docs/HARDWARE.md`, and the risk/compatibility notes as evidence changes.
4. Add tests before or with behavioral changes; run the smallest relevant host suite and report what was not run.
5. Do not create installers or device deployment tooling unless the task explicitly authorizes it and the safety milestone permits it.
6. Do not commit or push unless the repository owner explicitly asks.

## Architecture guardrails

- The core emits semantic scheduled events; adapters resolve samples, slices, voices, UI, and clocks.
- Never store live firmware pointers in project data or core events.
- Grid and tracker editors must share one event representation.
- Performance overrides are separate from saved pattern data unless explicitly recorded.
- A replacement audio engine is not an implicit fallback. If stock-engine integration fails, document the evidence and request an architecture decision.


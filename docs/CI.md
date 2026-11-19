# Continuous integration

`.github/workflows/ci.yml` runs for pushes and pull requests on native Linux
x86_64 and GitHub-hosted AArch64 runners. Both install Nix, use the committed
`flake.lock`, evaluate/build their native flake outputs, run the CTest suite
through the package check phase, and perform
`neurogenic --headless --list-devices` as a headless smoke test. The Linux
CTest suite also checks that `libasound` resolves with `LD_LIBRARY_PATH` unset.
The x86_64 lane additionally builds and runs the dependency-enabled test suite
with GCC AddressSanitizer and UndefinedBehaviorSanitizer, and its flake check
cross-compiles the complete dependency-enabled application for AArch64 without
executing it.

CI requires no audio device, MIDI port, or repository secret. The smoke test
uses null audio explicitly and may report unavailable MIDI or no devices. It
fails only if initialization or enumeration is not handled safely. Physical audio/MIDI,
latency, permissions, host PipeWire/ALSA configuration, and hotplug behavior are
outside CI's evidence.

Native AArch64 host tests establish architecture/runtime compatibility in the
hosted environment, but the runner's null/headless devices do not establish
physical ARM audio/MIDI compatibility. The x86-hosted cross build is compilation
evidence only. No CI job enables `-march=native`, AVX, AVX2, or another
machine-specific baseline.

## Nix build hygiene

Use Nix to pin the toolchain and dependencies, not as the inner edit/build
loop. Run `nix develop` (or `direnv allow`) once, configure `build/dev` once,
then reuse incremental CMake/Ninja builds and CTest. CMake's FetchContent source
trees remain in that build directory. Avoid `nix flake update` unless dependency
updates are the task; it changes reproducible inputs and can retain additional
closures. `nix build` creates a `result` GC root, while shells, profiles and
other result links may retain older closures. Sanitizer and AArch64 cross builds
are intentionally separate and should be run at verification points rather
than for every edit.

The project provides a read-only report:

```sh
./tools/nix-disk-report.sh
```

Useful manual diagnostics include `df -h /nix/store`,
`nix-store --gc --print-roots`, `nix path-info -Sh /run/current-system`, and
inspection of project symlinks into `/nix/store`. These commands do not reclaim
space. Garbage collection, profile-history removal and deletion of GC roots are
system/user maintenance decisions: inspect what retains a closure first and
run destructive cleanup only deliberately. NEUROGENIC does not automate Nix
garbage collection, alter NixOS configuration or delete unrelated outputs.

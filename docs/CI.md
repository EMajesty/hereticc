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

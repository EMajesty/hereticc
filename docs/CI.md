# Continuous integration

`.github/workflows/ci.yml` runs for pushes and pull requests on Linux x86_64.
It installs Nix, uses the committed `flake.lock`, evaluates/builds the flake,
runs the CTest suite through the package check phase, and performs
`neurogenic --list-devices` as a headless smoke test.

CI requires no audio device, MIDI port, or repository secret. The smoke test is
expected to report an unavailable native backend or no devices. It fails only
if initialization or enumeration is not handled safely. Physical audio/MIDI,
latency, permissions, host PipeWire/ALSA configuration, and hotplug behavior are
outside CI's evidence.

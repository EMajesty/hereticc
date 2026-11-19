# Graphical interface

NEUROGENIC uses SDL3's built-in 2D renderer behind a renderer-independent C++
UI model. SDL3 supplies X11/Wayland windows and keyboard, pointer and touch
events while leaving a future KMSDRM route. GLFW was considered, but would also
need a 2D renderer and separate touch handling; a retained desktop toolkit adds
policy and weight not needed by this instrument interface. Primary references:
[Wayland](https://wiki.libsdl.org/SDL3/README-wayland),
[high DPI](https://wiki.libsdl.org/SDL3/README-highdpi), and
[video/KMSDRM settings](https://wiki.libsdl.org/SDL3/EnvironmentVariables).

Build normally, then run `./build/dev/neurogenic --gui --headless` for a window
with null audio, or use `--fullscreen` with normal audio/sample options.
`-DNEUROGENIC_ENABLE_GUI=OFF` preserves a headless build. If SDL3 is absent,
the application builds and `--gui` returns a useful unavailable diagnostic.

The layout uses a 1024 × 600 logical canvas and uniformly scales/letterboxes at
higher 16:9, 16:10 and near-17:10 resolutions. It has a persistent status bar
and Sequencer, Samples, Parameters and X/Y views. Timeline and slot information
comes from application snapshots. Step editing is explicitly unavailable.
Filesystem browsing is outside the audio callback; a graphical file dialog and
virtual keyboard are future adapters to the existing path/text models.

Defaults are arrows for navigation/adjustment, Enter to confirm, Escape or
Backspace to cancel, Tab to cycle focus, Space to play/stop, Shift as modifier,
and number keys for step selection. Mouse and touch select views and controls.
X/Y dragging is limited to 125 commands per second. All input passes through
`ApplicationService`; rendering never runs in or blocks the audio callback.

CI runs the renderer-independent interaction suite and an SDL dummy-video
smoke test. The AArch64 cross-check compiles the portable model with the SDL
adapter disabled; native AArch64 flake outputs include SDL3. Physical
touchscreen, HDMI fullscreen, GPU combinations and
subjective stage readability remain unverified.

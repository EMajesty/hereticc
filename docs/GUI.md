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

The layout has a 1024 × 600 minimum logical canvas, scales uniformly, and lets
content consume extra horizontal space at wider landscape resolutions.
Viewport dimensions invalidate drawing and hit-test geometry. It has a persistent status bar
and Sequencer, Samples, Parameters and X/Y views. Timeline and slot information
comes from application snapshots. The sequencer displays actual target-note
events, selection, playhead, pages, resolution and note target. Clicking or
tapping a valid cell toggles it through `step.toggle.v1`.
Filesystem browsing is outside the audio callback; a graphical file dialog and
virtual keyboard are future adapters to the existing path/text models.

In the Sequencer, Left/Right select steps, Up/Down select tracks, Enter toggles
the selected step, Shift+Left/Right changes pages, and Shift+Up/Down changes
the target note. Number keys toggle current-page cells; Shift selects only.
Elsewhere, defaults are arrows for adjustment, Enter to confirm, Escape or
Backspace to cancel, Tab to cycle focus, Space to play/stop, Shift as modifier,
and number keys for step selection. Mouse and touch select views and controls.
X/Y dragging is limited to 125 commands per second. All input passes through
`ApplicationService`; rendering never runs in or blocks the audio callback.

CI runs the renderer-independent interaction suite and an SDL dummy-video
smoke test. The AArch64 cross-check compiles the portable model with the SDL
adapter disabled; native AArch64 flake outputs include SDL3. Physical
touchscreen, HDMI fullscreen, GPU combinations and
subjective stage readability remain unverified.

Edits publish at the next audio callback as complete validated plans. A final
partial step is available when its start tick lies inside a non-grid-aligned
pattern. Undo, parameter locks, drag-painting and boundary-quantized publication
are not implemented.

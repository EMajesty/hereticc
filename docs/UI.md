# UI architecture

## Status and visual target

This is an M0.3a design target for a future 320×240 interface. No renderer or hardware UI has been implemented. The desired style combines tracker density, Elektron-like hierarchy, and restrained industrial colors without copying proprietary artwork, fonts, or layouts.

The pinned SDK verifies post-stock drawing on the firmware's Sample-page class using ARGB color, filled rectangles, and stock-font text. It does not verify exclusive full-screen ownership, global theming, custom fonts, bitmap blits, partial redraws, or acceptable refresh cost. Those distinctions constrain every design below.

## Page hierarchy

```text
HERETICC mode
├── Perform
│   ├── scene launch
│   └── performance FX/macros
├── Grid
│   ├── track pattern
│   └── step/event detail
├── Tracker
│   ├── row/event columns
│   └── command/parameter detail
├── MIX/FX
│   ├── track mixer
│   ├── insert/send/master routing
│   └── effect parameter editor
└── Project
    ├── patterns/scenes
    └── diagnostics/capabilities
```

Grid and Tracker are projections of the same core events. Perform holds transient gestures and scene state; it never rewrites saved parameters unless recording/edit mode is explicit. MIX/FX edits the logical model from `EFFECTS.md`; unavailable device routes remain visible as unsupported rather than being simulated.

## Screen responsibilities

### Persistent frame

- 16-pixel status header: page, active track/pattern/scene, transport observation, pending transition, and capability/error indicator.
- Main work area: approximately 204 pixels high; exact geometry remains a renderer decision.
- 20-pixel soft-key footer aligned to B1–B3. A physical button is consumed only when its current action is visibly labeled.

### Grid

Shows one track pattern, 16 primary steps, playhead, note duration/voice overlap hints, and page/zoom for longer patterns. Pads select/toggle the visible cells according to explicit edit mode. Event detail exposes alternate sound/slice and typed parameter events without creating a second representation.

### Tracker

Shows rows with note/sound, gate, parameter, and future command columns. DATA moves/edits the focused cell; K1–K3 operate the visible parameter bank with pickup. Dense text must remain legible using the verified stock font first. A custom bitmap font is a later measured option, not a baseline assumption.

### Perform

Shows 16 pad assignments, momentary/latched state, target, quantization, and macro/fader value. Pads decide what happens, K1–K3 decide how, and the fader decides how much. Press/release gestures do not alter saved parameters. Scene and effect engagement never reset the underlying sequencer implicitly.

### MIX/FX

Shows track gain/pan/sends/mute, two logical inserts, two shared returns, and master/performance stages. The screen must label each route as available, degraded, or unsupported from the adapter capability snapshot. It must not imply that logical tracks are independent stock audio buses.

## Shared widgets and typography

- `StatusBar`, `SoftKeys`, `PadMatrix`, `StepGrid`, `TrackerRows`, `ParameterStrip`, `Meter`, `CapabilityBadge`, and modal `Notice` are renderer-independent view models.
- Use a small neutral palette: near-black background, two neutral text levels, one selection accent, warning amber, error red, and per-track colors only where identity matters.
- Use size/spacing/contrast before color. Selected, playing, muted, pending, and unavailable states must remain distinguishable on the physical panel.
- The first renderer uses the stock font face and measured sizes. Custom fonts require either a new reviewed stock-typeface interface or a bounded bitmap-glyph renderer. The current SDK exposes neither font loading nor image blitting.
- Text layout must tolerate the SDK's 95-byte per-call truncation and unknown glyph coverage. Pre-layout and clipping happen in HERETICC-owned host logic.

## Future renderer abstraction

A platform-neutral renderer consumes an immutable `UiFrame` and emits bounded primitives:

- fill rectangle;
- draw text with logical style and bounding box;
- optional future monochrome/ARGB bitmap after feasibility proof;
- explicit full-frame damage initially, optional damage regions only after SDK support/measurement.

The backend maps logical styles to available stock colors/font sizes. It owns no sequencer state and receives no firmware pointers. Host snapshot tests can validate layout geometry, primitive bounds, text truncation, focus visibility, and deterministic frames without an SDK dependency.

M0.3a does not add this API yet. The paint hook is currently Sample-page-specific, has no verified damage contract, and may not be exclusive; those findings could materially change the smallest safe renderer boundary.

## Screen ownership and restoration

HERETICC requests a page-scoped display lease only when the current component class and surface generation are supported. During a paint callback it lets stock paint first, then covers/draws the complete HERETICC frame. It never retains the graphics handle.

Lease release is mandatory on normal exit, page change, firmware-object generation change, panic/error boundary, or unsupported state:

1. stop requesting HERETICC repaints;
2. clear transient input ownership while preserving paired releases;
3. restore all SDK pad-light overrides;
4. clear performance/macro overrides;
5. request a fresh stock repaint;
6. forward subsequent inputs unchanged.

Button LEDs are not part of the verified SDK restoration API and must remain stock-owned in the first experiment.

## Input focus and pickup

Focus consists of page, region/widget, edit mode, and captured surface generation. Each input is classified as observe, consume, or forward. A consumed press owns its release even if the page changes. Duplicate surface and high-level pad paths must be deduplicated.

K1–K3 and the fader use pickup: after focus/parameter changes, movement is observed but forwarded or ignored by HERETICC until the physical value crosses the logical value within a defined tolerance. Pickup completion is visible. MAIN VOLUME remains stock-owned.

SHIFT is a modifier, not a permanent mode key. A lone SHIFT press should continue to stock unless a validated chord-claim/replay mechanism preserves the complete native gesture. ERASE is owned only by a focused editor. Transport remains forwarded until an approved transport-owner experiment passes. No permanent HERETICC activation shortcut is selected by this document.

## Physical-test questions

- Does a full-screen post-paint cover every Sample-page child and transient overlay without flicker?
- Which dialogs/pages bypass or repaint after the hooked class?
- What repaint rate and primitive count fit the UI deadline under audio and sample-loading load?
- Are broad repaint requests always full-screen, and can partial damage be added safely?
- Which stock font sizes are legible, which glyphs exist, and is bitmap-glyph drawing affordable?
- Can leaving the mode always force a clean stock repaint without stale pixels?
- Are all raw codes for B1–B3, fader, transport, and mode buttons stable on hardware and page-independent?
- Can input press/release ownership survive modal changes, project loads, and overlapping callbacks?
- Does pad-light restore reproduce the latest native state after stock changes while HERETICC owns the lights?

The experiments and pass criteria are specified in [Integration experiments](INTEGRATION-EXPERIMENTS.md).

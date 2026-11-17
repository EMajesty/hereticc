# M0.5d DATA navigation and redraw

Status: implemented, host/QEMU-tested and cross-built; physical validation on
firmware 1.3.0.12 remains pending.

## Physical M0.5c evidence

The owner confirmed that entering Step Edit opens NEUROGENIC, PLUS inserts and
removes the correct native notes, stock transport audibly plays them, and the
stock Step editor shows the expected positions. M0.5c therefore validates the
native edit path. It also exposed three presentation defects: one DATA detent
could move four cells, a committed bit appeared only after later input, and
switching to stock left stale overlay pixels.

## Corrections

The pinned SDK v0.7.1 encoder contract says that `EncoderEvent.delta` is raw,
may have magnitude greater than one, and should use `signum()` for one logical
step per callback. M0.5c passed the complete raw value into the reducer. The
native grid now applies the documented sign-only normalization at the device
boundary; the platform-independent reducer and HIL `MOVE n` semantics remain
unchanged. Direction reversal therefore takes effect on the next callback and
zero remains inert. Bounded diagnostics record callback count, saturating raw
sum, and logical movements so the next run can confirm the physical stream.

M0.5c advanced at most one native state per Step paint. A synchronous add or
remove therefore reached `accepted`, requested another paint, and depended on
that request not being coalesced. M0.5d performs exactly one immediate
verification after a command is accepted in the same verified Step callback.
If the event is not yet visible, the existing callback-local repaint and
eight-observation limit remain in force. Grid bits still commit only after
unique presence or verified absence; no native adapter ABI or comparator has
changed.

SHIFT+DATA previously changed only reducer presentation state and consumed the
whole pair, so there was no reason for stock Step Edit to repaint the areas
covered by the overlay. M0.5d forwards that one complete presentation-switch
pair after changing state. The normal stock handler then schedules a genuine
Step paint: stock paints the whole view first, and NEUROGENIC draws over it only
when selected. No screen or Graphics pointer is retained, and no firmware draw
call is made from an input or background thread. Whether the stock chord has an
unexpected page-specific effect remains a physical-test gate.

`GRID QUERY` v3 retains the existing hook/context/edit counters and adds
pointer-free bounded fields: `enc_cb`, `enc_raw`, `enc_move`, `repaint`,
`switch`, and `commit`. Wire counters saturate at 99 (the signed raw sum at
plus/minus 999) to preserve the established 192-byte response bound; internal
counters remain full-width. No pointer, event bytes, or firmware object is
exposed.

## Physical acceptance

Use the existing guarded `grid-experiment` command. On a disposable one-bar
sequence, verify one detent/one cell, four detents/four cells, immediate reverse
movement, immediate ON and OFF bit redraw, complete stock presentation after
SHIFT+DATA, complete NEUROGENIC presentation after the second chord, the
existing four-hit/three-hit playback test, and automatic stock restoration.
Host tests and QEMU cannot establish encoder detent grouping, framebuffer
restoration, or audible behavior.

## Artifact and validation

The inspectable candidate is
`target/m05a/build/neurogenic-m05a-native-grid.so` (659,760 bytes). Two builds
were byte-identical:

| Artifact | SHA-256 |
|---|---|
| linked ELF | `10601eec0618872ef81d3870f035ad3990047d1046400d4cead56501d19faffb` |
| stripped shared library | `e2f0308b641d91661c0f85046a6ce73bba5d25019baf11445e2114aa6fde107e` |

The AArch64/QEMU fixture passed construction, native equality, ownership,
duplicate rejection, context identity, exact removal, and ABI dispatch. Static
inspection found an AArch64 shared object with non-executable stack, separate
R/E and RW load segments, and only the established libc, pthread, and dl
dependencies. Workspace Rust tests, 114 Python tests, Clippy with warnings
denied, formatting, both SDK patch-integrity checks, Nix flake checks, the
M0.3e rollback package inspection, and whitespace checks passed. These remain
host evidence, not physical UI validation.

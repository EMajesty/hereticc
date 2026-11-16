# M0.3b integration experiments

## Status and rules

These are separately authorized device experiments. M0.3b-A is implemented as a zero-hook probe and inspectable package, awaiting source/package review and physical validation; it has not run on hardware. B–E remain unimplemented and unauthorized. Firmware 1.3.0.12 and SDK `e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39` remain guarded research inputs, not generally supported targets. See [SDK hook safety](SDK-HOOK-SAFETY.md) and [device testing](DEVICE-TESTING.md).

Common prerequisites: owner authorization for that experiment; exact source/build hashes; spare backed-up SD card; saved projects/samples; stable power; stock boot verified without the card; no unique identifiers in logs; bounded run time; and the recovery rules in `SAFETY.md`. No experiment flashes firmware, modifies partitions, saves a project, or installs persistent device state.

## A. Firmware and SDK compatibility

**Implementation status:** source, host tests, AArch64 build, ELF inspection,
and deterministic SD package complete. M0.3b-A1 established the likely SD path
and writable-mount checks but could not establish that the stock-app-owned
autorun shell survives `ac50` restart. Report retrieval and physical validation
remain blocked pending a revised, separately reviewed lifecycle design.

**Purpose:** prove fail-closed recognition before any feature hook is trusted.

**Prerequisites:** reviewed HERETICC-specific zero-hook probe; table-derived 63-word fingerprint set; no ALSA interposition, generated executable pages, feature hooks, or paint-vtable patch; test on the exact reported firmware only. Any later hook test requires its own authorization and hardened installation state machine.

**Procedure and observations:** blocked before physical execution. A future
revision must first provide a bounded same-card retrieval process whose lifetime
is independent of the restarted `ac50` cgroup, without a persistent service,
network access, remount, or fallback write. Do not infer hook atomicity from the
zero-hook probe. Process exit/service restart, not live detach, is the module
lifecycle boundary.

**Failure conditions:** any executable-memory write, partial installed hook,
stock restart/boot loop, mismatch accepted, stock behavior changed after
rejection, absent/incomplete/oversized report, non-writable SD, attempted write
outside a dedicated result directory, or reliance on an autorun shell killed by
the service restart.

**Recovery:** terminate the transient process if possible, reboot without the SD package, verify stock input/audio/UI, stop the milestone on any incomplete restoration.

**Pass:** the exact image is recognized; every altered/unsupported image is rejected; the probe performs no executable-memory or vtable write; rejection and SD removal leave stock behavior unchanged. Passing A authorizes no feature hook.

## B. Input interception and forwarding

**Purpose:** validate page-scoped ownership and paired release behavior without triggering synthetic notes.

**Prerequisites:** A passed; one diagnostic mode limited to the verified Sample page; fixed-size counters only; button/pad light overrides disabled initially.

**Procedure and observations:** for pads, DATA turn/push, K1–K3, fader, B1–B3, SHIFT, ERASE, transport, and mode buttons, observe then forward each press/release/value. Consume one explicitly displayed test action at a time. Test held controls while leaving the page, SHIFT-before/after another key, duplicate surface/pad paths, encoder bursts, and reentrant/overlapping callbacks. Compare stock action counts before/after.

**Failure conditions:** duplicate or missing stock action, stuck note/button, consumed release without its press, stale callback object, unintended page/transport change, or an input remaining owned after exit.

**Recovery:** clear ownership ledger, forward/complete any native release explicitly proven pending, restore lights, request stock repaint, exit and reboot stock if any state remains uncertain.

**Pass:** every unowned input reaches stock exactly once; owned pairs never leak; exit/page change restores forwarding; all observed raw codes and duplicate paths are documented. This does not approve an activation shortcut.

## C. Full-screen rendering and restoration

**Purpose:** determine whether the Sample-page post-paint hook can support a reliable HERETICC screen.

**Prerequisites:** A and B passed; static bounded frame using only color, rectangles, and stock-font text; pad lights initially untouched.

**Procedure and observations:** draw a full 320×240 diagnostic frame after stock paint; exercise stock dialogs, toasts, page changes, sample loading, and idle repaint. Measure paint-call duration, repaint interval, dropped/coalesced requests, visible flicker/overdraw, and audio stability at several bounded primitive counts. Then exit through every normal/error path, restore pad lights in a separate subtest, and request stock repaint.

**Failure conditions:** stock elements unpredictably overdraw the frame, stale HERETICC pixels after exit, unreadable font/glyph behavior, audio disruption, unbounded paint time, stale component access, or pad lights not restored to the newest stock state.

**Recovery:** stop repaint pump/ownership, clear overrides, request stock paint, exit transient module, reboot without card if restoration is incomplete.

**Pass:** one explicitly named page scope is visually stable at a measured sustainable refresh/primitive budget, dialogs have a defined policy, and every exit restores stock screen and pad colors. Passing does not establish global theming or every page.

## D. Stock voice trigger and release

**Purpose:** distinguish callable pad audition from token-addressable polyphonic scheduling.

**Prerequisites:** A and B passed; one backed-up disposable project with one populated pad; transport stopped; no project save; valid live objects obtained only from the current callback; audio observer records bounded atomic counters/peaks only.

**Procedure and observations:** establish a forwarded physical baseline. In a separate run, consume that pad event and call SDK `send_note_on`/matching `send_note_off` inside the same still-live physical press/release callbacks; retain no object afterward. Compare audible/output onset and release. In separately reviewed phases, issue two calls from one live press callback and pair the allowed releases, test forwarded physical plus direct-call overlap, mono/poly and one-shot/gated stock settings, project/page exit while held, then stock transport playback. Record whether releases address individual calls or aggregate pad/source state. Do not invent sample/slice pointers or source IDs.

**Failure conditions:** restart/crash, stuck voice, wrong pad, unmatched release, project mutation, stale object use, stock transport corruption, or cleanup uncertainty.

**Recovery:** send only releases whose live ownership is proven, clear the adapter ledger, stop transport through normal controls, exit, reboot without card, and verify the project remains unchanged.

**Pass levels:**

- D1: one synthetic pad-path press/release is repeatable and restores cleanly.
- D2: overlapping identical sounds have independently matchable lifecycles suitable for `VoiceToken` mapping.
- D3: logical sample/slice selection can be resolved without mutating shared pad state.

D1 alone is insufficient for HERETICC's sequencer; failure of D2 or D3 triggers an architecture decision rather than an undocumented workaround.

## E. Clock correlation and scheduling jitter

**Purpose:** determine whether stock triggering can meet a measurable frame-timing contract.

**Prerequisites:** D1 passed; output callback with preallocated fixed-size observation ring; callback-local cumulative frame counter; host-side offline analysis; agreed provisional jitter/latency threshold before running.

**Procedure and observations:** mark each trigger request with a sequence number and monotonic timestamp, observe output blocks and onset offline, and compute call-to-block/onset latency. Repeat at idle, sustained UI repaint, encoder/pad load, sample loading where safe, transport stopped/playing, and increasing bounded lookahead. Test adjacent scheduling windows without sleeping in the scheduler; only the device harness waits for callbacks.

**Failure conditions:** observation overrun, audio underrun, callback blocking/allocation, ambiguous onset, discontinuous frame counter without a reported generation change, unstable/unbounded latency, or transport disruption.

**Recovery:** disable observation/trigger requests atomically, drain/stop owned voices, restore UI/input/lights, exit and verify stock audio.

**Pass:** an authoritative frame epoch or a stable measured conversion exists, discontinuities are detectable, and worst-case jitter stays within the owner-approved threshold under the declared loads. A millisecond UI clock or consistent average alone does not pass.

## Decision sequence

A must pass before B–E. B and C may proceed without D. D1 gates E; D2 and D3 gate a stock-engine HERETICC voice adapter. If C fails, retain stock UI or request a different UI architecture. If D/E fail, keep the host sequencer and request an explicit choice between deeper reverse engineering, a replacement engine, or stopping device integration. No failure silently authorizes custom DSP or stock-app replacement.

# SDK hook safety investigation

## Status and evidence

M0.3b-A0 is a host-only source investigation. No SDK fork, dependency, device
binary, cross-build, package, or firmware operation was created. M0.3b-A remains
blocked pending review and a separately authorized implementation.

The inspected upstream is `ioma8/mpc-sample-sdk` v0.6.0 at exact commit
[`e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39`](https://github.com/ioma8/mpc-sample-sdk/tree/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39),
MIT licensed. Claims below refer to that revision's actual implementation:

- [`loader.rs`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/loader.rs)
  locates the image, verifies fingerprints, installs sites, and publishes the
  module;
- [`sites.rs`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/sites.rs)
  defines the 22 branch hooks, callback forwarding, feature state, and a
  separate Sample-page paint-vtable patch;
- [`hook.rs`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/hook.rs)
  maps relay/trampoline pages and prepares, seals, commits, and rolls back
  branch hooks;
- [`firmware.rs`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/firmware.rs)
  contains the firmware offsets and expected words;
- [`image.rs`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/image.rs)
  finds the main image and reads/writes through `/proc/self/mem`.

This is source evidence, not emulator or physical-device validation. Upstream
documentation says 59 fingerprint words in places, but this revision's table
iterates 22 hook words, 20 guard words, and 21 code-call words: **63 words**.
The one call-table data entry has no instruction word. Any hardening work must
derive and test the count from the table rather than copy the documentation.

## Installation sequence

The supported `export_mod!` macro places a constructor in `.init_array`. Its
module value is static and intended to live until process termination. The
constructor ignores the `Result` from `loader::install`.

The complete path is:

1. `Firmware::find` walks loaded ELF program headers and identifies the unnamed
   main image whose load segment covers the SDK probe offset. This reads loader
   metadata; it does not authenticate the image.
2. `image::set_base` publishes the candidate base in an atomic. Notably, this
   occurs **before** fingerprint verification and is not cleared on rejection.
   It does not itself patch stock code, but makes the candidate visible through
   SDK APIs.
3. `firmware_ok` reads all 63 expected instruction words through
   `/proc/self/mem`. Any unreadable or unequal word rejects the image before
   hook allocation or stock-code modification.
4. `audio::keep_exports` keeps the SDK's exported ALSA interposition symbols
   linked. It does not start a worker or install a branch hook. Because the
   library is loaded with `LD_PRELOAD`, symbol interposition is a separate
   behavior from the 22-site hook transaction and must be audited in the future
   probe build even when `AudioHooks` is empty.
5. The first `hook::prepare` initializes the process-global `HookEngine`. It
   attempts two private anonymous 4 KiB mappings near the application using
   `MAP_FIXED_NOREPLACE`: one relay page and one trampoline page, initially
   read/write and non-executable. Failure leaves a permanently disabled engine
   in its `OnceLock` for that process.
6. `sites::install` calls `prepare` for all 22 sites. Each preparation reads up
   to four prologue words, rejects a PC-relative first word, chooses the safely
   displaced prefix, checks both branch ranges, and writes a relay plus
   trampoline into the new non-executable pages. It records the original first
   instruction and proposed branch. It does not alter the stock site.
7. Failed preparations are silently represented as `None`. Installation stops
   only if **all** 22 preparations fail. One successful site is sufficient to
   continue.
8. `sites::ORIGINALS` is populated with trampoline pointers for successful
   sites and `None` for missing sites. This publishes callback forwarding state
   before branches, but no stock call can reach it yet through these hooks.
9. `HookEngine::seal` changes both generated-code pages from read/write to
   read/execute. If either `mprotect` fails, both pages are unmapped and no
   branch is published. Generated relay/trampoline ranges receive explicit
   AArch64 data/instruction cache maintenance using an assumed 64-byte line.
10. `HookEngine::commit` visits prepared sites in table order. For each site,
    `patch_site` changes the containing stock page to read/write/execute, writes
    one AArch64 branch word through `/proc/self/mem`, reads it back, requests
    restoration to read/execute, and flushes that instruction range.
11. On a patch failure, commit tries to restore the possibly landed current
    word and all earlier words in reverse order. It unmaps generated pages only
    if every restoration reports success. If restoration fails, generated pages
    stay mapped and `touched` is set because a live branch may remain.
12. `sites::install` publishes note readiness and records Extract, control,
    sample-service, and lighting capability state even when commit failed but a
    rollback also failed. Handlers then have forwarding trampolines available,
    while the user module is deliberately not published.
13. Independently of commit success, `install_paint` attempts to replace the
    Sample-page vtable paint pointer. It validates only that the vtable address
    is mapped and the slot is nonzero, changes the vtable page to read/write,
    writes the handler pointer directly, then requests restoration to read-only.
    This operation is not in `HookEngine`, is not rolled back, and its result is
    ignored.
14. `sites::install` returns the branch-commit result. A false result becomes
    `InstallError::NoHooks`, even though rollback failure and/or the paint-vtable
    path may already have changed the process.
15. On branch-commit success, the loader copies audio and independent input
    callbacks into `OnceLock`s, invokes `Mod::on_start`, then publishes the mod
    behind a process-global mutex. Between branch publication and these steps,
    callbacks fall back to empty input/audio hooks and `Flow::Forward` because
    no module is visible yet.

The loader starts no background work. Feature modules can start workers when
called, but a default inert `Mod` does not call them. UI callbacks use a
non-blocking `try_lock` and a thread-local reentry guard; contention or reentry
forwards stock behavior. Audio and independent-input function pointers bypass
the module mutex after publication.

## First observable change and failure points

Mapping and filling new private pages does not change stock dispatch. Sealing
those pages makes newly generated bytes executable but still unreachable. The
first stock-image mutation is the first call to `mprotect` a stock text page as
read/write/execute. The first stock control-flow change is the subsequent write
of the first branch, `SIXTEEN_CTOR` in the current order.

After the first text-page permission change, these operations can still fail or
remain unverified:

- the first or any later `/proc/self/mem` branch write or read-back;
- restoration of a text page to read/execute (the return value is ignored);
- rollback writes, read-backs, and their permission changes;
- instruction-cache maintenance assumptions on the physical CPU;
- any later site's patch after earlier sites are already live;
- the separate paint-vtable page permission change or write;
- restoration of the vtable page to read-only (also unchecked);
- module `audio`, `input`, or `on_start` code, including a panic crossing the
  constructor boundary; and
- publication into `OnceLock` (the SDK ignores an already-set result).

The text branch write is reversible in principle because the original first
word is retained. It is not an atomic multi-site operation: other threads must
not execute sites during installation, and rollback itself consists of more
fallible live writes. The SDK relies on `.init_array` running before application
threads, an assumption not established by host tests or physical evidence.

## Atomicity answers

1. **Validate all sites first?** Yes in a hardened design. The current
   fingerprint pass validates every listed first word, and preparation reads the
   four-word prologues and branch constraints before stock modification. The
   current caller nevertheless accepts missing preparations instead of requiring
   the requested set. The paint vtable needs its own preflight: mapped slot,
   exact expected pointer/value, page classification, and permission plan.
2. **Prepare every trampoline first?** Yes. The existing engine already maps and
   writes all successful relays/trampolines before sealing or patching. A
   hardened caller must reject any missing required site and abort before seal.
3. **Can failure occur after the first executable-memory write?** Yes. Generated
   code is written before it becomes executable; every stock branch is then a
   separate fallible write/read-back. Later patches and rollback can fail.
4. **Can originals be restored safely?** The original word is preserved and the
   engine attempts restoration. Safety is conditional on startup quiescence,
   successful page-permission changes/writes/cache maintenance, and no thread
   executing a patched prologue. The implementation cannot guarantee these
   conditions during live removal.
5. **Instruction-cache flush?** The AArch64/Linux build executes `dc civac`,
   `dsb ish`, `ic ivau`, `dsb ish`, and `isb` for generated code and patched
   words. It hard-codes a 64-byte cache-line stride. The sequence is
   architecture-aware, but the line-size assumption and physical behavior are
   unverified.
6. **Permissions restored?** Generated pages become read/execute. Each stock
   text patch requests read/execute restoration, and the vtable path requests
   read-only restoration. Both restoration return values are ignored, so the
   source cannot prove restoration succeeded. Text pages are temporarily RWX,
   not W^X.
7. **Safe live removal?** No. There is no removal API, quiescence protocol,
   callback generation barrier, or way to prove no thread is in a handler or
   trampoline. `abort` intentionally does nothing after publication. Unloading
   the shared library would leave branches and callback pointers targeting
   unloaded code. Process-lifetime installation followed by a controlled
   `ac50` service restart is the supported recovery model.
8. **Stale pointers or invalid trampolines after partial install?** Successful
   partial preparation produces matching optional originals, but it can violate
   feature-level dependencies. Rollback failure deliberately retains generated
   pages and forwarding state, avoiding an unmapped trampoline, but leaves an
   indeterminate live subset. The paint-vtable pointer has no equivalent
   retention/rollback bookkeeping. A later `dlclose` is unsafe in every live
   case.

Therefore true atomicity across independent instruction writes is unavailable.
A fail-closed installer can make failure before publication deterministic and
can reduce the post-publication window, but it cannot promise recovery from
arbitrary `mprotect`, write, cache, or rollback failure. Any such failure must
keep code mapped, avoid publishing feature callbacks, terminate/restart the
stock service, and require a stock-only reboot before further testing.

## Hook inventory

Offsets are relative to the loaded firmware image and come from `firmware.rs`.
“Omittable” means only that the feature group must remain disabled and no SDK
API may rely on its captured state; it is not physical proof that a selective
build is safe. None is necessary for experiment A's compatibility probe.
The constants are listed at
[`firmware.rs` lines 77–102](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/firmware.rs#L77-L102),
their handler mapping at
[`sites.rs` lines 186–281](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/sites.rs#L186-L281),
and the handler bodies at
[`sites.rs` lines 288–773](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/sites.rs#L288-L773)
plus the two Extract handlers in
[`extract.rs`](https://github.com/ioma8/mpc-sample-sdk/blob/e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39/src/platform/extract.rs#L232-L295).

| # | Site / offset | Handler purpose | Dependencies and required state | A | Selective-install assessment |
|---:|---|---|---|---:|---|
| 1 | `SIXTEEN_CTOR` `0x01545a50` | Pass-through 16 Levels constructor | Forward trampoline | No | Omit with all 16 Levels features |
| 2 | `SIXTEEN_PROC` `0x021472f0` | Pass-through 16 Levels note handler | Forward trampoline | No | Omit with all 16 Levels features |
| 3 | `DATA_HOOK` `0x0140dbc0` | DATA encoder dispatch | `MODULE` or forwarding trampoline | No | Omit when encoder interception is disabled |
| 4 | `NOTE_ON` `0x01e27360` | Pad-path press dispatch and forward completion | Pair with `NOTE_OFF`; `INPUT`/`MODULE`; original input/property lifetimes | No | Install only as a paired pad group |
| 5 | `SURFACE` `0x023aff60` | Raw surface/pad event dispatch | `INPUT`/`MODULE`; readable payload | No | Omit when raw surface interception is disabled |
| 6 | `NOTE_OFF` `0x01e270d0` | Pad-path release dispatch | Pair with `NOTE_ON`; `INPUT`/`MODULE` | No | Install only as a paired pad group |
| 7 | `CC` `0x0159f230` | K knobs/fader CC dispatch | `MODULE`; firmware event layout | No | Omit when CC interception is disabled |
| 8 | `PITCH` `0x027e4400` | 16 Levels pitch adjustment; displaced setter supplied to `NoteEvent` | `MODULE`; coupled to MIDI-note replay/16 Levels features | No | Keep with features that use its displaced setter |
| 9 | `BUTTONS` `0x0159f2f0` | Button dispatch and SHIFT/ERASE held-state tracking | `MODULE`; event/context layout | No | Omit when button interception is disabled |
| 10 | `REPAINT` `0x01149880` | Pass-through repaint entry captured for drawing support | Forward trampoline; drawing/repaint subsystem | No | Omit with custom drawing |
| 11 | `STRING_FROM_C` `0x00f535c0` | Label substitution; also captures constructor used by draw/Extract | `MODULE`; reentry guard; draw/Extract callers | No | Keep with label replacement or SDK features calling this constructor |
| 12 | `EXTRACT_CTOR` `0x0143ed40` | Captures/dispatches Extract command construction | Pair with `EXTRACT_EXEC`; Extract state | No | Install only with reviewed Extract group |
| 13 | `EXTRACT_EXEC` `0x0143f0b0` | Captures/dispatches Extract execution | Pair with `EXTRACT_CTOR`; Extract state | No | Install only with reviewed Extract group |
| 14 | `WRITER` `0x01d8aff0` | Writer block substitution | Writer substitution state; may support generated output paths | No | Omit unless writer substitution is enabled |
| 15 | `NOTE` `0x0211b090` | MIDI-note event dispatch/replay | `INPUT`/`MODULE`; uses `PITCH` original when present | No | Omit when MIDI interception/replay is disabled |
| 16 | `CONTROL` `0x01968020` | Observes live control/surface and forwards setter | Control module and forward trampoline | No | Omit when native control calls are disabled |
| 17 | `SAMPLE_SERVICE_CTOR` `0x01df3330` | Records Temporary Samples service after constructor | Must pair with both service destructors | No | Install only as complete three-hook lifecycle group |
| 18 | `LIGHTING` `0x0159f910` | Observes native pad light, applies temporary override, forwards | Must pair with both lighting destructors for retained sender safety | No | Install only as complete three-hook lifecycle group |
| 19 | `SAMPLE_SERVICE_DTOR` `0x01df6d40` | Invalidates service before base destructor | Service constructor and deleting destructor | No | Part of complete service lifecycle group |
| 20 | `SAMPLE_SERVICE_DELETE` `0x01df6ed0` | Invalidates service before deleting destructor | Service constructor and base destructor | No | Part of complete service lifecycle group |
| 21 | `LIGHTING_DTOR` `0x01439e60` | Invalidates lighting sender before base destructor | Lighting callback and deleting destructor | No | Part of complete lighting lifecycle group |
| 22 | `LIGHTING_DELETE` `0x01439fd0` | Invalidates lighting sender before deleting destructor | Lighting callback and base destructor | No | Part of complete lighting lifecycle group |

There is also an unnumbered direct patch: `install_paint` changes vtable data at
`SAMPLE_PAGE_VTABLE + 27 * sizeof(usize)` to `page_paint`. It depends on the
module, drawing/page state, an exact vtable layout, and lifetime-long library
mapping. It must be modeled as a separate UI hook group and must not run for
experiment A.

Some current handlers tolerate absent originals by forwarding nothing or using
`None`, but that is not proof that arbitrary subsets preserve stock behavior.
Selective groups need explicit dependency tests and should reject missing
members as a unit.

## Failure matrix

| Failure | Current outcome | Stock mutation possible? | Hardened outcome |
|---|---|---:|---|
| Candidate image missing | `NotFound` | No | Observable probe rejection |
| One fingerprint unreadable/wrong | `WrongFirmware`; candidate `BASE` remains published | No branch/vtable write | Publish base only after full verification |
| Relay/trampoline mapping failure | Disabled engine; every prepare fails | No | Structured preflight error; no retry in same process |
| One prologue unreadable/unsafe/out of range | Site omitted; remaining subset may commit | Yes | Reject entire required group before seal |
| One generated-page `mprotect` failure | Pages discarded | No branch | Structured seal error; verify both unmaps |
| Branch-page writable change failure | Commit rollback begins | Earlier branches may be live | Keep mappings; report exact failed site; restart service |
| Branch write/read-back failure | Attempts current and prior restoration | Yes | Same, but explicit indeterminate/restart-required state |
| Permission restoration failure | Ignored | Yes; page may remain RWX | Check result and enter restart-required state |
| Rollback write failure | `touched`; forwarding pages retained; loader reports `NoHooks` | Yes, subset live | Never claim inert failure; retain library and force process restart |
| Paint-vtable preflight/write/restore failure | Result ignored; no rollback | Possibly | Exclude from base transaction; explicit UI group transaction and result |
| `on_start` failure/panic | No error contract | Branches already live | Make activation non-panicking and return status before feature enablement |
| Repeated initialization | `OnceLock` state persists; later install results are not a clean retry | Existing state persists | Explicit `AlreadyInitialized`/`PreviousFailureRequiresRestart` |

## Lifecycle and recovery model

`MODULE`, `AUDIO`, `INPUT`, and the hook engine are `OnceLock` globals. The
exported mod value is static. There is no normal shutdown callback, callback
drain, hook removal, or supported unload. A mod may be temporarily unavailable
when `try_lock` fails or reentry is detected; hooks then forward rather than
wait. This is useful for callback responsiveness, not an unload barrier.

The upstream SD loader uses `LD_PRELOAD` in a transient systemd drop-in under
`/run` and restarts `ac50`. Removing the SD `autorun` package and rebooting
removes that transient setup according to upstream documentation. HERETICC has
not physically verified this recovery. For future experiments, do not call
`dlclose` or attempt live unpatching. Treat the extension and every published
handler as process-lifetime. Recovery is a controlled service restart where
safe, followed by stock-only reboot without the package if any installation
phase is indeterminate.

## Hardening options

### A. Minimal compatibility probe — recommended first

Create a small HERETICC-specific fork/API mode that performs only:

1. main-image discovery;
2. complete table-derived fingerprint verification;
3. read-only validation of the 22 site prologues and the paint-vtable expected
   value/layout, producing a bounded structured report; and
4. explicit success/failure reporting with no `keep_exports`, generated-code
   mappings, `mprotect`, executable writes, vtable writes, or callback
   publication.

The minimal safe hook configuration is therefore **zero branch hooks and no
paint-vtable patch**. The probe should avoid exporting ALSA interposition symbols
or otherwise prove they cannot alter behavior. It should publish the firmware
base only after verification. This answers compatibility and dry-run questions
without claiming hook execution safety. It cannot validate actual page
permissions, cache maintenance, branch execution, callback ABI, or rollback on
hardware.

### B. Selective hook groups — later experiments

Represent named, closed groups rather than arbitrary bitsets: pad press/release;
raw surface; encoder; buttons; CC; MIDI/16 Levels; drawing/paint; Extract;
writer; control; sample-service lifecycle; lighting lifecycle. Each group owns
its prerequisites, exact expected sites, state publication, and activation
result. Prepare all members, reject the group if any member fails, and never
install the direct paint patch implicitly. This reduces exposure but requires
careful cross-group review and physical ABI tests.

### C. Hardened full installation

For a future full SDK mode:

- expose structured per-phase/per-site errors instead of `Option`/boolean;
- require all requested preparations before seal;
- verify expected first words again immediately before each patch;
- check every permission restoration and unmap result;
- separate `Prepared`, `Sealed`, `Publishing`, `Active`, and
  `IndeterminateRestartRequired` states;
- publish feature capability state only after complete commit;
- make the paint-vtable patch explicit, preflighted, and separately reversible;
- never report an inert failure if `touched` or a vtable write may be live;
- preserve mappings and library lifetime after any uncertain rollback;
- make repeated initialization return a precise state error; and
- inject memory, mapping, protection, cache, and patch backends in host tests.

Host tests should cover wrong fingerprints, missing required sites, allocation
and seal failures, partial preparation, each patch index failing, rollback at
each index, permission-restoration failure, paint-patch failure, and repeated
initialization. A model must assert both the externally visible state and which
pages/mappings remain owned. These tests belong in the future fork, where they
exercise the real state machine; duplicating a simplified model in HERETICC now
would provide weak evidence and was intentionally avoided.

Option C improves determinism but cannot make 22 independent live instruction
writes truly atomic. The fail-closed boundary is: no stock write until every
requested site is prepared and sealed; after the first stock write, any
unrecoverable uncertainty requires process restart.

## Recommendation and review gate

Use **Option A** for the first separately authorized physical experiment. It has
zero feature hooks, directly tests exact-image recognition and read-only SDK
assumptions, and avoids pretending rollback has been proven. Do not use the
upstream full installer for M0.3b-A.

Before device-facing implementation, the owner must approve:

- a HERETICC-specific SDK fork pinned to this upstream commit and how fork
  patches/rebases will be reviewed and licensed;
- the exact bounded diagnostic output and transient destination;
- whether merely loading an `LD_PRELOAD` library with no interposed audio
  symbols is acceptable for A;
- the 63-word table-derived fingerprint policy and read-only paint-vtable
  preflight;
- process/service restart and stock-only reboot as the recovery boundary;
- explicit physical pass/fail criteria for A0's assumptions; and
- a later, separate authorization before Option B or C installs any hook.

Passing the compatibility probe will not approve input, UI, note, audio, MIDI,
or timing hooks and will not establish physical safety of hook installation.

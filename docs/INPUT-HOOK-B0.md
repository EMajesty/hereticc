# M0.3b-B0.2 selective input-hook diagnostics

Status: source and host validation implemented; no physical execution or SD
package is authorized. Upstream is `mpc-sample-sdk` v0.6.0 at commit
`e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39` (MIT).

## Selection

`DATA_HOOK` at the SDK-owned symbolic site is the smallest useful experiment.
The SDK declares its original and handler ABI as
`unsafe extern "C" fn(usize, i32)` with a unit return. It is one independent
branch site. A transparent handler can forward the surface pointer and encoder
delta unchanged without decoding firmware-owned memory.

Rejected candidates:

- `NOTE_ON` and `NOTE_OFF` form an inseparable press/release lifecycle and carry
  five and three arguments respectively. Testing only one risks broken voices.
- `CC` carries a firmware event pointer, raw control number and two floating
  values. The existing SDK handler decodes firmware-owned event state.
- `BUTTONS` returns `i32`, reads firmware event layout, and updates shared
  SHIFT/ERASE state in the full SDK path.
- `SURFACE` receives a raw payload pointer that the full handler dereferences to
  classify messages. It is broader than an encoder-only dispatch.

This conclusion is source-grounded, not hardware-validated. The meaning and
thread context of DATA arguments remain reverse-engineered assumptions.

## Selective lifecycle

The B0 feature compiles the read-only A4 probe, hook engine, and a new SDK patch
module exposing only `prepare_data`; it does not compile full `sites`, loader,
audio, MIDI, drawing, platform, or paint code. NEUROGENIC cannot request an
arbitrary firmware offset through this API.

1. Classify `/proc/self/exe`; only exact `/usr/bin/MPC Sample` continues.
2. Discover the image and require `Compatible`, exactly 63 checked words, and a
   63-word expected table. The verified base is published by the existing probe.
3. Allocate relay/trampoline pages and prepare only DATA. Preparation reads the
   prologue and writes private non-executable pages; stock text is untouched.
4. Publish the original trampoline pointer with release ordering.
5. Seal/flush relay and trampoline pages, then publish. Publish is the first
   phase that changes stock behavior: it changes text-page permissions and
   attempts the one branch-word write.
6. When the original branch word is read back, clear the trampoline pointer. If rollback is not proven,
   retain it and report `CommitFailedIndeterminate` so any live branch still has
   a valid forwarding target for process lifetime.

The handler loads one atomic pointer, increments fixed saturating atomic counters, and calls
the original once with unchanged arguments. It allocates no memory, performs no
I/O, takes no lock, and invokes no sequencer code. DATA's return is unit, so
direct trampoline invocation preserves the return contract. Successful
installation does not prove that forwarding was ever invoked.

## Failure and atomicity limits

All required sites—one in B0—are prepared before text mutation. A missing site,
mapping failure, relocation refusal, or sealing failure occurs before the first
stock-text write. Commit can still fail after a write or read-back failure. The
SDK attempts restoration and keeps trampoline pages mapped if restoration is
not proven, but this is not atomic rollback. B0 reports that state as
indeterminate and never calls it success.

### Hook-engine state transitions

- `new`: maps separate RW relay and trampoline pages near the verified image.
  A second-map failure unmaps the first; stock text is untouched.
- `prepare`: reads up to four prologue words, rejects unreadable/PC-relative or
  out-of-range sites, writes only the private RW pages, and records the original
  first word. Failure does not publish a branch.
- `seal`: changes both owned pages to RX and flushes their data/instruction
  caches. Any `mprotect` failure discards both pages before publication.
- `publish`: for each pending site, temporarily requests RWX, writes one branch
  word through `/proc/self/mem`, reads it back, attempts RX restoration, and
  flushes the site cache. This is the first stock behavior change.
- publish failure: the current site is considered landed whenever it cannot be
  read back as the original. The engine restores it and every earlier site in
  reverse order. Only a write plus original-word readback counts as restoration.
- restored failure: pages are unmapped and `touched()` stays false. Under the
  engine's one-installer/no-external-writer assumptions, this establishes that
  no SDK branch word remains live. It does **not** establish RX restoration,
  because the final `mprotect` result is ignored.
- indeterminate failure: `used` becomes true, pages remain mapped, and
  `touched()` is true. B0.1 retains the original pointer and classifies failure
  as indeterminate.
- success: pending state is cleared, `used` is true, and pages remain for
  process lifetime. `abort` is inert after `used`; there is no live unpatch.

Cache maintenance on AArch64 performs data-cache clean, barriers,
instruction-cache invalidation, and `isb`. This is appropriate architecture
machinery but has not been physically validated during concurrent execution.

### Post-interaction retrieval investigation

The constructor publishes an initial bounded report to `/run` and two isolated
`m03b-b01-init-00..01.txt` SD slots using the A4-validated mount selection,
descriptor-relative no-follow operations, exclusive partial creation, synced
rename-without-replace, and directory sync. Reserved
`m03b-b01-events-00..01.txt` names are host-tested as a separate namespace but
are not written by device code.

The evaluated mechanisms are:

| Mechanism | Assessment |
|---|---|
| `/proc/<pid>/mem` or `ptrace` | Requires unverified privileges and kernel policy, exposes address discovery, may pause or race the process, and is rejected. |
| Socket, signal, or controlled request | Requires a resident listener/thread or unsafe signal work and a protocol attack surface; rejected for B0.2. |
| Destructor/shutdown snapshot | No verified stock shutdown callback or unload ordering; it could race the live branch and trampoline; rejected. |
| Existing SDK lifecycle | The reviewed SDK provides initialization callbacks but no safe post-interaction snapshot point; unavailable. |
| Shared `/run` mapping | Does not attach to or pause the process, adds no hook or callback I/O, and permits bounded atomic reads; selected for host implementation. |

For an exact, compatible process, the constructor exclusively creates a
process-specific, mode-0600, fixed shared counter file under `/run`. B1-P0
versions it to 72 bytes and includes a process-start token to reject PID reuse.
The callback updates only its five atomics. Failure to create it leaves
the existing local counters active and does not affect hook gating. The mapping
is never unmapped while the branch may live.

The opt-in reader performs two identical fixed-size reads, retrying at most
eight times, validates the magic/version/size and aggregate invariants, and
then requires `/proc/<pid>/exe` to resolve exactly to `/usr/bin/MPC Sample`.
Missing, denied, malformed, unstable, wrong-process, and disappeared-process
conditions are explicit errors. Output is at most 384 bytes and includes only
the PID and aggregate counters. It does not expose an address or event payload.

This is not yet a device retrieval path. It is unknown whether the device has a
safe way to launch an opt-in helper after interaction, under a UID able to read
the mode-0600 file, without restarting `ac50`. A restart destroys the process
whose counters are needed. The helper is therefore neither cross-packaged nor
added to autorun, and no SD export is implemented.

Remaining physical risks are material: temporary RWX text permissions,
instruction-cache coherence on the production kernel/CPU, another core entering
the four-byte site during publication, correctness of the inferred DATA ABI,
constructor timing relative to stock threads, and process-lifetime mappings.
There is no safe live detach contract.

## Build and inspection boundary

The isolated source is under `device/m03b-b0`. It applies the already-reviewed
A4 probe patches and then one B0 patch. It emits an AArch64 shared object for
static inspection but no ZIP, autorun, or SD package.

Unlike A4, a branch-hook binary necessarily imports memory mapping/protection
and stock-text writing machinery. Therefore B0 cannot pass A4's
`mprotect/pwrite absent` rule. Its separate inspection profile requires zero
defined dynamic exports, no ALSA/interposition identifiers, no paint or other
input-hook identifiers, no writable-executable ELF sections, and exactly the
DATA experiment identity. A4 remains independently buildable under its stricter
zero-hook profile.

## Recommendation

B0.2 is ready for B1 planning, not execution. A minimal **not-authorized** B1
plan would first establish a read-only, non-restarting way to invoke the helper
with sufficient `/run` and `/proc` access; only then would a separately reviewed
package install the existing DATA experiment, allow bounded encoder interaction,
snapshot while the same stock PID remains live, and copy only the formatted
report through the proven SD policy. Concurrent text patching, ABI correctness,
cache synchronization, rollback, and helper privileges remain physical risks.

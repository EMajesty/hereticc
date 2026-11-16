# M0.3b-B0 selective input-hook feasibility

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
audio, MIDI, drawing, platform, or paint code. HERETICC cannot request an
arbitrary firmware offset through this API.

1. Classify `/proc/self/exe`; only exact `/usr/bin/MPC Sample` continues.
2. Discover the image and require `Compatible`, exactly 63 checked words, and a
   63-word expected table. The verified base is published by the existing probe.
3. Allocate relay/trampoline pages and prepare only DATA. Preparation reads the
   prologue and writes private non-executable pages; stock text is untouched.
4. Publish the original trampoline pointer with release ordering.
5. Seal/flush relay and trampoline pages, then call commit. Commit is the first
   phase that changes stock behavior: it changes text-page permissions and
   attempts the one branch-word write.
6. On clean failure, clear the trampoline pointer. If rollback is not proven,
   retain it and report `CommitFailedIndeterminate` so any live branch still has
   a valid forwarding target for process lifetime.

The handler loads one atomic pointer, increments one atomic counter, and calls
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

B0 establishes a reviewable one-site design and host-tested state machine, but
does not resolve concurrent text-patch safety. A B1 physical test is only
justified after owner review accepts that residual risk and separately approves
a retrieval/package design and controlled stock-service restart boundary. B0
itself must not be deployed.

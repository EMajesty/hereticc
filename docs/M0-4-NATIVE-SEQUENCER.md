# M0.4/M0.4a: first native stock-sequencer event

M0.4b subsequently hardened this adapter and prepared a separately authorized
one-command experiment. See [M0.4b safety review](M0-4B-NATIVE-SAFETY.md).

## Result through M0.4c

The owner-run M0.4c experiment on firmware 1.3.0.12 physically confirmed the
first NEUROGENIC-generated native event: the stock command accepted it, the
note was visible in Step Edit, and stock transport played it audibly. Automatic
stock restoration passed. The old raw-byte scanner did not observe the event,
so native removal was not attempted and remains unverified.

M0.4 establishes and host-tests the safe request side of one stock-sequencer
insertion. A selected active NEUROGENIC cell becomes a bounded, pointer-free
request for the stock UI's current sequence and current track. The request
preserves 960-PPQ timing, one-step duration, physical-pad identity, and fixed
velocity. A one-shot owner permits at most one adapter call and makes failures
terminal because a native command's application status could otherwise be
ambiguous.

At the original M0.4 host milestone no native event was inserted. The pinned SDK has no stock sequence API, and
the source-proven native boundary requires substantially more live context than
NEUROGENIC can currently acquire safely. M0.4 therefore does not add raw
firmware writes, hardcoded calls, new hooks, or a HIL command that would imply
unsupported functionality.

## Evidence reviewed

Pinned revisions and licenses:

- `mpc-sample-sdk` `9487cbba3648afb29d8e3107e31a7ac930985220`,
  MIT, remains the device dependency;
- Am MOD `6cdc67276f43cd39f64871cb7e36782481244c65`, MIT, was
  research evidence only in M0.4. M0.4a later adapts its narrowly reviewed
  native add/remove pattern with attribution and the full notice preserved.

The upstream heads were also checked on 2026-11-17 to avoid recreating a newer
solution. SDK v0.7.4 at `7d82a51279c4e9af4a571eb4479805caeb0f1f9c`
still exposes no sequence insertion API. Am MOD head
`119546dcd0078275393afc1155c4803f507cfcd6` has no changes to the reviewed
Steps native map, implementation, or ABI bridge since the pinned revision.
NEUROGENIC remains pinned to the already validated SDK v0.7.1 revision.

### SDK facts

SDK v0.7.1 exposes input forwarding, callback-local note replay, selection,
samples, drawing, and guarded firmware installation. It exposes no sequence,
event-list, stock event constructor, command manager, or Undo insertion API.
Its worker tasks are not a UI-thread native-command scheduler. Incoming MIDI
replay enters below stock sequence recording and cannot create a scheduled
event.

### Native facts from Am MOD source

For stock firmware 1.3.0.12, Am MOD's pinned implementation establishes:

- a native musical value is 56 bytes: signed 64-bit tick at offset 0, tag/flags
  word at offset 8, and 40 bytes of inline payload at offset 16;
- tag 3 represents the note variant used by its Steps implementation;
- the native constructor receives a resolved note, signed tick and duration,
  integer default value `100`, flags `0`, and normalized floating velocity;
- time and duration use 960 PPQ;
- the live Step controller supplies the current event list and command service;
- the pad-to-note mapping is resolved from live stock state rather than treating
  physical pad 0–15 as the event note;
- addition uses firmware-owned event multisets, native strings and shared
  results, then the stock add-command factory/command service so the edit can
  participate in Undo;
- all controller/model access is confined to its identified UI thread and
  revalidated; borrowed pointers are not retained after the operation.

Am MOD's ARM/QEMU verification executes the actual constructor, containers,
and command factories with GUI/executor fixture boundaries. Its own notes state
that this does not prove device audio, live Undo replay, persistence, or physical
input behavior. These facts justify an SDK extension design, not direct calls
from NEUROGENIC.

## Implemented boundary

`neurogenic_device::stock_sequence` contains:

- `StockNoteRequest::from_active_selection`, which requires the editor to be
  open and its selected step active, then copies only track-as-physical-pad,
  tick, duration, and velocity;
- `resolve`, which accepts a live adapter's separately resolved MIDI note and
  sequence length and produces typed constructor arguments without constructing
  or serializing the 56-byte native value;
- `StockSequenceEditPort`, the narrow future current-sequence/current-track
  submission boundary;
- `OneShotInsertion`, an at-most-once state machine with explicit ready,
  submitted, cancelled, and failed terminal states.

The port's receipt means only that its implementation accepted the request. It
is not evidence that the event is present or audible. At the end of M0.4 there
was intentionally no production implementation; M0.4a supplies the isolated,
feature-gated implementation described below.

## Validation levels

| Level | Current result |
|---|---|
| A. NEUROGENIC step to typed native-constructor arguments | Host-tested |
| B. Submission through a verified live stock editing boundary | Physically confirmed in M0.4c |
| C. Event observed in the stock sequence | Visible in stock Step Edit; automated identity awaits M0.4d hardware verification |
| D. Stock transport audibly plays the event | Owner-confirmed in M0.4c |

Tests cover active/inactive UI capture, all 16-step timing, pad/step/velocity
bounds, sequence-length bounds, normalized constructor fields, successful
at-most-once submission, adapter failure, repeat refusal, and cancellation.
No test creates native bytes or firmware pointers.

## Required SDK extension

The shortest safe next step is a separately reviewed, upstream-compatible SDK
API implemented for firmware 1.3.0.12 that:

1. runs synchronously on the proven owning UI callback/thread;
2. acquires and validates the current controller, sequence, event list, Program,
   sequence length, command service, and pad-to-note mapping;
3. owns native event construction, containers, strings, shared results, cleanup,
   and command dispatch internally;
4. accepts only copied typed fields and retains no caller or firmware pointer;
5. reports command acceptance separately from event-list verification;
6. supports an Undo/removal proof and fails closed on page/context changes.

This should be proposed to the SDK rather than maintained as a private set of
firmware offsets in NEUROGENIC. Only after host/emulator ABI tests and review
should one reversible physical insertion be authorized in a fresh unsaved test
sequence. That operation must use the existing exact firmware guard, one event,
normal stock transport, native Undo/removal, automatic evidence collection, and
stock restoration.

## Original M0.4 safety status

At the original host-only M0.4 milestone, the M0.3e UI and HIL reducer protocol
were unchanged. No device-changing native
operation was added, so the established HIL runner cannot honestly validate
insertion and was not extended. No MPC connection or restart was performed.

The first native insertion and audible stock playback are now physically
demonstrated by M0.4c. M0.4's original concrete blocker was an SDK-owned live sequence-command
API, not step timing or request construction. M0.4a implements that boundary
as described below; physical execution remains a separate approval gate.

## M0.4a implementation

M0.4a adds a firmware-1.3.0.12-only implementation of
`StockSequenceEditPort`. It is callback-scoped:
`NativeStockSequencePort::from_step_paint` accepts the live stock
`StepEditScreen` only for its owning UI paint callback. The port retains no
screen, controller, Program, event-list, or command-service pointer afterward.

The native shim validates exact Step-screen/controller vtables and visibility;
current-things, sequence, Program, event list, command service and bounded
sequence length; physical-pad range and native pad-to-note mapping; and the
tag-3 constructor's returned type/tick. It owns three native tree containers,
two native strings, the shared command result and cleanup. Addition uses the
pinned `0x19500e0` factory through `controller+0x240`; exact removal uses
`0x194fa30`. Both use the AArch64 hidden-result ABI bridge.

A pointer-free receipt contains the copied 56-byte event, resolved note and
sequence-length token. A bounded 4096-node lookup distinguishes command
acceptance from observation. M0.4d replaces the original raw 56-byte comparison
with the stock Step editor's native event comparator and requires exactly one
matching event.

The implementation is adapted from Am MOD under MIT; its notice is preserved
in `THIRD_PARTY_NOTICES.md` and source headers. It is isolated behind
`native-sequence-adapter`; normal M0.3e does not link or install it and remains
byte-identical to the physically validated baseline.

### UI-thread extension and first experiment

The distinct `neurogenic-device-native-hil` build installs one guarded Step
screen vtable callback. Installation requires the pinned original slot value,
changes one process-memory pointer, returns the page read-only, and attempts
rollback on publication failure. The callback invokes the stock painter first.
The existing HIL worker only changes atomics:

- `NATIVE ARM` arms exactly one A01, tick-0, velocity-100, 240-tick insertion;
- the next valid stock Step-screen paint performs the command;
- a later paint reports `observed` only after finding the exact event;
- `NATIVE REMOVE` requests exact removal on the next valid Step-screen paint;
- `NATIVE STATUS` is observation only.

No insertion occurs merely by loading the artifact, and no retry exists.
`accepted` is command-boundary evidence; `observed` is event-list evidence;
neither proves audible playback.

### Non-hardware evidence

`device/m04a/scripts/verify-native-abi.sh` cross-builds and executes an
AArch64/QEMU fixture covering constructor and hidden-result ABIs, owned
containers/strings, add dispatch, shared-result cleanup, exact remove,
presence lookup, bounds and invalid context. It does not run Akai firmware and
cannot prove live Undo, stock stability, persistence, audio or timing.

The approval-gated artifact is
`target/m04a/build/neurogenic-m04a-native-test.so`, SHA-256
`51e7be14232c72b19fa92a1b825cccd179de99dffb5243a5ffc2efeeec5cd446`.
It is AArch64, has a non-executable stack, no writable/executable load segment,
and requires no GLIBC symbol newer than 2.30.

### Current physical experiment gate

M0.4c executed one insertion and physically confirmed audible playback. The
remaining gate is M0.4d's unique native-comparator observation, canonical
removal, and verified absence on a fresh disposable sequence/track. The same
fail-safe one-command runner performs that bounded operation and restores stock.

Stop on any unexpected event, UI/control/audio regression, unavailable/failed
state, service instability or inability to remove. Do not re-arm in the same
run.

The M0.4a fixed host preparation/deployment commands were:

```sh
python3 tools/neurogenic-hil/hil.py --config target/hil/config.json build --native-m04a
python3 tools/neurogenic-hil/hil.py --config target/hil/config.json deploy --native-m04a --confirm MPC-SAMPLE-1.3.0.12
python3 tools/neurogenic-hil/hil.py --config target/hil/config.json restart --sha256 657ed92c2224af0f15947ccd637c56aa36f1717639074ecc6d59b2f76727b3d8 --confirm MPC-SAMPLE-1.3.0.12
```

They are retained as the historical M0.4a record and must not be used for the
first experiment. Use the reviewed M0.4b command in
[the safety review](M0-4B-NATIVE-SAFETY.md) only after explicit approval.

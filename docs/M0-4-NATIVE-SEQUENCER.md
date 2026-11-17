# M0.4: first native stock-sequencer event

## Result

M0.4 establishes and host-tests the safe request side of one stock-sequencer
insertion. A selected active NEUROGENIC cell becomes a bounded, pointer-free
request for the stock UI's current sequence and current track. The request
preserves 960-PPQ timing, one-step duration, physical-pad identity, and fixed
velocity. A one-shot owner permits at most one adapter call and makes failures
terminal because a native command's application status could otherwise be
ambiguous.

No native event was inserted. The pinned SDK has no stock sequence API, and
the source-proven native boundary requires substantially more live context than
NEUROGENIC can currently acquire safely. M0.4 therefore does not add raw
firmware writes, hardcoded calls, new hooks, or a HIL command that would imply
unsupported functionality.

## Evidence reviewed

Pinned revisions and licenses:

- `mpc-sample-sdk` `9487cbba3648afb29d8e3107e31a7ac930985220`,
  MIT, remains the device dependency;
- Am MOD `6cdc67276f43cd39f64871cb7e36782481244c65`, MIT,
  is research evidence only. No upstream code was copied.

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
is not evidence that the event is present or audible. There is intentionally no
production implementation until the SDK owns the native object, UI-thread,
command, and lifetime contracts.

## Validation levels

| Level | Current result |
|---|---|
| A. NEUROGENIC step to typed native-constructor arguments | Host-tested |
| B. Submission through a verified live stock editing boundary | Blocked: SDK API absent |
| C. Event observed in the stock sequence | Not attempted |
| D. Stock transport audibly plays the event | Not attempted |

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

## Safety and hardware status

The M0.3e UI and HIL reducer protocol are unchanged. No device-changing native
operation was added, so the established HIL runner cannot honestly validate
insertion and was not extended. No MPC connection or restart was performed.

The first native insertion and audible stock playback have **not** been
demonstrated. The concrete blocker is an SDK-owned live sequence-command API,
not step timing or request construction.

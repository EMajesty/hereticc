# M0.6b live playback-window replacement feasibility

## Decision

**No live replacement artifact is implemented.** The current evidence does not
establish a safe edit-ahead boundary while stock transport is running. M0.6a's
static materialization path remains the last verified native-editing boundary.

This is the required fail-closed result, not a claim that live replacement is
impossible. A transport tick can probably be observed on firmware 1.3.0.12, but
NEUROGENIC does not yet have a reviewed or physically validated way to acquire
that tick, relate it to the sequence being edited, or determine which future
events the playback engine has already queued.

## Evidence reviewed

### Physically confirmed

On firmware 1.3.0.12, M0.6a materialized eight events for A01/A02 while stopped,
uniquely verified them, played them through stock transport, removed them, and
restored stock. Playback was audible; timing and jitter were not measured.

This proves bounded static cache materialization. It does not prove that the
same event list may be changed safely while its sequence is playing.

### Pinned SDK 0.7.1

Revision `9487cbba3648afb29d8e3107e31a7ac930985220` exposes monotonic time,
audio callbacks, input callbacks, and stock drawing hooks. It exposes no typed
stock transport position, transport generation, current sequence playhead, or
playback-engine queue boundary. The verified Step Edit paint context supplies
the current sequence, track, Program, event list, and command service, but not
a playback position.

Wall-clock time, DATA cursor position, transport-button state, or paint cadence
cannot substitute for the missing transport clock.

### Pinned Am MOD reference

Revision `6cdc67276f43cd39f64871cb7e36782481244c65` contains a firmware-specific
transport observer:

- it hooks the native NowPosition timer entry at image offset `0x2109a90`;
- validates the timer object's vtable as image offset `0x6da4588`;
- borrows the engine at timer offset `0x258` only during that callback;
- calls the native engine-tick getter at `0x2113b10`;
- obtains the transport facade at engine offset `0x768`, then calls its
  `isPlaying` virtual method;
- publishes copied tick/running/serial values through atomics.

Its source says the engine clock uses 960 ticks per quarter and deliberately
avoids a queued UI property at timer offset `0x238`. Its host ARM fixtures cover
the getter path. These facts are **source-supported upstream evidence**, not a
NEUROGENIC hardware contract. The stock SDK has no equivalent public API, and
NEUROGENIC's existing hook transaction does not install this extra entry hook.

## Why position alone is insufficient

A correct replacement requires both of these facts at the same operation:

1. a fresh tick and running state for the exact sequence/context being edited;
2. a proven lower bound on how far ahead the stock playback engine has already
   expanded or queued native events.

The inspected sources establish neither the queue depth nor a native
"safe-to-edit-after" boundary. No measured command latency, UI-callback
latency, loop-wrap behavior, tempo-change behavior, or engine prefetch horizon
exists. Choosing one bar, two bars, or any fixed number of ticks would therefore
be a guess. Verifying a command before its musical tick also does not prove the
engine had not already cached the old event.

Sequence, track, Program, and event-list context identities can still guard
ownership. Canonical receipts can still prevent unrelated deletion. Neither
guard prevents a missed or duplicated sounding event once playback has queued
work.

## Undo and partial replacement

The current native adapter submits one stock command per event. M0.6a showed
that a small bounded batch can be inserted and cleaned up, but did not measure
Undo-stack capacity or coalescing. A live cache updater built on repeated stock
Undo commands could interfere with user history or grow it without bound.

The batch is deliberately non-atomic. During playback, removing an old
generation and inserting a new generation would expose an intermediate event
set. Inserting first avoids a gap but may create duplicates; removing first
avoids duplicates but may create silence. There is no verified native batch
transaction or generation swap to resolve this.

## Smallest justified next hardware experiment

The next experiment must be **read-only transport observation**, not live event
replacement:

1. add one isolated, firmware-1.3.0.12-only SDK-compatible observer for the
   pinned NowPosition site, installed in the same prepare/seal/commit phase as
   other instruction hooks;
2. publish only bounded copied values: callback count, tick, running flag,
   serial/generation, and monotonic observation timestamp;
3. verify stopped, start, continuous play, loop wrap, stop, continue, tempo
   change, sequence change, and stock restoration against a disposable sequence;
4. correlate observations with known sequence events and measure callback
   cadence; perform no native edits;
5. reject the observer if the exact prologue, timer vtable, engine, playing
   interface, or tick progression differs from the pinned evidence.

That test requires a separately reviewed SDK extension because hook preparation
must occur before SDK publication; installing another inline hook later from
`Mod::on_start` would violate the SDK's startup-only hook contract. No M0.6b HIL
command exists until this prerequisite is implemented and reviewed.

Even a successful clock test would not immediately authorize replacement. A
second bounded experiment must measure the playback engine's effective queue
horizon using disposable, uniquely owned future events, with objective output
capture or another non-subjective signal. Only then can the project choose a
conservative edit-ahead deadline.

## Go/no-go

- Static materialization: **physically confirmed** for the M0.6a batch.
- Transport-position source in current NEUROGENIC artifact: **unavailable**.
- Safe edit-ahead horizon: **unknown**.
- Live event replacement while transport runs: **no-go**.
- Full rolling windows, scenes, and song playback: **blocked on the two gates
  above**.

No firmware, HIL runner, native adapter, or device artifact changed in M0.6b.

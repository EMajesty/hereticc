# M0.6a static playback-window materialization

## Scope and status

M0.6a implements a host-tested compiler, fixed-capacity native batch coordinator,
and a guarded one-command physical experiment. The owner subsequently ran that
experiment on firmware 1.3.0.12: all eight A01/A02 events were materialized,
played audibly through stock transport, removed, and followed by successful
stock restoration. Static materialization is therefore **physically confirmed
for this bounded batch**. Timing and jitter were not objectively measured.

NEUROGENIC remains authoritative. Native tag-3 notes are disposable cache
entries in a fresh unsaved stock sequence. This milestone does not implement
rolling windows, scene transitions, MIDI output, or a replacement sampler.

## Playback-window compiler

`neurogenic-core::PlaybackWindowCompiler` wraps the existing stateful scheduler
and accepts an immutable validated `Project`, a contiguous half-open
`TickInterval`, and separate semantic/audio capacities. It transactionally emits
deterministically ordered `AudioMaterializationRequest` values containing:

- stable `(EventId, absolute MusicalTime)` occurrence identity;
- logical track and `SoundSource` target;
- exact integer start and duration;
- format-v1 default velocity 100.

It preserves off-grid ticks, overlaps, independent pattern wrapping, scheduler
ordering, and interval boundaries. Capacity failure does not advance its cursor.
It contains no firmware pointer, native bytes, or device dependency. A future
MIDI router can consume the same scheduled semantic timeline; MIDI output is not
implemented here.

The current format-v1 model has no persisted velocity, so the compiler labels
100 as an explicit temporary default. Format v2 must carry per-note velocity.

## Native batch coordinator

`NativeBatchCoordinator` holds at most 12 copied requests and receipts. The
M0.6a fixture uses eight. One Step Edit paint advances at most one boundary:

1. submit one stock add command;
2. observe that receipt until the stock comparator reports one unique event;
3. advance to the next request;
4. after explicit cleanup, remove verified receipts in reverse order;
5. observe exact absence before marking each receipt removed.

Insertion is deliberately not called atomic. Per-slot states distinguish
accepted, uniquely verified, removal accepted, and absent. On partial failure,
only uniquely verified receipts are eligible for cleanup. An accepted but
unverified event is reported as ambiguous and is never deleted speculatively.
Context change, comparator ambiguity, observation exhaustion, or native error
fails closed.

The existing firmware-1.3.0.12 guard, Step paint hook, UI-thread identity,
callback reentrancy gate, native comparator, canonical receipt, context identity,
and volatile HIL restoration remain unchanged. No background thread invokes a
native editing function.

## Compiled experiment window

The experimental library constructs an immutable two-track, two-bar core
snapshot and runs it through `PlaybackWindowCompiler`. It then translates the
result to the existing pointer-free `StockNoteRequest` boundary.

| Logical track / pad target | Tick | Duration | Purpose |
|---|---:|---:|---|
| track 1 / A01 | 0 | 480 | ordinary downbeat |
| track 2 / A02 | 240 | 240 | sixteenth position |
| track 1 / A01 | 960 | 240 | quarter position |
| track 2 / A02 | 973 | 240 | off-grid position |
| track 1 / A01 | 1920 | 480 | overlapping long note |
| track 2 / A02 | 2040 | 240 | overlap partner |
| track 1 / A01 | 2880 | 240 | quarter position |
| track 2 / A02 | 3857 | 240 | second off-grid position |

The stock sequence must be at least 4097 ticks; the operator procedure requires
a fresh two-bar 4/4 sequence (7680 ticks at 960 PPQ). Pad mapping is resolved
live through the current drum Program. Unique comparison rejects conflicts with
preexisting identical notes, which is why an empty disposable sequence is a
precondition.

## Guarded HIL experiment

The runner reuses strict SSH identity, host locking, immutable hash deployment,
one controlled activation, mailbox bounds, structured reports, and `finally`
stock restoration. It uses a distinct authorization token and never retries a
native insertion automatically.

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  static-batch-experiment \
  --confirm MPC-SAMPLE-1.3.0.12 \
  --confirm-batch STATIC-BATCH-TWO-BAR-1.3.0.12
```

The artifact is built at:

```text
target/m06a/build/neurogenic-m06a-native-batch.so
```

The runner asks the owner to create a fresh unsaved two-bar sequence, assign
sounds to A01/A02, stop transport, and open Step Edit. DATA movements provide
the already verified Step-paint handshake until all eight events are uniquely
observed. The owner then starts stock transport, records an explicit subjective
PASS/FAIL, stops transport, and uses DATA again while exact cleanup completes.
The report records native positions separately from subjective listening and
explicitly says timing accuracy was not measured.

On an insertion failure, the runner reports accepted/verified/ambiguous counts
and attempts removal only of the uniquely verified prefix. A final `dirty` state
means at least one accepted event could not be proven unique; the disposable
sequence must be discarded. Stock process restoration does not claim to undo an
in-memory sequence edit.

## Safety boundaries

- No saved stock project or internal persistent file is modified.
- Transport must remain stopped during insertion and removal.
- Native calls occur only in the verified Step UI callback.
- Each callback performs one bounded command or comparison traversal.
- The 180-second lease bounds the operator handshake; it does not make firmware
  calls interruptible.
- No rolling replacement, automatic re-arm, arbitrary command, network service,
  firmware flash, or new loader is introduced.
- The existing M0.5 artifact and stock restoration path remain separate.

## Host evidence

Tests cover deterministic multi-track/off-grid/overlap compilation, half-open
windows, stable occurrence identity, transactional capacity failure, full batch
insert/verify/remove, reverse exact cleanup, partial insertion failure,
accepted-but-ambiguous ownership, protocol bounds, mocked orchestration failure,
and stock restoration. The established AArch64/QEMU ABI fixture and strict ELF
inspection remain build prerequisites.

The reviewed host artifact is 670,912 bytes. Its SHA-256 is
`df8b7bcedca7ac116c8d9a96b4661e0961e61501f133e1d301e666b18cc6a980`;
the prelinked ELF SHA-256 is
`db6aba005fa2e900f457b02dcbd4cebb15cd2aa800078febdf0a44355656b271`.
These identify this host build only and do not constitute physical validation.

## Remaining gates before live replacement

- Physical confirmation that all eight commands and comparator scans complete
  reliably in one Step context.
- Subjective confirmation of both mapped pads, overlap, and off-grid ordering;
  measured timing remains separate.
- Stock Undo-history impact of the small batch.
- Safe non-UI-dependent command scheduling.
- Measured edit-ahead horizon and atomic generation replacement while transport
  runs.
- Cleanup and ownership behavior across Program/track/sequence changes.

Static insertion, playback, exact cleanup, and restoration passed. M0.6b's
source review nevertheless found no current NEUROGENIC transport-position API
or verified playback-engine queue horizon, so live replacement remains no-go;
see [M0.6b](M0-6B-LIVE-WINDOW-FEASIBILITY.md).

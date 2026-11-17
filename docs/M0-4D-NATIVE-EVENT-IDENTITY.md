# M0.4d native event identity

## Hardware evidence and failure cause

The owner-run M0.4c report
`target/hil/runs/20261117T175742.518743Z-3767503/report.json` records successful
build, activation, Step context, one command acceptance, and stock restoration.
After acceptance, later Step paints occurred without a context or traversal
error, but `event_observed` remained false. The owner then confirmed the note
in stock Step Edit and heard it through stock transport.

The scanner used `memcmp` over all 56 event bytes. That is not the stock
identity contract: the pinned Am MOD Step implementation uses firmware
comparator `0x19722e0` for exact native event selection. Tag-3 values also have
a reserved word and packed payload that the stock command may canonicalize.
The incompatible comparison therefore explains the false negative; the exact
bytes changed on hardware are not known.

## Correction

The firmware-1.3.0.12 adapter now:

- traverses at most 4096 nodes in the same guarded event-list context;
- compares with the stock event comparator used by Am MOD;
- rejects malformed traversal and multiple native matches separately;
- requires a unique match before reporting observation;
- copies that live canonical 56-byte value into the pointer-free receipt;
- supplies only that canonical value to the native remove command;
- verifies later absence with the same native comparator;
- rejects a matching preexisting event before insertion.

An ambiguous match is terminal and never triggers removal. Sequence, track,
Program, event-list, length, UI-thread, reentrancy, and operation-deadline
guards remain unchanged. No borrowed firmware pointer survives a callback.

`NATIVE DIAG` returns only bounded counters: scan attempts, traversed events,
tag-3 events, tick matches, note matches, native matches, and byte matches. The
runner records these on success and automatically includes them in terminal or
timeout errors. It exposes no pointer or arbitrary memory.

## Validation and gate

The AArch64/QEMU fixture covers native-equal but byte-different events,
canonical receipt adoption, ambiguous duplicate rejection, removal using the
canonical event, context change, and cleanup. Host Rust/Python tests cover the
bounded protocol and fail-safe runner. These tests do not prove live comparator
or removal behavior.

The inspected AArch64 shared library is
`target/m04a/build/neurogenic-m04a-native-test.so` (625,320 bytes), SHA-256
`51e7be14232c72b19fa92a1b825cccd179de99dffb5243a5ffc2efeeec5cd446`.
The linked ELF SHA-256 is
`3d437b9866f3a0a61da9c54b2839fbdc272704693e3cd01c147506f67770758b`.

M0.5 grid editing is not implemented yet. Its safety prerequisite is one
controlled run that observes exactly one inserted event, removes it, and
verifies absence. Use a fresh disposable unsaved sequence or known-empty track;
do not use the owner's saved project containing the prior event.

The one-command experiment remains:

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  native-experiment \
  --confirm MPC-SAMPLE-1.3.0.12 \
  --confirm-native INSERT-ONE-UNSAVED-NATIVE-EVENT-1.3.0.12
```

Follow the terminal's DATA-detent prompts. The run performs at most one add and
one remove and always attempts guarded stock restoration. Passing automated
observation/removal authorizes implementation review for the first playable
grid; it does not by itself validate that grid.

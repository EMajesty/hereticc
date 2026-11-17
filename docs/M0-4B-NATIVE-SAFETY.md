# M0.4b native adapter safety review

## Outcome

M0.4b keeps the M0.4a native event and stock command boundary, but closes the
two largest preflight gaps: an accepted event is now bound to the same live
sequence/track context for observation and removal, and synchronous paint
re-entry cannot reacquire the native operation path or deadlock on the receipt.
A dedicated one-command HIL experiment is implemented and host-tested. It has
not been run on an MPC; native insertion still requires its separate token and
explicit owner approval.

## Pinned-source audit

The implementation was compared line by line with Am MOD commit
`6cdc67276f43cd39f64871cb7e36782481244c65`, principally
`step_native.c`, `step_exceptions.cc`, and `step_abi.S`:

- firmware 1.3.0.12 offsets and signatures match the pinned tag-3 constructor,
  pad-to-note mapper, sequence-length accessor, native tree/string functions,
  add/remove command factories, shared-result release and tree successor;
- the 56-byte event, 48-byte native tree, constructor arguments, 960 PPQ and
  AArch64 hidden-result conventions match the pinned implementation;
- add uses three initialized native containers, two native strings, the live
  `controller+0x240` command service and current event list; all locally owned
  nodes, strings and shared results are released on success and exception;
- exact removal uses the pinned single-event command factory. Both commands go
  through the stock command service used for Undo, but live Undo replay remains
  unverified until hardware testing;
- the Step vtable slot and original `0x10e2e10` no-op match the source. The
  extension changes only that validated slot and restores page protection.
  An inability to restore the firmware vtable page read-only terminates the
  transient process rather than continuing with writable code metadata.

The QEMU fixture executes these ABIs with substitutes, not Akai firmware. It
cannot prove live allocation behavior, command execution, Undo, UI stability,
event persistence, transport playback or sound.

## Context and event identity

The live context now requires the current sequence, Program, track, event list,
command service and bounded sequence length. A two-word mixed fingerprint
combines the sequence, current track, event list and Program addresses without
retaining any individual address. Only that non-dereferenceable fingerprint is
copied; borrowed firmware pointers never leave the paint callback.

Arm and removal requests expire after two seconds. Arm captures the most
recently validated context, and the insertion callback rejects a changed token.
The receipt carries
the same tokens, and both presence lookup and removal reject any later context
change even when sequence lengths and event bytes match. A pre-insertion scan
also rejects an already-identical event, keeping exact removal unambiguous for
the controlled empty-sequence experiment.

No source-confirmed persistent sequence UUID is available. Address reuse after
destruction and recreation of the entire native graph is therefore an ABA
limitation; a fingerprint collision is also theoretically possible. The short
lease, same-process callback ownership, four-object input, fresh unsaved
sequence prerequisite and immediate removal bound these risks; they do not turn
the fingerprint into a proven stable firmware identifier.

## Reentrancy and state machine

The original stock paint target runs first. A nonblocking atomic guard admits
only one native callback operation; synchronous recursive paint performs the
stock paint but skips NEUROGENIC native work. Receipt and context mutexes are
held only long enough to copy pointer-free values and are never held across a
firmware command. The first valid paint establishes the owning pthread, and all
later native work must execute on that same thread.

Insertion is arm-once with no retry. Sticky evidence distinguishes command
acceptance from exact list presence. Removal is permitted only after presence,
uses one command attempt, and is not considered complete until a later paint
observes the exact event absent. Failure, rejection and expiry are terminal.

## One-command experiment

The prepared command is:

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  native-experiment \
  --confirm MPC-SAMPLE-1.3.0.12 \
  --confirm-native INSERT-ONE-UNSAVED-NATIVE-EVENT-1.3.0.12
```

The reviewed shared library is
`target/m04a/build/neurogenic-m04a-native-test.so`, SHA-256
`13f108f8bfc32d2e467c4fe2d5ffef618efed41f1a2d08e4b202a56483cb2294`.

This deliberately uses a token different from ordinary HIL authorization. The
runner reuses the existing lock, identity verification, immutable deployment,
single activation, mailbox, collection, restoration and report directory. It
also runs the AArch64 ABI fixture and artifact inspection before deployment.

The owner must begin with a healthy stock application. Activation restarts that
application, so unsaved setup must not be done beforehand. After activation the
command prints one bounded prompt and waits up to 180 seconds while the owner
creates a fresh, unsaved, stopped one-bar sequence, assigns a sample to A01 and
opens the stock Step editor. The runner then:

1. requires a recent, valid and idle Step context;
2. arms one A01 event at tick 0, velocity 100, duration 240 ticks;
3. records stock-command acceptance separately from exact presence;
4. requests one exact removal and requires later exact absence;
5. restores stock in `finally`, even after ordinary failure;
6. writes `report.json` and `summary.txt` below `target/hil/runs/`.

It never saves the sequence, retries an insertion, retries failed removal, or
claims audible playback. If removal or observation fails, it records the
ambiguous state and immediately uses the existing guarded stock-process
restoration; it does not attempt speculative cleanup.

## Physical gate

The experiment is ready for an authorization decision, not authorized by this
document. Its first execution introduces one verified in-memory vtable change
and, after the new confirmation token, one native stock-sequence command. No
physical operation occurred during M0.4b.

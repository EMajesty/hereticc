# M0.5b guarded playable-grid hardware test

Status: runner physically exercised; activation passed and stock restoration
passed, but the grid presentation did not open, so musical acceptance failed.

## Reviewed execution path

The M0.5a Step Edit integration remains unchanged: stock paint runs first, the
native adapter executes only on that callback's UI thread, and callback-local
screen/graphics pointers are never retained. A synchronous repaint request
cannot recurse into native work because the existing operation guard rejects a
nested callback. Follow-up observation remains bounded to eight paints. A
context identity change invalidates the transaction, closes the grid, clears
receipts, and fails closed.

The M0.5b build adds the existing test-only volatile mailbox to the experimental
artifact. Its read-only `GRID QUERY` command reports only bounded counters and
state: hook installation, Step paints, overlay activation, accepted intents,
verified insertions/observations/removals, invalidations, and current state. It
does not expose firmware pointers, memory, commands, paths, or audio claims.

The owner run recorded `hook=1`, 18 Step paints and `state=ready`, but zero
presentation activations, intents, insertions, observations, or removals. This
confirms the native paint path while leaving the old SHIFT+DATA activation
transition unobserved. M0.5c supersedes that activation model.

## One-command experiment

With the owner present at the MPC, run exactly:

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  grid-experiment \
  --confirm MPC-SAMPLE-1.3.0.12 \
  --confirm-grid PLAYABLE-GRID-ONE-BAR-1.3.0.12
```

The command verifies pristine stock state and the exact firmware fingerprint,
builds and inspects the AArch64 artifact, deploys it immutably under `/run`,
performs one controlled `ac50.service` restart, verifies the hook, and then
waits while the owner performs the test. It accepts only an explicit `PASS` or
`FAIL`. Stock restoration is attempted in `finally` after success, failure,
EOF, or interruption. Owner acceptance and restoration are recorded
independently under `target/hil/runs/<unique-run>/`.

Physical procedure shown by the runner:

1. Use a disposable, initially empty, one-bar 4/4 sequence with A01 assigned.
2. Open stock Step Edit, then open NEUROGENIC with SHIFT + DATA.
3. Select A01 and enable steps 1, 5, 9, and 13. DATA remains the explicit stock
   repaint handshake when the footer is pending.
4. Start stock transport and confirm four evenly spaced audible hits.
5. Disable step 9 and confirm three hits remain.
6. Close NEUROGENIC and inspect stock Step Edit: ticks 0, 960, and 2880 must be
   present and tick 1920 absent.
7. Enter `PASS` only if UI, event list, timing, audio, and controls all match;
   otherwise enter `FAIL`. Do not save the disposable sequence.

Stop and enter `FAIL` on a `FAILED`/context-change footer, wrong note or timing,
input loss, abnormal audio, unexpected restart, or inability to close the grid.
If automatic restoration fails, shut down normally, remove the development SD,
and perform a stock-only boot.

## Artifact and validation

The current reviewed artifact is:

```text
target/m05a/build/neurogenic-m05a-native-grid.so
```

| Artifact | SHA-256 |
|---|---|
| linked ELF | `2b3080bdeeda565e840b81c5a1d9b2ea01638a153b720eac9441474604c595c0` |
| stripped shared library | `23221a9943a50865607818ab04672cec06aa04a363ccaee68a19c0c96ce4ac49` |

The shared library is 657,616 bytes. The table is updated from the final
reproducible build during this milestone.
Host mocks cover owner PASS, owner FAIL, interruption, strict diagnostics,
distinct authorization, report generation, and restoration. These results do
not constitute physical validation or proof of audible playback. Final host
validation passed 113 Python tests and 138 Rust tests, Clippy with warnings
denied, formatting, both SDK patch-integrity checks, Nix flake checks, the
AArch64/QEMU ABI fixture, two reproducible cross-builds, shared-library
inspection, and the M0.3e rollback build/package inspection.

# HIL-1.2 — one-command hardware test cycle

Status: **host-validated; first physical run pending**. HIL-0 and the HIL-1
`QUERY` path are physically validated on firmware 1.3.0.12. HIL-1.1's complete
scenario and this orchestration have not yet run on hardware.

## One-command workflow

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  run --level B --scenario \
  --confirm MPC-SAMPLE-1.3.0.12
```

`run` reuses the existing strict identity, cross-build/inspection, immutable
upload, activation, settle, mailbox scenario, collection, and stock-restoration
functions. It verifies a healthy stock process with no drop-in and refuses any
existing `/run/neurogenic-hil` session before building. It then deploys and
activates the exact reviewed SHA-256, waits up to ten seconds for the bridge,
runs the eight-command scenario, collects state, restores stock once, and
verifies the restored process.

Level A is host-only and needs neither a configuration nor an MPC:

```sh
python3 tools/neurogenic-hil/hil.py run --level A
```

Level B still exercises the reducer inside the running mod. It does not
simulate physical SDK callbacks or verify screen/audio/stock sequencing.

## Failure and recovery

An atomic host lock at `target/hil/run.lock` prevents overlapping cycles. An
existing lock is never removed automatically: it denotes another owner or an
incomplete run requiring review. The runner also refuses an existing device
runtime, recognized or unrecognized service override, or non-stock process.

After device mutation begins, stock restoration runs from `finally` after an
ordinary failure. It performs the existing identity and exact-drop-in checks,
at most one restoration restart, and a healthy-stock settle check. Deployment,
activation, testing, and restoration are never automatically retried. A failed
restoration produces exit status 7 and an uppercase warning that the test mod
may remain active; the original failure is retained. If SSH is unavailable,
shut down normally, remove the development SD, and perform a stock-only boot.

## Reports

Every accepted run creates one mode-0700 directory under
`target/hil/runs/<UTC timestamp>-<host PID>/` and never overwrites another:

- `report.json`: schema `neurogenic-hil-run-v1`;
- `summary.txt`: compact final status.

The JSON records UTC start/end times, Git commit, test level, artifact SHA-256,
firmware fingerprint, initial/active/final service state, activation and
restoration results, every test command with expected/actual/pass and duration,
step durations, bounded diagnostics, errors, and assertion totals. It excludes
SSH keys, configuration paths, credentials, environment data, and unrelated
device information. Reports are bounded to 128 KiB.

Typical terminal output is:

```text
[1/6] Building...
[2/6] Deploying...
[3/6] Activating...
[4/6] Testing...
[5/6] Restoring stock...
[6/6] Reporting...
Overall PASS
Assertions: 8 passed, 0 failed
Artifact SHA-256: <hash>
Restoration: pass
Report directory: .../target/hil/runs/<run>
```

## First physical validation

Confirm normal stock UI/audio/controls, connect the already reviewed development
SD and USB cable, and run the command above once. Stop if the CLI reports an
existing session, unexpected drop-in, identity mismatch, restart anomaly,
mailbox timeout, or stock regression. Inspect `summary.txt` and `report.json`.
Success requires all eight assertions, restoration `pass`, a new healthy stock
PID, and normal UI/audio/controls afterward.

After this validation, development returns to the stock-sequencer integration;
HIL should grow only when a concrete feature test requires it.

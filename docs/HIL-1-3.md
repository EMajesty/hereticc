# HIL-1.3: live reducer scenario correction

## Physical evidence and root cause

The first complete HIL-1.2 cycle on firmware 1.3.0.12 validated build,
deployment, activation, Level B command delivery, automatic stock restoration,
and report generation. `RESET` and `OPEN` passed. Before `SELECT 4`, reducer
state changed from open to closed; the remaining edit assertions consequently
observed the initial track, step, and pattern.

Source inspection found exactly two asynchronous production paths that close an
open overlay: an SDK input callback's `close_if_page_gone()` visibility check,
and a paint callback for a non-Sample page. Level B `OPEN` changed the real
reducer but did not create a real SDK Sample-page context, so either callback
could legitimately apply production page-loss semantics between mailbox
commands. The hardware report does not identify which callback occurred. This
is a confirmed test-context/lifecycle conflict, not evidence that physical
page-loss handling is wrong.

## Correction

HIL builds now carry one atomic, test-only page-ownership token. A valid
mailbox `OPEN` claims it before the reducer lock; `CLOSE` and `RESET` release
it. While claimed, the two asynchronous page-loss paths do not close the
synthetically opened reducer. Normal callbacks still use `try_lock`, retain
their existing fail-closed input decisions, and do no mailbox I/O. The token
does not make Level B an SDK input, paint, or screen test.

The field and exception are compiled only with `hil-test-interface`.
Production M0.3e builds retain the original page visibility and non-Sample
paint closure behavior. Tests cover background page loss during a synthetic
session, edit commands after `OPEN`, release of the test context, real page
loss after release, and forwarding of unowned controls.

## Runner diagnostics

On any failed Level B assertion, terminal output now shows every failed
command with its expected and actual fields and explicitly identifies the
first unexpected transition. `report.json` remains the authoritative complete
record. Automatic restoration and the report schema are unchanged.

## Scope and next physical check

This is a Level B reducer integration test only. It does not exercise physical
pads/buttons, SDK callback delivery, drawing, stock sequencing, or audio. Run
the existing single command once:

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  run --level B --scenario --confirm MPC-SAMPLE-1.3.0.12
```

Acceptance requires eight passing assertions and verified stock restoration.
After that one check, development returns to the bounded stock-sequencer
adapter; HIL should not expand further without a concrete blocker.

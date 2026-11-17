# M0.4c native insertion diagnosis

## Physical result analyzed

The first M0.4b run never reached a Step-editor context. In the second run,
captured at `target/hil/runs/20261117T173619.604003Z-3744832/report.json`, the
stock service, deployment, activation, bridge, Step context and restoration all
passed. The runner accepted exactly one `NATIVE ARM`, but no native insertion
was confirmed.

The decisive response sequence was:

- the final pre-arm status was `native=idle context=ready`;
- `NATIVE ARM` returned `action_accepted=1 native=armed context=ready`;
- every later status remained `native=armed command_accepted=0`, while context
  freshness changed to `unavailable`;
- neither exact event presence nor removal was reported.

Because an eligible paint consumes the one-shot armed flag before it attempts
the adapter, the unchanged `armed` state proves that no callback reached that
operation path. The old protocol did not count callbacks, so it cannot strictly
separate no Step paint from a paint that returned before the arm path because
its live context was invalid. Idle Step repaint starvation is the strongest
source- and report-supported explanation. There is no evidence that the native
constructor or stock command factory ran.

The two-second deadline was also enforced only inside the paint callback. With
no later paint, the old model incorrectly remained `armed` until the host's
generic ten-second timeout. That was an observability defect, not evidence that
the firmware command hung.

## Focused correction

Native work remains on the verified stock Step-paint thread. The HIL worker
still changes only atomics and never calls drawing or firmware command APIs.

The native protocol is now version 2 and adds:

- `paint_after_arm=0|1`, proving whether an eligible callback followed arm;
- a bounded `diagnostic` stage distinguishing waiting for paint, paint entry,
  context loss/change, wrong thread, request rejection, native failure,
  command acceptance, observation, removal acceptance and removal;
- status-side deadline enforcement, so an idle screen reports
  `expired_before_paint` instead of remaining armed indefinitely.

The recent-context lease remains two seconds. The post-arm operation lease is
ten seconds to permit one deliberate physical repaint request; the callback
still revalidates the exact sequence/track/Program/event-list fingerprint and
same UI thread before dispatch. The runner tells the operator to turn DATA one
detent immediately after `ARM confirmed`. That stock input requests a Step
repaint without moving native execution to the worker or retaining a firmware
component pointer. It may move the stock Step cursor; it does not itself prove
or perform sequence insertion.

The next run will therefore end with one of these useful boundaries:

- `expired_before_paint`: no eligible Step paint followed arm;
- `context_unavailable` or `context_changed`: a paint occurred but its guarded
  editing context was unusable or different;
- `request_rejected` or `native_failure`: the adapter was reached and returned
  a classified failure;
- `command_accepted`: the stock command boundary accepted the event, after
  which exact list presence is checked separately.

No automatic re-arm or speculative cleanup was added.

## Next physical experiment

The same approval-gated one-command experiment is used:

```sh
python3 tools/neurogenic-hil/hil.py \
  --config target/hil/config.json \
  native-experiment \
  --confirm MPC-SAMPLE-1.3.0.12 \
  --confirm-native INSERT-ONE-UNSAVED-NATIVE-EVENT-1.3.0.12
```

The reviewed M0.4c shared library is
`target/m04a/build/neurogenic-m04a-native-test.so`, SHA-256
`657ed92c2224af0f15947ccd637c56aa36f1717639074ecc6d59b2f76727b3d8`.

After activation, prepare the same fresh unsaved stopped one-bar sequence with
A01 assigned and leave the stock Step editor visible. When the terminal prints
`ARM confirmed`, turn DATA exactly one detent. Do not repeat the action or
re-run automatically after failure. If the terminal reports `Command accepted`,
turn DATA one detent to run the separate exact-list observation. If exact
presence is confirmed, turn DATA one further detent only when the terminal
prints `REMOVE armed`, then one final detent at `Removal accepted` to verify
exact absence. Dispatch and observation therefore use the same UI-thread paint
boundary. The runner still performs at most one add, one remove, automatic
evidence collection and guarded stock restoration.

This document prepares that run; it does not authorize or claim a new physical
insertion, exact event presence, removal, Undo behavior or audible playback.

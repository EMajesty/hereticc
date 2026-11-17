# HIL-1 — repeatable deployment and in-process reducer tests

Status: **host-validated; physical validation pending**. HIL-0 was physically
validated by the owner on an MPC Sample running 1.3.0.12. This work did not
contact the device.

The owner subsequently validated HIL-1 activation, one live `QUERY`, and stock
restoration. [HIL-1.1](HIL-1-1.md) hardens request publication and callback
contention for repeated scripted tests.

## Result

HIL-1 preserves the fixed, strict SSH transport and adds immutable builds,
explicit activation/rollback, and a test-only in-process reducer bridge. The
bridge exercises the `UiState` inside the loaded NEUROGENIC mod; it does not
simulate physical controls or prove rendering/audio behavior.

The two physical HIL-0 compatibility fixes are now regression-tested:

- the SSH subprocess inherits the host's real `PATH` (needed on NixOS), while
  retaining only `PATH`, `LC_ALL`, `HOME`, and `SSH_AUTH_SOCK`;
- the BusyBox-compatible identity probe uses `dd ... bs=4096 count=1`, not
  unsupported `head -c`.

## Deployment lifecycle

`deploy` uploads a reviewed library to
`/run/neurogenic-hil/builds/<sha256>.incoming`, verifies its length and SHA-256,
then renames it to immutable `<sha256>.so`. An existing verified hash is
idempotent. More than three staged generations is rejected.

`restart --sha256 HASH` explicitly activates one staged build. It verifies the
library, active stock service, exact HIL drop-in, and recorded prior hash before
writing the new transient drop-in. It performs exactly one daemon reload and
one `ac50.service` restart, then verifies a new PID and active hash. It never
overwrites a mapped library and never automatically retries.

`rollback` activates the recorded previous immutable hash using the same path.
Only the active and preceding builds are retained after successful activation.
`restore-stock` verifies and removes the exact HIL drop-in, restarts stock once,
and removes only recognized volatile HIL files. Reboot remains the out-of-band
recovery if SSH or stock control is unavailable.

Example (device-changing commands require owner authorization):

```sh
python3 tools/neurogenic-hil/hil.py build --hil
python3 tools/neurogenic-hil/hil.py --config target/hil/config.json \
  deploy --hil --confirm MPC-SAMPLE-1.3.0.12
python3 tools/neurogenic-hil/hil.py --config target/hil/config.json \
  restart --sha256 HASH --confirm MPC-SAMPLE-1.3.0.12
```

## Level B bridge

The separate `neurogenic-device-hil` cdylib enables
`hil-test-interface`; normal M0.3e builds do not. At mod startup a worker owns
the fixed volatile mailbox:

- `/run/neurogenic-hil/request` — at most 64 ASCII bytes;
- `/run/neurogenic-hil/response` — at most 192 ASCII bytes;
- `/run/neurogenic-hil/bridge.ready` — fixed readiness marker.

The worker alone performs filesystem I/O. SDK callbacks use `try_lock`, forward
unchanged on contention, and never block on the bridge. Commands call the same
pointer-free reducer used by real callbacks. No firmware pointer crosses the
thread boundary. Responses report open state, selected track/step, step bits,
flow, restoration, and repaint state. Mailboxes and the worker disappear with
the stock process or explicit stock restoration.

Run one query or the deterministic scenario:

```sh
python3 tools/neurogenic-hil/hil.py --config target/hil/config.json \
  test --level B --command QUERY --confirm MPC-SAMPLE-1.3.0.12
python3 tools/neurogenic-hil/hil.py --config target/hil/config.json \
  test --level B --scenario --confirm MPC-SAMPLE-1.3.0.12
```

The scenario covers RESET, OPEN, repeated OPEN, SELECT, MOVE, two TOGGLEs,
CLOSE, invalid-command rejection, and final RESET. Every line includes actual,
expected, and pass state. A host-side timeout is terminal; it does not restart
the app.

## Test levels and limitations

| Level | HIL-1 status | Evidence |
|---|---|---|
| A — host reducer | implemented | Rust reducer/protocol tests |
| B — live process reducer | implemented, hardware validation pending | mailbox invokes the reducer in the loaded mod |
| C — real SDK input callbacks | not implemented | Level B commands do not synthesize pad/button callbacks |
| D — display/audio | not implemented | no screenshot or audio oracle |

The bridge changes reducer state from a worker. A natural stock paint is needed
before a visual update; HIL-1 deliberately does not call firmware drawing APIs
from that worker. Normal callbacks may forward during brief lock contention.

`virtual-mpc-sample` (`15f732c`, MIT) remains the preferred optional Level C/D
backend. Its existing Linux/QEMU runner can preload a mod and inject emulated
panel frames, but it requires separately obtained firmware assets and was not
executed here. HIL-1 does not create another emulator or claim those frames are
physical hardware input.

## Next physical validation

With explicit owner authorization: build `--hil`, deploy the printed hash,
activate it once, run the Level B scenario, collect state, restore stock, and
verify normal UI/audio/pads. Stop on host-key, firmware, drop-in, restart,
mailbox, or stock-behavior anomalies. Do not run tests automatically merely
because the device becomes reachable.

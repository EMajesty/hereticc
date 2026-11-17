# HIL-0 — USB development and hardware-test foundation

Status: **physically validated baseline** on one MPC Sample running 1.3.0.12.
The owner confirmed identity verification, volatile deployment, controlled
restart, M0.3e operation, and stock restoration. HIL-1 supersedes the original
two-slot deployment and implements the previously blocked Level B bridge.

## Outcome

The original HIL-0 implementation provided:

- a standard-library Python CLI with `status`, `build`, `deploy`, `restart`,
  `test`, `collect`, and `restore-stock`;
- a strict SSH transport fixed to `root@192.168.155.1:2222`, public-key-only
  authentication, a dedicated known-hosts file, bounded output, and timeouts;
- exact firmware-prefix, architecture, service, PID, and executable checks
  before device-changing operations;
- two bounded volatile build slots under `/run/neurogenic-hil`;
- one exact transient `ac50.service` drop-in and at most one restart per
  requested `restart` or `restore-stock`;
- a feature-gated, fixed-capacity reducer command protocol with no I/O;
- mocked host tests. They never discover or contact an MPC.

It did **not** install SSH, simulate physical controls, capture screenshots or
audio, or expose arbitrary remote commands. The subsequent owner run validated
its device workflow; HIL-1 supplies the reviewed Level B bridge.

## Reuse audit

Heads rechecked on 2026-11-16:

| Upstream | Revision / license | HIL disposition |
|---|---|---|
| `orrpunk-art/mpc-sample-root-ssh` | `8334ca085b3cff327f62a23cf0aad489dc8d578d`; **no license found** | External owner-installed prerequisite only. Do not copy or redistribute its source/package. |
| `orrpunk-art/virtual-mpc-sample` | `15f732c71fdd722c924590b9cdec2429f7a1ec2b`; MIT | Future Level C/D emulator backend. It already injects control-surface frames, captures QEMU screenshots, exercises autorun packages, and can preload a mod. HIL-0 does not reimplement it. |
| `ioma8/mpc-sample-sdk` | `9487cbba3648afb29d8e3107e31a7ac930985220` (`v0.7.1`); MIT | Direct pinned dependency and build/hook/package authority. |
| `ioma8/mpc-minimod` | `18d41fd805beace4fed623653a250c46a4fa2243`; manifests say MIT, no root license found | Patterns/reference only pending license clarification. |
| `gospodindilly-ops/mpc-sample-lab` | `3d482b94c918b96d174f31c411686f2b18b74ce6`; no project license found | Hardware/debug facts only; no copied code. Its replacement runtime stops stock and is not this harness. |

The root-SSH source and hardware note establish a runtime-only transient
`sshd` using the device's existing binary. It binds only
`192.168.155.1:2222`, disables passwords and forwarding, uses an owner-generated
client key and host key, leaves `ac50.service` running, and stores active files
under `/run`. Reboot removes it; removing the SD while running does not. It is
still an unaudited root shell, and USB-only binding is not authentication.

## Original CLI safety contract

Run `python3 tools/neurogenic-hil/hil.py --help`.

`build` invokes only `device/m03e/scripts/build-package.sh`. `deploy` accepts
only `target/m03e/build/neurogenic-m03e-ui.so`, verifies live stock firmware,
then transfers it to a fixed no-clobber incoming path. A verified current build
moves to one `previous.so` slot; a third unresolved generation fails closed.
Deployment alone does not restart stock.

`restart` verifies active stock state and refuses unrelated service drop-ins.
It writes only this transient configuration:

    [Service]
    Environment=LD_PRELOAD=/run/neurogenic-hil/current.so

It performs one daemon reload and one `ac50.service` restart, then polls
read-only state for at most 15 seconds. It never retries the restart. Failure
requires an explicit `restore-stock`.

`restore-stock` removes only the exact expected HIL drop-in after hashing it,
reloads systemd, restarts stock once, and verifies stock state. It leaves the
volatile libraries for diagnosis and reboot cleanup. No operation reboots the
MPC or writes internal persistent storage.

`collect` returns only identity, service state, and hashes for the two fixed
runtime slots. It can create one new JSON file directly under `target/hil`; it
will not overwrite. SSH stdout/stderr are each bounded to 8192 bytes.

## Original test interface

The `neurogenic-device` feature `hil-test-interface` is disabled by default.
When enabled it accepts at most 64 ASCII bytes:

    OPEN
    CLOSE
    MOVE <signed -16..16, excluding 0>
    SELECT <1..16>
    TOGGLE
    QUERY
    RESET

It calls the existing `UiState` reducer, not a duplicate model, and emits at
most 192 ASCII bytes containing overlay state, selected track/step, current
track bits, forward/consume, repaint, restoration, and pending-repaint state.
It performs no network, filesystem, process, firmware-pointer, or callback I/O.
No exported firmware ABI or delivery transport exists yet.

| Level | HIL-0 status | Claim |
|---|---|---|
| A — host reducer | implemented and host-tested | reducer/protocol behavior only |
| B — in-process MPC mod | protocol implemented; delivery moved to HIL-1 | no physical-input simulation |
| C — real input path | planned; prefer virtual MPC first | must exercise SDK callbacks/stock path |
| D — display/audio | planned | counters do not prove visible/audible output |

## Historical first physical setup procedure

This procedure is proposed, not executed or authorized by this document.

1. Save work, verify stock firmware 1.3.0.12 and normal UI/audio/pads, and keep
   the known-good M0.3c card/package available.
2. Obtain the upstream root-SSH release directly from its owner. Because it is
   unlicensed, NEUROGENIC does not package it. Inspect its fixed revision and
   run its local key generator without `sudo`.
3. Use a dedicated client Ed25519 key with a passphrase. Keep its private key
   on the host. The prepared SD contains only the public client key and its
   separate server host key.
4. Verify the server public-key fingerprint out of band. Create a dedicated
   known-hosts entry from that public key before connecting:

       mkdir -p target/hil
       awk '{print "[192.168.155.1]:2222 "$1" "$2}' \
         /reviewed/root-ssh/autorun/ssh_host_ed25519_key.pub \
         > target/hil/known_hosts
       chmod 600 /absolute/path/to/client_ed25519 target/hil/known_hosts

5. Copy `tools/neurogenic-hil/config.example.json` to
   `target/hil/config.json`, use absolute paths, and verify the dedicated SSH
   SD contents. Do not merge autorun packages.
6. With separate physical authorization, boot normally with the development
   SD and USB data cable. Allow up to 45 seconds for upstream SSH startup.
7. Run the first read-only gate:

       python3 tools/neurogenic-hil/hil.py \
         --config target/hil/config.json status

8. Stop on any host-key mismatch, authentication anomaly, firmware mismatch,
   unexpected service/drop-in, missing stock PID, abnormal UI/audio/control
   behavior, or inability to return to stock. Do not deploy in that session.

Only after the read-only result is reviewed should an owner separately
authorize `build`, `deploy`, then `restart`. The confirmation token is
`MPC-SAMPLE-1.3.0.12`. `restart` is disruptive: the SDK hooks modify the new
stock process in memory and stock UI/audio will be unavailable briefly.

Recovery is:

    python3 tools/neurogenic-hil/hil.py --config target/hil/config.json \
      restore-stock --confirm MPC-SAMPLE-1.3.0.12

If SSH/control is unavailable, use normal shutdown, remove the development SD,
and boot stock-only. A forced shutdown is a last resort and may lose unsaved
work.

## HIL-0 prerequisites at completion

- owner acceptance and setup of the unlicensed external root-SSH package;
- first read-only hardware identity/host-key check;
- physical confirmation that fixed `/run` staging and the transient drop-in
  behave as modeled;
- a bounded in-process Level B bridge designed around an SDK-safe task context;
- virtual-MPC backend integration for scripted real-path events/screenshots;
- a separate audio/display oracle before making Level D claims.

See [HIL-1](HIL-1.md) for the repeatable immutable deployment lifecycle and
in-process reducer tests. Neither milestone simulates physical input or grants
automatic permission to change a connected device.

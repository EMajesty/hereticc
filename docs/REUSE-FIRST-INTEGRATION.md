# Reuse-first MPC Sample integration

Status: host research and integration decision. No device package or hardware
operation is created or authorized by this document.

## Decision

NEUROGENIC will use pinned `mpc-sample-sdk` v0.7.1 as the device integration
layer instead of extending the B1 diagnostic infrastructure. The next device
milestone is a small SDK mod that draws one NEUROGENIC page and reads its owned
controls while forwarding everything else. A following, separately authorized
spike will exercise the SDK's existing stock pad-input call path. It will not
wait for the generalized B1 relay, SSH access, or a complete hardware
abstraction layer.

P3.2l is host-complete. Its source audit found and fixed a concrete early
failure: the collector treated `/proc/mounts`' escaped `\040` spelling as the
literal mount path. The P3.2k physical result does not include the mount row, so
that defect is the leading explanation rather than a physically proven root
cause. This distinction no longer blocks feature integration. Do not create a
P3.2m or repeat P3.2k; retain P3.2l only if its lifecycle evidence later becomes
necessary for a specific relay decision.

## Upstream disposition

The revisions below are the inspected, current upstream heads on 2026-11-16.

| Project | Revision and license | Disposition | Reuse boundary |
|---|---|---|---|
| `ioma8/mpc-sample-sdk` | `9487cbba3648afb29d8e3107e31a7ac930985220`, tag `v0.7.1`, MIT | **Direct dependency** | Use its firmware verifier, loader/hooks, typed input, paint/drawing, repaint, pad lighting, audio observation, stock pad-input calls, cross-build, and autorun generator. Pin the revision and preserve its license. |
| `ioma8/mpc-minimod` | `18d41fd805beace4fed623653a250c46a4fa2243`; Rust manifests say MIT, no root license found | **Reference**, then **adapt** only after licensing is clarified | Follow its `Mod` organization, page-scoped input ownership, repaint pump, held-note ledger, build, and recovery patterns. Do not copy repository code yet. |
| `AlekseyMalykh/am-mod` | `6cdc67276f43cd39f64871cb7e36782481244c65`, MIT plus notice | **Reference** | Use its physically tested native event/play/release findings to guide stock-sequencer research. Its RAM-copy patch architecture is too broad for the first prototype. Copy nothing without a focused provenance review. |
| `gospodindilly-ops/mpc-sample-lab` | `3d482b94c918b96d174f31c411686f2b18b74ce6`; no license found | **Reference** | Hardware/protocol facts only. Its replacement audio/display mode stops stock `ac50`, contrary to the product goal; public source is not reusable code. |
| `jeffwebb-to/mpc-sample-tracker` | `4a13c1792ac9a4200567950ac9eb5c0b505348ce`, MIT | **Reference**; replacement runtime **avoid** | Timing, tracker workflow, and test ideas are useful. It stops the stock app and owns DRM/ALSA/control surface, so it is not the integration base. |

This policy prefers pinned dependencies and small upstream compatibility fixes.
NEUROGENIC should contribute generally useful SDK fixes upstream rather than
maintaining a private loader, hook engine, packaging system, or firmware table.

## Existing SSH debugging

MPC Sample Lab points to `orrpunk-art/mpc-sample-root-ssh` commit
`8334ca085b3cff327f62a23cf0aad489dc8d578d`. Its source and accompanying
hardware record establish the following for one MPC Sample on firmware
1.3.0.12:

- stock autorun creates a runtime-only transient systemd unit and starts the
  device's existing `/usr/sbin/sshd` without restarting `ac50.service`;
- it binds `192.168.155.1:2222`, uses root public-key authentication, disables
  passwords and forwarding, and keeps configuration, credentials, and logs in
  `/run`;
- it runs beside the stock application; two cold boots reportedly retained
  normal UI, audio, and pads;
- shutdown/reboot removes the runtime service and files, but removing the SD
  while running does not stop SSH;
- it exposes a full root shell to the directly attached USB network. USB-only
  binding reduces reachability, but is not authentication or confinement.

The SSH repository contains no license file. Classify it as an externally used
reference, not code NEUROGENIC may copy or redistribute. It is also unnecessary
for the next SDK prototype. If a later integration failure is otherwise opaque,
a separate owner-authorized session could first check for `strace`, `readelf`,
and dynamic-loader diagnostics, then inspect the exact process/service state or
run one fixed command. Utility availability is unverified; do not install
fallback tools. This is preferable to another bespoke diagnostic package only
when the required observation cannot be obtained through the SDK's bounded
reporting.

## SDK integration shape

Keep `neurogenic-core` unchanged and platform-neutral. Add one device-only mod
crate that depends on the exact SDK revision and contains only the translation
needed for the current spike:

```text
SDK Mod callbacks
  paint  ------> small 320x240 page renderer
  controls ----> page-local edit actions
  pad path ----> stock forward/consume policy
  scheduler ---> bounded due-event queue ---> UI-thread stock note calls
```

The first implementation need not expose a general public adapter trait. It
should use fixed-capacity state, retain no firmware pointer outside its callback,
pair every consumed press with its release, and restore stock repaint and pad
lighting when the page closes. All unowned controls forward to stock.

The SDK directly covers the first two prototype questions:

1. `on_paint` plus stock drawing primitives can render a page over the Sample
   page; `on_button`, `on_pad`, and `on_encoder` provide typed input.
2. Unsafe `send_note_on`/`send_note_off` can replay the displaced stock pad path
   while the callback's input/source/properties objects are live.

The second facility is not yet a sequencer voice API: it returns no voice token,
accepts no target frame, and has unverified overlap/release/transport behavior.
Am MOD demonstrates that deeper native event and engine calls are possible, but
those calls should be adapted only if the SDK pad path fails the measured
prototype gate. Do not adopt Sample Lab or Tracker's replacement audio engine as
an implicit fallback.

## Next feature milestone

Implement **M0.3c — SDK page and input prototype**:

- add a device-only Rust mod crate pinned to SDK v0.7.1 commit `9487cbb`;
- render a static NEUROGENIC step grid on the SDK's Sample-page paint hook;
- enter/leave the page with one explicit button chord;
- use DATA to move a cursor, pads to select tracks, and one button to toggle a
  step in a small fixed in-memory pattern;
- forward all controls outside that page and preserve paired releases;
- restore the stock page and pad lights on exit;
- use the SDK's existing guarded package generator and rollback instructions;
- host-test the page state reducer and input ownership separately from firmware.

Acceptance on hardware is limited to page visibility, input correctness, stock
forwarding, and complete restoration. It installs no scheduler playback path.
The next small milestone then calls the stock pad path for one sound and measures
repeat/overlap/release behavior. Only after that passes should the existing core
scheduler feed a bounded adapter queue; step editing follows, then parameter
locks whose stock mapping is actually demonstrated.

This sequence returns development to the differentiating product while keeping
stock-engine feasibility as an explicit measured gate.

# M0.3b-B1-P2 USB network and diagnostic access investigation

Status: host-only investigation and offline tooling. No MPC was contacted or
modified. No hook, firmware, package, on-device reader, service, or payload was
built or executed.

## Executive summary

New physical evidence closes an important B1-P1 gap: one MPC Sample running
firmware 1.3.0.12 exposes USB audio, MIDI, and CDC-NCM Ethernet, and the device
at `192.168.155.1` serves an Akai-supported browser firmware updater. Akai's
[update guide](https://support.akaipro.com/en/support/solutions/articles/69000877649-mpc-sample-firmware-update)
documents both `mpc-sample.local` and `192.168.155.1` for that purpose.

This establishes a supported network transport and HTTP service, but not a
diagnostic path. The rendered updater is observed using `GET /version.json` and
the state-changing `POST /software-update`. Generic object-API and WebSocket
client code is present in its JavaScript bundle, but the supplied evidence does
not connect it to a rendered component or prove server support. A physical
`GET /api/v1/subtree` returned JSON `{}` with 404.

No public AZ0X server source or protocol documentation found in this review
documents arbitrary file reads, commands, `/run`, process inspection, or a
read-only diagnostic export. The public inMusic
[AZ0x open-source release](https://github.com/inmusic-software/az0x-open-source)
describes third-party Linux components released for license compliance; it does
not document the proprietary updater application's HTTP contract.

**B1 remains no-go.** USB networking may be a future transport, but the stock
server supplies no evidenced operation that invokes the B0.2 reader or returns
its bounded result while preserving the live stock PID.

## New physical evidence

The following owner observations apply to one unit on firmware 1.3.0.12 and
were not reproduced during P2:

- USB VID:PID `09e8:205c`, product `MPC Sample`.
- Composite USB audio, USB MIDI, and CDC-NCM Ethernet interfaces.
- Linux `cdc_ncm` binding; host interface `enp14s0u2u4u2i3` at
  `192.168.155.8/28`; DHCP server/device address `192.168.155.1`.
- Three successful ICMP requests at approximately 1.7 ms.
- HTTP 200 for `/` from `AZ0X Web Server/0.5.1+git9826367`, serving a React
  updater that displayed firmware 1.3.0.12.
- Active frontend asset `/static/js/main.d096576e.js` and updater calls
  `GET /version.json` and `POST /software-update`.
- Generic `/api/v1/...` remote-object and WebSocket client code in the bundle.
- HTTP 404, `application/json`, `{}` from `GET /api/v1/subtree`.

The prior request is historical evidence supplied to P2. Neither the offline
tool nor this investigation issued any device network request.

## Existing source evidence

- A4's autorun restarts `ac50.service`; it is not a live retrieval channel.
- B0.2 creates a mode-0600, process-specific 72-byte snapshot under `/run`.
- The reader validates version, exact length, counter consistency, stable
  repeated reads, PID, exact executable identity, and process start time.
- Its formatted report is capped at 384 bytes, but the reader has no on-device
  invocation or network transport and is not packaged.
- Neither A4 nor B0 adds an endpoint, listener, worker, or callback I/O.

## Supported MPC Sample USB workflows

Akai documents the browser updater over USB and says a completed update restarts
the device. This supports the updater transport, not diagnostic access. The
[MPC Sample FAQ](https://support.akaipro.com/en/support/solutions/articles/69000876967-mpc-sample-frequently-asked-questions)
also documents a class-compliant drive, USB sampling, and direct content
transfer. The [product page](https://www.akaipro.com/mpc-sample/) describes
SD-card file transfer and USB audio/MIDI connectivity. USB MIDI is documented
in the [MIDI guide](https://www.akaipro.com/guides/mpc-sample/midi_configuration.htm),
and USB audio input in the
[input guide](https://www.akaipro.com/guides/mpc-sample/input_configuration.htm).

These are distinct class/protocol functions:

- audio carries streams, not files or commands;
- MIDI carries musical/control events, not process diagnostics;
- user-content transfer exposes supported storage, not `/run`; and
- the updater accepts the vendor's firmware workflow, not arbitrary files.

No reviewed MPC Sample documentation identifies diagnostic file access, remote
commands, authenticated management, or desktop companion functionality that can
read the live root filesystem. Other MPC models are not evidence for this unit.

## HTTP frontend architecture

The complete bundle is not in the repository. P2 therefore does not claim a
full component/import graph and records how the operator can supply the bundle
privately for offline inspection.

### Rendered application

| Operation | Evidence | Interpretation |
|---|---|---|
| `GET /` | Physical 200; officially documented origin | Active updater entry point |
| `GET /static/js/main.d096576e.js` | Referenced by rendered HTML | Active frontend asset; unavailable locally |
| `GET /version.json` | Bundle observation and rendered version | Active read-only updater call; response schema not saved |
| `POST /software-update` | Bundle observation and documented update workflow | Active firmware upload; state-changing and prohibited |

The rendered behavior is fully explained by a small firmware updater without
the generic object client.

### Generic client and WebSocket code

The bundle contains definitions for:

- `GET /api/v1/subtree`;
- `GET /api/v1/object-meta...`;
- `GET /api/v1/object-properties...`;
- `PUT /api/v1/object-properties...`;
- `POST /api/v1/object-method...`; and
- WebSocket object-change notifications.

This is consistent with a reusable remote-object client in the dependency
graph. It does not prove the updater instantiates that client, initializes a
WebSocket, or has a corresponding server module. The one tested route returned
404. Thus client-library presence is confirmed, active-component reachability
and WebSocket initialization are unknown, and server implementation is
unproven with one negative observation.

Do not commit the proprietary bundle. An operator may place an already-saved
copy in a private directory and use the offline tool described below.

## AZ0X Web Server findings

`AZ0X Web Server/0.5.1+git9826367` appears to identify an inMusic embedded
component, not a publicly documented general-purpose HTTP server:

- inMusic uses AZ0x for a shared embedded Linux platform family;
- the official open-source repository lists an MPC Sample v1.3.0 release of
  third-party Linux components;
- the physical server delivers the updater; and
- Akai officially documents this origin only for firmware update.

No reviewed official source publishes the server implementation, endpoint
catalog, authentication model, object protocol, file service, or diagnostic
contract. It is reasonable to infer a firmware-update component that may share
framework code across products. It is not justified to infer arbitrary file
reads, shell access, usable RPC, or a broader supported management framework.
The build suffix has no public source mapping found in this investigation.

## Endpoint inventory

| Path/interface | Method | Evidence | Risk and disposition |
|---|---|---|---|
| `/` | GET | Physical 200; documented updater | Read-only rendering expected; may save bounded fixture |
| `/static/js/main.d096576e.js` | GET | Active HTML asset | Read-only asset; keep private and inspect offline |
| `/version.json` | GET | Active updater call | Expected read-only; firmware version only, not counters |
| `/software-update` | POST | Active documented upload | Firmware write and restart; prohibited |
| `/api/v1/subtree` | GET | Client literal; physical 404 `{}` | Do not repeat or enumerate |
| `/api/v1/object-meta...` | GET | Client definition only | Unknown; do not invoke |
| `/api/v1/object-properties...` | GET | Client definition only | Unknown; do not invoke |
| `/api/v1/object-properties...` | PUT | Client definition only | Explicit mutation; prohibited |
| `/api/v1/object-method...` | POST | Client definition only | Unknown method semantics; prohibited |
| WebSocket | upgrade/frames | Client code only | URL/protocol/side effects unknown; do not connect |

No endpoint is documented to read `/run`, inspect a process, execute a command,
export diagnostics, or accept an arbitrary path. One 404 does not prove every
generic route absent, but removes any basis for treating the API as available.

## Diagnostic retrieval feasibility

| Possibility | Evidence | Missing evidence and permissions | Lifecycle/security assessment | Feasibility |
|---|---|---|---|---|
| Read-only HTTP diagnostic/export | Version read only | Handler for validated snapshot/report, access control, bound | Could preserve PID if it existed | No current path |
| Remote-object API | Client code; subtree 404 | Active component, routes, object tree, authentication | Unknown methods/properties may mutate; enumeration is probing | Not feasible or authorized |
| Vendor file transfer | Documented user-content drive | `/run`, live namespace, mode-0600 and `/proc` access | User storage is wrong namespace; PID continuity unknown | Not suitable |
| Authenticated management | None | Existence, contract, credentials, support | Could create persistent state and attack surface | No evidence |
| Stock export | User project/content workflows | Diagnostic source and bounded destination | Potentially safe if vendor-defined; none documented | No current path |
| Network command | None | Command contract and B0.2 privileges | General commands greatly broaden risk | No evidence |

The confirmed USB network can transport HTTP responses without necessarily
restarting `ac50`, but no stock handler bridges it to the B0.2 snapshot. Adding
an endpoint, CGI, plugin, symlink, upload, or helper would be a new on-device
component and is outside P2.

## Offline inspection tool

`tools/b1_p2_inspect.py` accepts only local files. It performs no DNS, socket,
HTTP, subprocess, browser, or JavaScript execution. Inputs are capped at 32 MiB.
Its JSON output records basename, size, and SHA-256; allowlists non-identifying
HTTP headers; and extracts HTML scripts, literal API paths, simple direct
`fetch`/Axios calls, WebSocket constructor count, USB protocol facts, and IP/DHCP
literals.

`direct-static-call` is only a syntactic classification. It does not prove
runtime reachability or server support; computed/minified wrappers may be
missed.

```sh
python tools/b1_p2_inspect.py \
  --headers /path/to/root.headers \
  --html /path/to/index.html \
  --javascript /path/to/main.d096576e.js \
  --usb /path/to/usb-descriptors.txt \
  --network /path/to/network-observation.txt \
  > /tmp/mpc-sample-b1-p2-evidence.json
```

Review/redact output before sharing. Keep raw assets, serial numbers, MAC
addresses, lease identifiers, cookies, and proprietary bundles out of the repo.

## Evidence gaps

1. Exact saved HTML, `version.json`, and bundle with sizes and SHA-256 values.
2. Static import/call reachability for the generic client and WebSocket.
3. `version.json` schema, headers, and conservative size bound.
4. PID/start-token continuity during ordinary user-content drive access, which
   still would not provide `/run` access.
5. Any official/source-backed diagnostic export contract.
6. A supported principal/operation able to read the snapshot and
   `/proc/<pid>` identity without configuration changes.
7. A bounded egress operation returning only the formatted report.

Items 6 and 7 remain decisive. Frontend analysis cannot create them.

## Consolidated operator procedure

### Tier A — passive host observations

1. Boot stock 1.3.0.12 without autorun media; record normal UI, audio, and DATA
   behavior. Observe PID/start token only through an already-authorized method.
2. Connect USB normally. Save redacted `lsusb -v` output for `09e8:205c`, bound
   driver/interface facts, address/route output, and DHCP facts. Exclude MAC,
   client ID, serial, and unrelated interfaces.
3. Preserve browser-cached/already-downloaded HTML, headers, `version.json`, and
   bundle without visiting unknown URLs. Hash originals before formatting.
4. Run the offline tool and manually trace component/import references around
   object-client and WebSocket constructors. A literal alone remains unknown.
5. Observe an already-mounted class-compliant drive read-only, recording only
   volume type and system-defined top-level structure. Do not write, repair,
   read a raw device, open user content, or probe hidden paths.

Stop on restart, stock UI/audio/control interruption, storage-repair/update
prompt, unexpected write, or request to change a device setting.

### Tier B — bounded read-only device requests

P2 did not execute these. Use only the confirmed origin, one request at a time,
without redirects, cookies, or authentication.

| Order | Request | Expected response | Limit | Rationale and stop conditions |
|---:|---|---|---|---|
| 1 | `GET http://192.168.155.1/` | 200 updater HTML | 256 KiB, 3 s | Official page; stop on redirect, auth, wrong type, oversize, restart, or behavior change |
| 2 | `GET http://192.168.155.1/version.json` | Small JSON with 1.3.0.12 | 4 KiB, 3 s | Active version read; same stop conditions plus non-JSON |
| 3 | `GET http://192.168.155.1/static/js/main.d096576e.js` | Referenced JavaScript | 16 MiB, 5 s | Active static asset; stop on redirect/wrong type/oversize and keep privately |

Observe stock UI, audio, and DATA behavior before/after. Compare PID/start token
only through an already-authorized method. Do not repeat `/api/v1/subtree`,
enumerate `/api`, guess paths, send `OPTIONS`, attempt WebSocket upgrade, scan
ports, or follow source-map URLs.

### Tier C — separate authorization required

- `POST /software-update`, updater apps, firmware data, or update mode.
- Object-property `PUT`, object-method `POST`, unknown RPC, WebSocket sessions,
  authentication attempts, or endpoint enumeration.
- Device commands, shells/debug access, service activation, port scans,
  credentials, firewall changes, or persistent configuration.
- Hook deployment, B0.2 device packaging, on-device helpers, server extensions,
  CGI, listeners, workers, or daemons.
- Project, sample, setting, internal-storage, rootfs, or firmware modification.

None belongs in an executable inspection script. Each requires a separate
design, threat/lifecycle analysis, bounded output, rollback plan, and explicit
authorization.

## Alternative architectures

**A. Existing stock-supported retrieval.** Preferred and least invasive, but no
diagnostic operation is demonstrated. Version retrieval is too narrow and user
storage is the wrong namespace.

**B. Bounded export at an existing stock lifecycle point.** A future design
could run the validator after interaction and publish only its 384-byte report
to the validated SD directory. It still needs a non-callback, non-resident,
non-restarting trigger. None is known.

**C. Separately authorized transient component.** A one-shot validator could
return a bounded result over USB networking or SD, but any HTTP helper or
invocation mechanism changes privileges, ancestry, attack surface, and rollback
requirements. This needs a new architecture/safety milestone.

**D. Defer B1 physical testing.** This retains the strongest guarantees and
avoids a hook run whose forwarding cannot be observed. It is recommended now.

## Recommendation and go/no-go

Preserve CDC-NCM/HTTP as a confirmed transport and updater interface. Complete
Tier A and, if desired, the three allowlisted GETs to archive the exact frontend
for offline analysis. Do not probe the generic API further.

Unless official documentation or saved-source analysis reveals an existing
read-only diagnostic/export operation reaching the snapshot, select a separate
bounded-export design review or defer testing. The least invasive feasible
choice today is deferral.

**B1 physical testing is not authorized.** A future go decision still requires
a non-restarting invocation path, live PID/start-token validation, bounded
result transfer, and all existing hook/rollback approvals. USB reachability and
a firmware updater do not satisfy those conditions.

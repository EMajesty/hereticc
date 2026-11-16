# M0.3b-B1-P3.1/P3.1a host-only diagnostic relay model

Status: implemented, hardened in P3.1a, and host-tested. These milestones add no listener, socket
I/O, device filesystem access, process spawning, systemd unit, hook, package,
or deployable device payload. B1 physical execution remains unauthorized.

## Implemented scope and boundaries

The standard-library-only `neurogenic-b1-relay-model` workspace crate contains
five deliberately separate contracts:

1. a strict parser for the existing bounded constructor report;
2. an in-memory B0.2 snapshot decoder and identity/counter equivalence model;
3. a `SnapshotReader` interface through which future integration must call the
   unchanged B0.2 reader;
4. a deterministic relay state machine driven by injected monotonic times; and
5. a fixed-scope parser for one complete HTTP/1.1 request already held in a
   bounded buffer.

It contains no transport. The parser cannot accept a path argument, command,
upload, arbitrary method, object API, firmware operation, or configuration
change. Tests supply constructor bytes, repeated snapshot bytes, identity
observations, clock values, and reader outcomes as fixtures; they do not inspect
the host's `/proc` or create device-shaped files.

The model duplicates the 72-byte decode rules for fixture testing because the
B0 crate is device-facing, depends on the pinned patched SDK and `libc`, and is
intentionally outside the host workspace. Moving its reader would change B0
production structure in this milestone. The production boundary is therefore
the `SnapshotReader` trait: a future adapter must invoke B0.2's unchanged
`read_verified`/`format_snapshot`, not substitute the model decoder. Equivalence
fixtures cover magic, version, exact length, reserved field, counter invariants,
stable repeated reads, PID, exact executable, start token, and the 384-byte
format bound.

P3.1a audited this model against the authoritative B0.2 source. B0.2 makes at
most eight attempts, and each attempt performs two fresh reads from offset zero:
16 individual reads at most. Only the two reads within one attempt are compared;
overlapping adjacent reads from different attempts are not. A read error aborts
immediately. A matching pair is decoded immediately, so a stable malformed pair
returns a format error rather than being skipped. If all eight pairs differ, the
result is `Unstable`. Process executable and start-token verification happens
only after a stable pair decodes. The fixture model now follows that ordering.
Supplying fewer fixture reads can return `Unstable` early, while the production
reader owns a real file and continues until success, read error, or all eight
attempts; this is an intentional fixture-source distinction.

For test diagnosis, the model distinguishes `WrongLength`, `WrongMagic`, and
`WrongVersion`; B0.2 maps those header/metadata cases to its coarser
`SnapshotError::Malformed`. A fixture slice is also not a real `read_exact`:
the model maps a short/long supplied read to `WrongLength`, whereas production
maps an unexpected EOF to `Malformed` and other read failures to `Access`.
These error-label differences do not change acceptance, attempt count, or
identity ordering.

B0.2 currently totals delta classes with saturating addition. Saturation can
make an overflowing invalid tuple equal `u64::MAX`. P3.1a therefore applies a
strict checked-add invariant both in fixture decoding and again to every result
returned by a future production-reader adapter. This is intentionally stricter
than B0.2 and prevents relay acceptance of an overflow combination without
changing B0 production code or the snapshot format.

## Constructor-report compatibility

This matrix reflects the actual `device/m03b-b0/src/lib.rs` formatter, not the
P3 proposal alone.

| Required evidence | Actual field | Classification | P3.1 treatment |
|---|---|---|---|
| Experiment identity | `hereticc_experiment=m03b-b1p0` | Available and sufficient | Exact match required |
| Patch identity | `patch_set=m03b-b1p0-snapshot-identity-v4` | Available and sufficient | Exact match required |
| SDK revision | `upstream_sdk_commit=...` | Available and sufficient for provenance, but not an arming invariant currently proposed | Parsed as a bounded field; exact patch identity remains the compatibility gate |
| Expected firmware | `expected_firmware=1.3.0.12` | Available and sufficient | Exact match required |
| Firmware compatibility | `firmware_compatible=true` | Available and sufficient with count | `true` required |
| Fingerprints validated | `fingerprint_words_checked=63` | Available and sufficient | Exact `63` required |
| Exact stock process category | `report_category=expected`, `sd_report_eligible=true` | Available and sufficient for constructor classification | Both required |
| DATA-only selection | `selected_hook=DATA_HOOK` | Available and sufficient | Exact match required |
| Site prepared | `required_site_prepared=true` | Available and sufficient | Required |
| Installation succeeded | `installation_succeeded=true`, `final_status=Installed` | Available and sufficient for publication status | Both required |
| Forwarding trampoline ready | `forwarding_ready=true` | Available and sufficient for pre-interaction readiness, not proof of forwarding | Required |
| Callback forwarding observed | `forwarding_observed=false` | Not appropriate at constructor time | Must remain `false`; post-interaction counters provide proof |
| Snapshot mapping ready | `counter_snapshot_ready=true` | Available and sufficient | Required |
| Stock PID | `counter_snapshot_pid` | Available and sufficient only when snapshot-ready | Nonzero and equal to discovered/snapshot PID |
| Process start token | none | Missing from constructor report | Initial verified snapshot must supply it; report alone cannot arm |
| Report publication status | no explicit field | Available but ambiguous | Receipt proves readable bytes, not durable publication; no synthetic field is inferred |
| Text permissions restored | `text_permissions_restored=unknown` | Not appropriate as an arming success claim | Not promoted to evidence; existing indeterminate-state rules remain |

Unknown fields remain bounded and inert. Duplicate fields, malformed lines,
invalid UTF-8, reports over 768 bytes, missing required fields, and mismatches
fail closed. P3.1 does not change the report format.

## State machine

```text
                       valid report + initial B0.2 snapshot
 WAITING ──process──► WAITING ─────────────────────────────► ARMED
    │                    │                                    │
    │ timeout/invalid    │ timeout/invalid                    │ valid one-shot
    ▼                    ▼                                    │ post snapshot
 EXPIRED/FAILED      EXPIRED/FAILED                           ▼
                                                           REPORTED
                                                              
 ARMED ──timeout──────────────────────────────────────────► EXPIRED
 ARMED ──identity/snapshot/counter failure────────────────► FAILED

 REPORTED, FAILED, EXPIRED ──any later operation──────────► rejected terminally
```

`WAITING` has separate process-discovery and post-discovery arming deadlines.
Every live state also has a hard overall deadline. Defaults are bounded host
policy values pending physical measurement: 30 seconds for process discovery,
15 seconds after discovery for arming, 120 seconds overall, 2 seconds to parse
one request, 3 seconds supplied to one snapshot read, and 32 total status or
snapshot operations.

All model deadlines are absolute monotonic instants and **inclusive**: work
completed exactly at the applicable deadline is accepted; the first instant
strictly greater than it expires the model. Before discovery, the applicable
deadline is the earlier of process-discovery and overall lifetime. After
discovery but before `ARMED`, it is the earliest of arming, overall lifetime,
and the per-read deadline. In `ARMED`, it is the earlier of overall lifetime
and the per-read deadline. `tick()`, `handle()`, `observe_process()`, and
`arm()` all enforce the applicable state deadline. Expiry clears discovered
PID, discovery time, and pinned identity; terminal state cannot be rearmed.

`SnapshotReader` now receives call-start and absolute-deadline times and returns
an explicit completion time with its result. Completion before call start is a
source-contract failure; completion after the earliest deadline expires the
model even if the snapshot is otherwise valid. The production adapter must
enforce the deadline during each bounded open/read/identity operation. The host
model can reject a late result but cannot forcibly interrupt a blocked adapter;
that implementation mechanism remains a P3.3 review item.

Entering `ARMED` requires all constructor gates, matching discovered/report
PID, and an initially verified v2 snapshot with a nonzero process-start token.
Initial counters may be zero. The identity is pinned as PID plus start token.

The snapshot operation revalidates that same identity and the full snapshot
contract. `REPORTED` additionally requires nonzero callback invocations and
nonzero successful forwards. Installation without callbacks, or callbacks
without successful returns, is a terminal B1 evidence failure. Identity and
cached state are cleared on every terminal transition.

## Protocol specification

Accepted operations are exactly:

```text
GET /v1/status HTTP/1.1
POST /v1/snapshot HTTP/1.1
```

The parser operates only on an already bounded byte slice and elapsed
monotonic time. It accepts HTTP/1.1, at most 1,024 total bytes and 16 headers,
and exactly one nonempty canonical `Host` plus one optional canonical
`Content-Length: 0`. Header names are case-insensitive. Host values are limited
to ASCII hostname/address characters; controls, whitespace, user-info, and path
characters are rejected. The snapshot request has
an empty body. Status returns only `state=WAITING|ARMED\n`; it does not disclose
PID, start token, filesystem names, errors, or firmware details. A successful
snapshot returns the existing address-free report, never more than 384 bytes.

It rejects unsupported methods/versions, unknown or traversal paths, updater
paths, bodies, nonzero lengths, duplicate or conflicting lengths, any
`Transfer-Encoding`, unexpected headers, obsolete folding, malformed request
lines, incomplete headers, bytes after the first header block, pipelining,
missing/duplicate/invalid hosts, noncanonical lengths, request timeout, and
size/count excess. A snapshot before `ARMED` is rejected;
one accepted retrieval transitions terminally, so duplicates and replay cannot
produce a second report.

The parser intentionally does not implement keep-alive, chunking, content
negotiation, routing parameters, percent decoding, or a general HTTP stack. A
future transport must close each connection and may not concatenate requests
or reinterpret parser failures.

## Validation flow

1. Record one nonzero candidate PID within the discovery deadline.
2. Parse the constructor report within 768 bytes and require every arming gate.
3. Require candidate PID and `counter_snapshot_pid` equality.
4. Ask the injected reader for an initial verified snapshot before its supplied
   absolute deadline and require a completion time within that deadline.
5. Require snapshot PID equality and a nonzero verified start token; pin both.
6. Enter `ARMED`; status reveals only this coarse state.
7. Accept one parsed snapshot operation.
8. Ask the reader to revalidate the pinned PID/start token and stable counters,
   then reject a result completed after the read or overall deadline.
9. Require nonzero invocations and successful forwards.
10. Format no more than 384 bytes, clear identity, and enter `REPORTED`.
11. On any post-arming validation error, clear identity and enter `FAILED`; on
    expiry, clear identity and enter `EXPIRED`.

## Security findings

Protocol-level controls now modeled and tested include strict framing, fixed
method/path allowlists, no bodies or transfer coding, duplicate-length
rejection, no pipelining, small request/header/request-count bounds, injected
deadlines, canonical Host/length handling, checked request counting, coarse
status output, one-shot retrieval, replay rejection, and terminal cleanup.
Incomplete requests must be dropped by the future transport
at the parser deadline; the model performs no waits or retries.

These controls are not authentication. Binding to `192.168.155.1` would limit
the intended interface but does not prove the host is the only reachable peer.
Firewall/routing and interface isolation remain deployment gates. A per-run
capability could mitigate another reachable peer, but no secure channel for
delivering it is established; embedding a static token or exposing it through
unauthenticated status would add no meaningful protection. P3.1 therefore does
not implement a token or authentication service.

One-shot operation limits replay and exposure duration but does not prevent an
unauthorized peer from consuming the single request first. The future transport
must also impose connection, read/write, listener-lifetime, and allocation
bounds around this parser. Running with root privilege remains unacceptable
without a separate least-privilege justification.

## Unverified lifecycle and privilege assumptions

P3.1 establishes none of the following device facts:

- actual `ac50.service` unit, user/group, cgroup, `KillMode`, and restart chain;
- survival of an independent runtime-only unit across the stock restart;
- available systemd hardening and runtime-limit directives;
- snapshot/report ownership and the least UID able to read them plus `/proc`;
- CDC-NCM interface readiness, exact-address bind behavior, and free high port;
- firewall/routing exposure beyond the physical USB peer; or
- stock PID/start-token continuity while CDC-NCM is in use.

No systemd unit or startup script was created. Generic Linux behavior is not
device evidence.

## Host test coverage

Deterministic tests cover actual-report acceptance; missing, duplicate,
malformed, oversized, wrong firmware/count/hook/preparation/installation/
forwarding/snapshot fields; valid and invalid process discovery; all state
timeouts at before/equal/after boundaries; arming expiry without `arm()`; late
reader completion; initial and post-interaction identity; exact snapshot
magic/version/length/reserved/counter/stability rules; eight non-overlapping
read pairs and last-opportunity stability; wrong executable/PID/start token;
checked near-maximum and overflowing counter totals;
zero callbacks/forwards; bounded successful formatting; one-shot and terminal
behavior; request count and non-extension of lifetime; strict methods, paths,
traversal and updater rejection; duplicate/missing/invalid Host, controls,
whitespace, canonical lengths, transfer encoding, header names, version,
framing, pipelining, size, incomplete-request, and timeout rejection.

These are host unit tests, not emulator or hardware evidence.

## Remaining gaps and next milestone

P3.1a is not a deployable relay and deliberately leaves transport I/O, B0.2
adapter linkage, filesystem notification, unit lifecycle, interface binding,
hardening, and capability delivery unimplemented.

The next milestone should be **P3.2 read-only lifecycle discovery**, separately
authorized and performed without the DATA hook or relay. It must resolve the
unit/cgroup, least-privilege reader, interface, bind, firewall, and PID-continuity
gates above. Only after those pass should **P3.3** implement a separately
reviewed AArch64 adapter/transport and runtime-only unit. P3.3 must replace the
fixture decoder with the unchanged B0.2 reader boundary and must not imply
deployment permission.

## Go/no-go

**No-go for B1 physical testing.** P3.1a validates host logic only. Neither P3.2
device discovery, P3.3 implementation, relay deployment, nor DATA-hook execution
is authorized by this milestone.

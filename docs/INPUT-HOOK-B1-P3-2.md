# M0.3b-B1-P3.2 lifecycle discovery preparation

Status: host-only procedure, evidence schema, and offline analyzer. No command
was run on an MPC, and no service, payload, hook, listener, or device file was
created. P3.2a and P3.2b both require new physical authorization.

P3.2c supersedes the missing-command-channel next step with a designed, but not
implemented or authorized, one-shot pre-restart SD autorun collector. It also
hardens parent and USB evidence handling and adds a bounded selected-fact report
schema. See [P3.2c](INPUT-HOOK-B1-P3-2C.md). This does not alter the P3.2b or B1
physical no-go decisions.

## Executive conclusion and evidence baseline

The repository does **not** establish a supported command-access mechanism to
the MPC Sample. USB CDC-NCM reaches the stock updater, but that interface proves
only `GET /version.json` and firmware upload. The generic object client is
apparently unused and its root returned 404. User-content storage is not `/run`.
The stock autorun executes a shell, but its documented path stages files,
changes an `ac50.service` drop-in, reloads systemd, and restarts `ac50`; it is
not passive discovery and must not be repurposed implicitly.

Established physical observations on one stock 1.3.0.12 unit are limited to:

- USB VID:PID `09e8:205c`, audio, MIDI, and CDC-NCM;
- host `192.168.155.8/28`, device/updater `192.168.155.1`;
- the updater HTTP service remains reachable during ordinary stock operation;
- A2--A4 autorun/preload observations, exact stock-process classification, all
  63 firmware words, bounded SD reports, normal controls/audio, and stock-only
  reboot recovery; and
- A3 constructor PIDs/PPIDs, which do not establish the actual systemd unit,
  cgroup, UID, kill policy, or service command chain.

Source establishes that B0.2 would create a mode-0600, process-owned 72-byte
snapshot and a constructor report under `/run`, but neither exists during a
stock-only P3.2a run. Generic systemd behavior and `/run` conventions remain
assumptions until observed on this device.

Therefore the immediate outcome is a reviewable procedure, not executable
device work. Every device command below is **blocked** until a separately
reviewed, read-only command channel is demonstrated. The procedure does not
authorize SSH, Telnet, ADB, a serial shell, developer mode, autorun, or an
undocumented RPC.

## P3.2a — passive discovery

### Phase 0: access gate and stock baseline

1. Boot stock firmware 1.3.0.12 with no autorun media. Through the normal UI,
   confirm projects are saved and UI, audio, DATA, transport, and pads behave
   normally. This changes only ordinary visible application state; do not load,
   save, or edit a project for the investigation.
2. Connect USB normally and confirm the already documented updater/version page
   only if that action has separate permission. Do not issue new endpoints.
3. Identify the provenance and safety contract of any proposed command channel.
   Stop unless it is already supported or separately reviewed as read-only and
   can run only the exact allowlist below without installing or enabling a
   service. At present this condition is not met, so command collection is
   **blocked**.
4. If a future authorization satisfies the gate, record `UI=normal`,
   `AUDIO=normal`, and `CONTROLS=normal` in a small operator-authored stock
   observation file. Do not include serials, MAC addresses, project names, or
   user content.

The stock UI baseline is read-only with respect to system configuration. Stop
on crashes, loss of audio/control, unexpected writes, update prompts, a service
restart, or any request for credentials/elevation not covered by the review.

### Explicit device-command allowlist

All entries are currently **blocked by the missing access mechanism**. If that
gate is later resolved, execute each command at most once, save at most 256 KiB
per output file, and do not substitute flags. `<PID>` must be the decimal,
nonzero `MainPID` returned by the first command; no PID search is permitted.

| Exact command or bounded substitution | Purpose and expected evidence | Privilege / state / side effects | Redaction and stop conditions | Decision supported |
|---|---|---|---|---|
| `systemctl show ac50.service --no-pager --property=Id,LoadState,ActiveState,SubState,Type,ExecStart,User,Group,KillMode,Restart,RestartUSec,TimeoutStopUSec,Slice,MainPID,ControlGroup,FragmentPath,DropInPaths,Requires,Wants,After,Before` | Selected unit lifecycle, restart, dependency, identity, cgroup, and main-PID properties | Normally unprivileged; read-only; systemd query only | Keep only selected properties; redact unexpected environment/credential text. Stop unless loaded/active with one nonzero PID. | Unit contract, ancestry seed, independent-unit review |
| `systemd --version` | PID-1 tooling version for offline feature review | Unprivileged; read-only | Retain first version line only | Runtime-unit/hardening feature candidates |
| `systemd-run --version` | Confirm matching transient-unit client exists; does not create a unit | Unprivileged; read-only | Retain version only; stop if absent or mismatched | P3.2b feasibility only |
| `ps -o pid=,ppid=,uid=,gid=,comm= -p <PID>` | One bounded process row with ancestry and credentials but no argument/environment disclosure | Unprivileged if `/proc` permits; read-only | Retain only numeric IDs and expected command name. Stop on multiple/no rows. | Process and least privilege |
| `ps -o pid=,ppid=,uid=,gid=,comm= -p <PPID>` | One bounded immediate-parent row, where `<PPID>` is taken only from the stock row | Same read-only process query | Analyzer compares parent PID to stock PPID and discards the command name. Do not walk further ancestry. | Immediate ancestry consistency |
| `readlink -e /proc/<PID>/exe` | Exact `/usr/bin/MPC Sample` executable identity | Unprivileged if permitted; read-only symlink lookup | Retain only exact-match boolean in shared report. Stop on mismatch/denial/disappearance. | Process identity |
| `cat /proc/<PID>/stat` | PID and Linux start-time token; bounded one-line proc file | Unprivileged if permitted; read-only | Analyzer retains PID/start token only. Stop on malformed or changed token. | PID-reuse defense and continuity |
| `sed -n -e '/^Uid:/p' -e '/^Gid:/p' /proc/<PID>/status` | Effective UID/GID and proc visibility, with output restricted to two lines | Unprivileged if permitted; read-only | Numeric ID columns only. Stop rather than weakening proc permissions. | Least-privilege reader UID/GID |
| `cat /proc/<PID>/cgroup` | Exact unit/cgroup membership | Unprivileged if permitted; read-only | Retain `ac50.service` path; redact unrelated scopes. Stop on ambiguous membership. | Kill-boundary analysis |
| `findmnt -no TARGET,SOURCE,FSTYPE,OPTIONS /run` | `/run` backing filesystem and mount policy | Unprivileged; read-only | Retain only `/run` row and selected option booleans | Runtime-only storage and reboot cleanup |
| `stat -Lc 'RUN_MODE=%a' /run` | `/run` directory mode | Unprivileged; read-only metadata | Retain numeric mode only | Traversal/access model |
| `stat -Lc 'RUN_UID=%u' /run` and `stat -Lc 'RUN_GID=%g' /run` | `/run` directory owner IDs | Unprivileged; read-only metadata | Numeric IDs only | Least-privilege access model |
| `stat -Lc 'RUNTIME_UNIT_DIR_MODE=%a' /run/systemd/system` | Whether the standard runtime unit directory exists and its mode | Unprivileged if traversal permits; read-only metadata | Mode/existence only. Absence is unknown feasibility, not permission to create it. | Runtime-unit staging assumption |
| `ip -o -4 addr show` | Device-side IPv4 assignment and interface | Usually unprivileged; read-only | Analyzer retains `192.168.155.1` mapping and count of other addresses only | Exact-address binding candidate |
| `ip -o link show dev <IFACE>` | Link state for the one interface carrying `192.168.155.1`; `<IFACE>` comes only from the preceding output | Usually unprivileged; read-only netlink query | Remove MAC address before sharing; stop on zero/multiple expected address mappings | CDC-NCM readiness |
| `ip -4 route show table main` | Route for `192.168.155.0/28` and possible broader paths | Usually unprivileged; read-only | Retain expected subnet/interface; redact unrelated gateways/addresses | Route exposure review |
| `ss -H -ltn` | Existing TCP listeners and collision/wildcard exposure | Usually unprivileged; read-only; no process lookup | Retain exact-address and wildcard ports; redact other addresses. Stop if output exceeds bound. | Port selection and pre-existing exposure |
| `nft --version` and `nft list tables` | Identify firewall implementation/table names without dumping rules | May be denied; read-only query | Table names may be sensitive; keep local pending review. Denial remains unknown. | Whether a later exact-chain query can be designed |

No `systemctl cat`, `journalctl`, recursive `find`, `ls -R`, `/proc` enumeration,
`ps` without `-p`, `ss -p`, `netstat`, `lsof`, `nft list ruleset`,
`iptables-save`, environment dump, memory read, debugger, signal, file write,
permission change, service state change, or port scan is allowlisted.

Firewall semantics cannot yet be collected safely because table/family/chain
names are unknown. After `nft list tables` is reviewed locally, a new procedure
may authorize one exact `nft list chain <family> <table> <chain>` command. It is
not implicitly allowed here. If no firewall tool is present or readable, record
the gate as unknown rather than assuming unrestricted or isolated traffic.

### Collection order and consistency checks

If the blocked access gate is later resolved, use this order:

1. Record the stock UI/audio/control baseline.
2. Query the selected systemd properties and version. Bind `<PID>` exactly once
   to `MainPID`; stop if zero, changing, inactive, or unexpected.
3. Collect the one process row, executable link, stat, selected status lines,
   and cgroup. Repeat only `/proc/<PID>/stat` once at the end to establish that
   PID/start token did not change during passive collection.
4. Collect `/run` mount and directory metadata. Do not expect or search for a
   B0.2 snapshot/report on a stock boot.
5. Collect interface, IPv4, route, and listener summaries. Do not bind, connect,
   scan, or request an HTTP endpoint.
6. Collect only firewall version/table names if readable. Do not broaden the
   rule query.
7. Reconfirm UI/audio/controls. Record bounded before/after PID/start fields in
   the stock observation file.
8. Remove unique identifiers and unrelated addresses locally, then run the
   offline analyzer. Preserve raw captures privately only long enough for
   review; do not commit them.

The procedure is passive with respect to service/device state, but reads may
update host-side terminal history and capture-file timestamps. It must not be
run as root merely to bypass a denial. A denial is valuable least-privilege
evidence and a stop for that command.

## Offline evidence analyzer

`tools/b1_p3_2_inspect.py` reads only explicitly named local files. Each file is
limited to 256 KiB and must be valid UTF-8. It never executes a command, opens a
network connection, traverses a directory, or reads an implicit path.

Accepted categories are independent and repeatable:

```sh
python3 tools/b1_p3_2_inspect.py \
  --systemd /path/to/systemd.txt \
  --process /path/to/process.txt \
  --cgroup /path/to/cgroup.txt \
  --mount /path/to/run-mount.txt \
  --network /path/to/network.txt \
  --sockets /path/to/listeners.txt \
  --firewall /path/to/reviewed-firewall.txt \
  --stock /path/to/stock-observations.txt \
  --collection /path/to/p3-2c-selected-facts.txt \
  > /tmp/mpc-b1-p3-2-analysis.json
```

The JSON contains source byte counts/digests but not filenames; sanitized facts
classified as `confirmed`, `inferred`, `unknown`, or `conflicting`; malformed
and unexpected-field counts; missing evidence categories; nine architecture
gates; and explicit limitations. Unrelated addresses, interface names,
credentials, environment values, arguments, and identifiers are omitted or
counted. Multiple files in one category are combined so contradictions are not
hidden by file boundaries. Absence never becomes a negative capability claim.

The optional stock file accepts only:

```text
UI=normal
AUDIO=normal
CONTROLS=normal
PID_BEFORE=<decimal>
PID_AFTER=<same decimal>
START_BEFORE=<decimal ticks>
START_AFTER=<same decimal ticks>
```

This is operator-authored bounded observation metadata, not command output.

The optional P3.2c collection file uses the versioned, 4096-byte selected-fact
contract specified in P3.2c. Duplicate or contradictory parent rows now conflict
instead of being accepted. USB confirmation requires the exact `/28`, one
matching address/route interface, and separately supplied link-state evidence;
an address line alone leaves link state unknown.

## Evidence and decision matrix

No gate is currently passed. `Inferred` means evidence may justify the next
review; it is not equivalent to demonstrated behavior.

| Gate | Required evidence | Acceptable observation | Reject / stop | Unknown | Passive? / active requirement |
|---|---|---|---|---|---|
| Independent systemd unit feasibility | Version, runtime directory, service cgroup, transient client, unit policy | Runtime units supported in principle and distinct unit/cgroup design review succeeds | No transient support, forced same cgroup, persistent-only mechanism | Missing/denied/version not reviewable | P3.2a can infer; P3.2b must demonstrate |
| Survival across `ac50` restart | Helper PID/cgroup before and after exactly one restart | Same helper PID/start token remains active while new stock PID starts normally | Helper killed/reparented ambiguously, restart loop, stock abnormal | No active run | P3.2b only |
| Least-privilege snapshot access | Stock UID/GID, `/run` metadata, future mode-0600 owner, proc reads | Same non-root UID can read exact report/snapshot and required proc files | Requires root, chmod/chown, broad proc access, or unrelated files | Stock-only boot has no snapshot | P3.2a informs; later zero-hook fixture proves |
| Constructor-report accessibility | Exact bounded report ownership/mode and reader access | Reviewed UID reads only expected report | Missing, ambiguous, permission workaround | Report absent stock-only | Later zero-hook fixture, not P3.2a |
| USB-only network binding | `192.168.155.1` on one CDC-NCM interface, route, collision check | Exact-address high-port bind is later demonstrated without wildcard fallback | Address on wrong/multiple interface, collision, wildcard required | Address/route missing | P3.2a maps; P3.3/authorized test binds |
| Firewall/routing isolation | Relevant exact chain/rules, routes, host reachability boundary | Traffic demonstrably limited to intended USB peer/interface | Broader route/interface exposure without control | Denied/unreviewed rules | Passive review plus separately authorized bounded connectivity test |
| PID/start-token continuity | Exact executable, MainPID, `/proc/stat` before/after | Same pair during passive collection; P3.2b records intentional stock replacement separately | Unexplained change/disappearance/mismatch | Access unavailable | Passive window in P3.2a; restart behavior P3.2b |
| Runtime-only cleanup | tmpfs `/run`, transient unit state, post-exit/reboot absence | No enabled/persistent unit; helper exits; runtime state disappears | Device-disk write, enablement, restart policy, residual process | tmpfs alone cannot prove exit cleanup | P3.2a infers reboot property; P3.2b demonstrates |
| Stock application stability | UI/audio/controls and process identity around collection/run | All normal, no loop/crash/write; expected PID behavior for phase | Any regression or unexpected state change | Operator observation missing | P3.2a baseline and P3.2b regression |

## P3.2b — proposed zero-hook lifecycle experiment

P3.2b is designed here but is neither implemented nor authorized. It may be
considered only after P3.2a establishes the exact unit, systemd version/client,
cgroup, UID, `/run`, interface, and baseline facts without a stop condition.

The least invasive candidate uses the device's observed `systemd-run` and a
reviewed existing bounded sleep executable; it creates no file, listener,
network endpoint, hook, preload, or firmware patch:

1. Save projects, use stable power, remove autorun media, and establish stock
   UI/audio/control plus PID/start-token baseline.
2. Start one uniquely named transient service with `Restart=no`, a fixed
   `RuntimeMaxSec`, high scheduling niceness, no inherited `LD_PRELOAD`, and a
   fixed existing sleep command. The exact command and supported properties
   require a separate reviewed P3.2b procedure after P3.2a; no shell command,
   arbitrary argument, or uploaded executable is allowed.
3. Record only its unit name, MainPID, start token, and cgroup. Stop unless it is
   independent of `ac50.service` and bounded to exit automatically.
4. Perform exactly one separately authorized `systemctl restart ac50.service`.
   This is the sole intentional stock interruption. Do not repeat on failure.
5. Verify the transient helper retained the same PID/start token and unit while
   `ac50` acquired one new expected PID/start token and returned to normal UI,
   audio, controls, and updater/CDC-NCM behavior.
6. Allow the helper to exit by its deadline. Verify it has no process, restart,
   enabled state, runtime artifact, listener, or device-disk file. Use a normal
   shutdown and stock-only reboot as final rollback and re-run stock checks.

Stop immediately on an unexpected process, helper cgroup inside `ac50`, helper
death before restart completes, more than one stock restart, service failure,
UI/audio/control regression, write outside runtime state, inability to observe
identity, or inability to return to stock. Do not substitute a background shell
child: it would not test the required independent unit boundary.

P3.2b demonstrates lifecycle isolation only. It does not approve a relay,
network listener, B0 reader, preload, or DATA hook.

## Rollback and authorization boundaries

- P3.2a command collection requires separate physical/read-only authorization
  and an established access path; neither exists today.
- P3.2b requires a later explicit authorization accepting one stock restart and
  transient-unit creation. Architecture text is not execution permission.
- Any helper that needs installation, upload, root escalation, persistence,
  networking, or a shell is outside this design and needs a new safety review.
- Normal shutdown, transient-helper timeout, removal of any test media, and a
  stock-only reboot are the final recovery boundary. No firmware restore or
  partition operation is permitted.

## Recommendation and go/no-go

Review P3.2c's safer pre-restart autorun design next. It avoids inventing a
general command channel and collects the baseline before any service change,
but implementation, packaging, and a single physical run remain separate
authorization gates. P3.2b must not proceed from design evidence alone.

**No-go for P3.2 device execution, P3.2b, relay implementation/deployment, and
B1 physical hook testing.**

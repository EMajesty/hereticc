# M0.3b-B1-P3.2c zero-hook lifecycle evidence design

Status: host-only design and offline-analyzer hardening. No MPC was contacted;
no autorun package, executable collector, service, hook, listener, or firmware
artifact was created. Physical execution remains unauthorized.

P3.2d implements the selected-fact normalization, identity checks, deterministic
report, deadline model, host-only no-clobber publication boundary, inert
manifest generator, and static safety inspection as a library-only Rust model.
It does not implement device acquisition or an autorun entry point. See
[P3.2d](INPUT-HOOK-B1-P3-2D.md).

## Executive conclusion

The least invasive useful experiment is a **new, one-shot pre-restart autorun
collector**. A future separately authorized package would use the already
observed stock SD autorun entry point, collect only selected baseline facts,
publish one bounded report to the verified SD mount, and exit. It would not copy
a preload, create a drop-in, reload systemd, restart `ac50.service`, start a
helper, or open a listener.

This design can replace P3.2's currently blocked arbitrary command-channel
procedure for baseline evidence. It cannot prove that an independent helper
survives an `ac50.service` restart. That remains a distinct, later zero-hook
lifecycle experiment. No independent relay design may be approved from the
baseline report alone.

## Evidence baseline and A4 execution timeline

The generated A4 script at `device/m03b-a/build/inspect/autorun/autorun.sh` and
the A4 constructor/report source establish this order:

1. The stock application recognizes an accepted SD autorun and invokes its
   shell. Source analysis says the application runs the script through `sh`;
   the exact call-site time, shell PID/cgroup, working directory before the
   script changes it, environment, UID, and GID are not recorded.
2. The script resolves `DIR` from its own path. Its later file access is
   therefore independent of the caller's working directory. The script does
   not sanitize or record the inherited environment in the inspected generated
   form.
3. `/run` is necessarily available when the script copies the library there;
   the mount type, options, ownership, and earlier availability are unknown.
4. The stock application is running when it launches autorun. Whether the
   systemd MainPID is that process and whether it remains stable for the whole
   pre-restart collection window are not yet observed.
5. The script hashes the first 4096 bytes of exact `/usr/bin/MPC Sample` and
   exits inertly on mismatch.
6. It copies the preload to `/run`, sets mode 0755, creates
   `/run/systemd/system/ac50.service.d`, and writes the runtime-only
   `LD_PRELOAD` drop-in.
7. It runs `systemctl daemon-reload`.
8. Its final command is `systemctl restart ac50`. The inspected script has no
   command after that restart. Because the application owns the autorun
   lifecycle, continuation after service teardown is unsupported.
9. During the replacement process startup, the dynamic loader invokes the A4
   constructor in inherited preload contexts. A3 physically observed the chain
   409/1, 410/409, 411/410 and a repeat 411/410, but those observations do not
   identify the service definition or cgroups.
10. In exact `/usr/bin/MPC Sample`, A4 validates executable identity and 63
    firmware words. The constructor writes a bounded `/run` report and directly
    publishes a no-clobber bounded report to one rigorously validated writable
    exFAT SD mount. Constructor completion timing relative to UI readiness is
    unknown.
11. A stock-only reboot without autorun media removes `/run` changes. A4
    physically observed normal UI/controls/audio and stock-only recovery on one
    1.3.0.12 unit. No evidence establishes shell continuation or a surviving
    child across restart.

Thus pre-restart collection has a real execution opportunity before step 6,
but the existing A4 script cannot be reused unchanged: it intentionally stages
a preload and restart. A diagnostic package must be a separate, reviewed
artifact whose control flow ends after publication.

## Collection-point comparison

| Candidate | Useful evidence | Device changes and assumptions | Failure/rollback | Assessment |
|---|---|---|---|---|
| A. Pre-restart one-shot | Original unit properties; MainPID/process identity, credentials and cgroup; `/run`; systemd version; USB address/link/route; relevant listener conflicts | Executes a bounded SD script/collector and writes one SD report. Does not require a service change, preload, restart, helper survival, or listener. Requires the autorun shell to have sufficient read authority and the stock PID to remain stable during collection. | Fixed deadline; fail closed; exit without service action; remove card and stock-only reboot | **Preferred baseline** |
| B. Constructor-time | Exact loaded process and constructor-time identity; existing bounded report path | Requires preload staging, daemon-reload and stock restart. Constructor I/O is physically supported, but external commands, waiting, process enumeration, systemd queries, and network inspection do not belong in a loader constructor. Changing B0/A4 is forbidden here. | Startup-path failure can affect stock launch; rollback needs card removal/reboot | Reject for lifecycle baseline |
| C. Independent helper | Could directly prove a distinct cgroup/process survives restart and later publish results | Requires a new runtime unit/process, understood privileges, one intentional restart, bounded supervision and output coordination. Every decisive lifecycle property is presently unverified. A background shell child is not sufficient. | More failure states; must time out, remove runtime state and verify stock recovery | Defer until A passes and receives separate design/authorization |

Candidate A minimizes UI/audio risk and persistent state. It still is not
“read-only”: running autorun is transient execution and report publication is a
persistent SD write. It is passive only with respect to stock service/process
configuration.

## Bounded evidence contract

The proposed UTF-8 line format is `neurogenic-b1-p3.2c-v1`. It permits at most
32 `key=value` lines, 128 bytes per value, and 4096 bytes total. Every field is
selected and normalized by the future collector; raw command output is not a
report format.

Required envelope fields:

```text
evidence_schema=neurogenic-b1-p3.2c-v1
collection_phase=pre-restart
collection_status=complete|partial|failed
restart_observed=false
```

Optional selected facts are:

```text
systemd_version=<decimal major version>
service_type=<safe token>
service_kill_mode=<safe token>
service_restart=<safe token>
service_main_pid=<decimal>
service_cgroup=<bounded absolute cgroup path>
process_executable=stock-exact|other|unknown
process_pid=<decimal>
process_parent_pid=<decimal>
process_start_ticks=<decimal>
process_uid=<decimal>
process_gid=<decimal>
process_cgroup=<bounded absolute cgroup path>
run_filesystem=<safe token>
run_read_write=true|false|unknown
run_mode=<octal digits represented as a safe token>
runtime_unit_dir_mode=<octal digits represented as a safe token>
usb_interface=<safe token>
usb_ipv4=192.168.155.1/28
usb_link_up=true|false|unknown
usb_route=192.168.155.0/28
usb_route_interface=<safe token>
listener_exact_ports=none|unknown|<comma-separated ports>
listener_wildcard_ports=none|unknown|<comma-separated ports>
```

The report excludes environment variables, arguments, MAC addresses, serials,
project/sample data, unrelated processes and addresses, credentials, logs, and
arbitrary filesystem contents. The future collector must emit `partial` rather
than broaden collection when a selected source is absent or denied.

`tools/b1_p3_2_inspect.py --collection REPORT` validates this envelope offline.
It rejects wrong bounds, malformed/duplicate values, mismatched service/process
PID or cgroup, mismatched USB address/route interfaces, wrong address/prefix,
and a restart claimed during `pre-restart`. The structured analyzer output
remains evidence classification, not proof that a device-side producer is
trustworthy.

The existing category inputs remain useful for reviewed raw captures. Their
hardening now gives parent consistency `confirmed` only for one exact PPID
match, `conflicting` for contradictions, and `unknown` for missing/ambiguous
rows. USB mapping requires exact `192.168.155.1/28`, the `/28` route on the same
interface, and separate explicit link-state evidence. Unrelated addresses do
not affect that decision.

## Proposed future physical procedure

This procedure is design only and requires explicit authorization plus a later
implementation review.

### Preconditions and package review

1. Confirm MPC Sample VID:PID `09e8:205c`, stock firmware 1.3.0.12 through the
   documented UI/updater version read, stable power, saved work, and normal UI,
   audio, pads, DATA, and transport.
2. Use a spare verified SD card containing no other autorun. Inspect the final
   manifest, hashes, exact firmware guard, report schema/bounds, collector
   source/binary provenance, and script line by line on the host.
3. Require the package allowlist to contain only autorun metadata, the bounded
   one-shot collector/script, checksums, and an empty no-clobber results target.
   Reject a preload, drop-in, unit file, reader, hook, listener, firmware asset,
   updater operation, background command, loop, or post-collection service
   command.
4. Require a hard monotonic deadline, one run/report slot, `umask 077`, exact SD
   mount validation, no-clobber publication, and fail-closed behavior. The
   exact future implementation must be host-tested before device approval.

### Single authorized run

1. Boot stock without the test card and record the normal baseline.
2. Insert the prepared card once and invoke only the documented autorun path.
   Do not retry if no report appears.
3. Stop immediately on an update prompt, UI/audio/control regression, stock
   restart, visible crash, repeated autorun, unexpected file, timeout, or any
   service-state change.
4. After bounded completion, use normal shutdown; remove the card; boot stock
   only; recheck UI/audio/controls and firmware version.
5. On the host, verify package hashes and that exactly one bounded report was
   created. Run the offline analyzer. Treat missing, partial, conflicting, or
   invalid evidence as a failed experiment, never permission to retry or relax
   collection.

Expected writes are one no-clobber SD result and unavoidable filesystem
metadata. The preferred design creates no `/run` artifact except transient
process state, no drop-in, and no device-disk file. Exact shell/collector
ownership remains a pre-run implementation question. Recovery is card removal,
normal shutdown, and stock-only reboot; no firmware restoration is permitted.

## Safety classification and failure analysis

| Operation | Classification | Bound / stop rule |
|---|---|---|
| Host inspection of package and report | Passive observation | Offline only; no device requests |
| Stock autorun invocation | Transient modification/execution | Once; fixed deadline; no child/helper |
| Selected systemd, proc, mount, network and socket reads | Passive observation | Exact sources only; no enumeration beyond selected MainPID/interface |
| Report publication | Persistent modification to removable SD only | One no-clobber file, at most 4096 bytes |
| Drop-in creation, daemon-reload, service restart | Not part of preferred design | Any occurrence is a stop/failure |
| Independent unit/helper | Not part of P3.2c run | Requires separate P3.2b authorization |

Denial, disappearing/changing MainPID, inconsistent identity, unexpected
privilege needs, output overflow, malformed source, address ambiguity, write
failure, or deadline expiry produces a partial/failed bounded report and exits.
The design must never change permissions, elevate privileges, signal a process,
read memory, enumerate projects, retry indefinitely, or fall through into the
existing A4 restart sequence.

## Remaining gates and next milestone

The preferred baseline can establish the actual unit/process/filesystem/network
facts and whether autorun can read them with its existing authority. It cannot
establish independent-unit creation, survival across restart, post-restart
publication, relay bind success, firewall isolation, snapshot ownership, or
stock stability during an active lifecycle experiment.

P3.2d completed the host-only collector core and inert review tools. The next
milestone must implement and bound the actual AArch64 observation/publication
adapters, reproduce the complete A4 SD identity checks, and statically inspect
cross-built artifacts without yet producing a device-recognized autorun tree.
Packaging and any single physical run remain separate authorization gates. Only
after a successful baseline report should a new P3.2b design specify an
independent zero-hook helper and exactly one restart.

**No-go for device execution, P3.2b, relay deployment, DATA-hook deployment,
and B1 physical hook testing.**

# B1-P3.2l collector exit-1 diagnosis

Status: host-complete diagnostic package. Physical execution remains owner-only.

## Physical evidence and leading diagnosis

On stock firmware 1.3.0.12, P3.2k reported firmware and collector hash passes,
successful RAM staging, one attempted invocation, exit status 1, no lifecycle
report, and successful cleanup. This proves the wrapper path through child
return, but does not by itself prove that the ELF entry point ran.

The audit found a concrete early collector defect: `/proc/mounts` escapes spaces
as `\040`. The physically successful shell wrappers compare that encoded mount
field with an encoded SD path, while the Rust collector previously rejected the
backslash and would have tried to open the encoded spelling. P3.2l decodes the
four standard mount escapes, then still requires exactly `/dev/mmcblk1p1`,
exFAT, writable options, one `/media/<component>` path, no slash/control/
backslash in the decoded component, `O_NOFOLLOW`, stable device/inode, and exFAT
filesystem magic. This corrects parsing without weakening mount identity.

This is the leading source-level failure candidate if the physical volume label
contains a space. Without the device mount row, it is not claimed as the proven
P3.2k cause. The next package also adds bounded stage evidence.

## Exit-1 audit

The P3.2f main program maps every `AcquireError` and `CollectionError` to status
1 and wrote nothing before final report publication.

| Boundary | Exit-1 paths | Current evidence |
|---|---|---|
| ELF startup | not a Rust exit path; loader failure normally appears to the shell as 126/127 | AArch64/loader/GLIBC host-inspected; physical entry unknown |
| Firmware | open/read/short/hash mismatch/deadline | wrapper passed the same prefix; collector check still separate |
| Initial SD | mount read/UTF-8/source/fs/options/path decode/open/fstatfs/identity | shell SD validation passed; encoded-path defect fixed; Rust validation physical result unknown |
| Service/process | missing/failing `systemctl` or `systemd`, malformed version/MainPID, denied or malformed exact `/proc/<PID>` records | utilities, output, privileges, and exact process facts unknown physically |
| Optional runtime/network | deadline/containment/oversize/invalid UTF-8 are fatal; missing/failing `ip` is partial | host-tested only |
| Collection | service/process/cgroup/executable inconsistency, bounds, deadline | host-tested only |
| Publication | mount change, descriptor/magic/identity mismatch, existing file, create/write/fsync failure | same SD writable physically; descriptor-relative path untested physically |
| Post-publication | final deadline expiry | could return 1 with a report already present; P3.2k had no report |

## Bounded stage evidence

The collector now best-effort writes exactly one whitelisted token at a time to
`/run/neurogenic-b1-p3-2l/collector-stage.txt`. The wrapper creates that
exclusive volatile directory, reads only the first line, accepts only this
allowlist, records it in the SD marker, removes the status file and collector,
and returns the collector status. Unknown content becomes `invalid`; raw content
is never copied.

Ordered tokens:

```text
entry-reached
firmware-passed
sd-validation-passed
service-process-acquired
acquisition-passed
collection-passed
report-publication-attempted
report-publication-failed
report-publication-passed
```

No token means executable entry was not observed or the volatile write itself
failed. `sd-validation-passed` followed by exit 1 localizes failure to service/
process acquisition. `service-process-acquired` means fixed service commands and
exact PID records were read, not that cross-field validation passed;
`collection-passed` confirms that validation. The lifecycle report schema is
unchanged.

## Package

Location: `target/b1-p3-2l/final-a/autorun/`.

| File | SHA-256 |
|---|---|
| `PACKAGE-MANIFEST.json` | `7687a5f2357427d5e12cc6c326bb58fcb3d7b911e785f2aee1de5d3722be8827` |
| `SHA256SUMS` | `8d740d6d815d97012538461547971cfa41695ce839493383955b52058e29ec4b` |
| `autorun.check` | `d3c0363b311bef9fbe60cf8862167bea3fda6d2c5a526227345ec4ad70d89903` |
| `autorun.json` | `c6559e79272a1bc4f36704231b3cc17be7561cb44faced37fab0c8c9262b1f7e` |
| `autorun.sh` | `cb00a9347339a1c13f129ffee3854992489adf54ca4d78ec26e16197470f39fe` |
| `p3_2l_collector` | `364db395b23823233b70024a66b2f4646ddfe8fc01ca07af451ae4cc8034e01c` |

Ordered-content package digest:
`34ea329ad09c17569619064b62652b6d764ce1ca15bf9f5e39322caa718e574f`.
Two generations are byte-identical and strict inspection passes.

Expected evidence is `b1-p3-2l-startup/marker.txt` (or an interrupted
`marker.partial`) plus the unchanged `b1-p3-2c-report.txt` only on collector
success. The marker adds `collector_stage=<allowlisted-token|not-observed|invalid>`.

## One-run owner procedure

After separate authorization, preserve P3.2j/P3.2k evidence, verify normal stock
UI/audio/pad/DATA behavior and all six hashes, and ensure the P3.2l marker paths
and lifecycle report names are absent. Copy only the reviewed `autorun/` tree to
the dedicated SD root, safely eject, insert once, and boot once. No prompt is
expected. Observe for 30 seconds; stop for regression, repeat behavior, restart,
or failure to regain control, and do not retry. Shut down normally, preserve the
SD, inspect the marker/report offline, then perform a stock-only recovery boot.

No hook, preload, service operation, listener, firmware modification, internal
persistent write, background worker, or retry exists. B1 hook testing remains
unauthorized.

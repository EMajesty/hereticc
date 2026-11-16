# B1-P3.2k RAM-staged zero-hook collector

Status: host-complete package candidate. Physical execution was not performed
and remains owner-only.

## Evidence and purpose

The owner ran P3.2j on stock firmware 1.3.0.12. Its complete marker was:

```text
schema=neurogenic-b1-startup-v1
autorun_entered=true
directory_valid=true
firmware_guard=pass
collector_invocation=deferred
```

This physically confirms autorun recognition, shell entry, package-directory
resolution, the exact 4096-byte firmware-prefix guard, and SD marker
publication. The earlier P3.2g no-report result is therefore narrowed to its
collector path or later behavior. P3.2g directly invoked an ELF from the
documented `noexec` SD mount. P3.2k changes only that integration boundary: the
reviewed P3.2f collector bytes and observation logic are unchanged.

## Established staging and selected lifecycle

Physically successful A4 copied a hash-checked payload to a fixed `/run` path,
set mode 0755, and used it from volatile storage. Its preload, systemd drop-in,
daemon reload, service restart, and report polling are deliberately excluded.

P3.2k uses this fixed lifecycle:

1. validate the same SD path/mount boundary physically confirmed by P3.2j;
2. reserve `b1-p3-2k-startup/` on SD and publish the initial partial marker;
3. validate the exact stock firmware prefix;
4. validate the packaged collector as a regular, non-symlink file with SHA-256
   `58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c`;
5. require the collector report and partial-report names to be absent;
6. exclusively create `/run/neurogenic-b1-p3-2k` with `mkdir`; an existing
   file, directory, or symlink is terminal;
7. copy to `/run/neurogenic-b1-p3-2k/p3_2f_collector`, set mode 0700, and
   revalidate type, executability, and SHA-256;
8. invoke that fixed path once as a foreground child;
9. retain the exact child status, classify report presence, remove the staged
   file and directory, finalize the marker, and exit with the child status.

The shell does not use `exec`, because replacement would prevent post-exit
cleanup and marker finalization. It does not use a background worker. A normal
exit removes the staged bytes. Interruption, an uninterruptible syscall, or
cleanup failure can leave the fixed directory in `/run`; reboot clears it.
Automatic retry is prohibited.

The collector has its existing ten-second monotonic deadline. As already
documented for P3.2f, that deadline cannot forcibly interrupt every filesystem
or kernel syscall. The wrapper adds no unverified `timeout` utility or watchdog.
Because the stock autorun owner is believed to wait for the foreground shell,
UI responsiveness during collector execution remains a physical observation
gate.

## Marker and report

Marker paths are distinct from P3.2j:

```text
b1-p3-2k-startup/marker.partial
b1-p3-2k-startup/marker.txt
```

The fixed, bounded schema is:

```text
schema=neurogenic-b1-ram-stage-v1
autorun_entered=true
firmware_guard=pass|fail|not-reached
collector_file=pass|fail|not-reached
ram_staging=pass|fail|not-reached
collector_invocation=attempted|not-reached
collector_exit=not-run|0..255
report_publication=present|absent|partial|blocked-existing|not-checked
cleanup=pass|fail|not-reached
```

`attempted` does not claim that the dynamic loader entered the executable. Exit
126/127 with no report is consistent with an execution or loader failure. A
nonzero exit with no report means invocation returned failure but does not by
itself localize acquisition. Only `b1-p3-2c-report.txt` is the lifecycle report;
the stage marker is not a substitute. A partial marker or report is failure
evidence, not a valid report.

## Package

Host package: `target/b1-p3-2k/final-a/autorun/`.

| File | SHA-256 |
|---|---|
| `PACKAGE-MANIFEST.json` | `db8aaea86b57b252d71079ba95396d3c5ec1837c2c2e95211cf34f5e029381e5` |
| `SHA256SUMS` | `f93bfb6f2b64d46bada0b94f0fa438e2a82bcae1c86a6b4d1980db0396ace75e` |
| `autorun.check` | `edde410b7c63619e4ca6ec6a37ec5febe6d0b0621965565c93ffcd1db07454f6` |
| `autorun.json` | `f9107d656d059c95e614204e5c1545b3590824a8fedbf00bc184b264f9d6fe56` |
| `autorun.sh` | `78eba0086a8f040d62b62149bf4e9bb7d936b9b59e7c1b767f26e34426e99cbf` |
| `p3_2f_collector` | `58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c` |

Ordered-content package digest:
`336c383d7b346944029929edfbb0adf346cdd4d167de0e13ed2a51fa0c829fe3`.
Two generated trees are byte-for-byte equal and the strict inspector passes.

Generation and inspection:

```sh
python3 -B tools/b1_p3_2k_package.py --repo . \
  --collector target/aarch64-unknown-linux-gnu/release/p3_2f_collector \
  --output target/b1-p3-2k/final-a \
  --revision 01c925f89d9b46ae9045b64459f4fc901015f324
python3 -B tools/b1_p3_2k_package.py --repo . \
  --inspect target/b1-p3-2k/final-a
```

The inspector enforces the exact inventory, recognition bytes, firmware and
collector hashes, modes, fixed `/run` path, one foreground invocation, cleanup
structure, and prohibited-operation exclusions.

## Safety and failure behavior

There is no preload, hook, service control, systemd configuration, listener,
firmware operation, process-memory modification, internal-storage write,
background process, or retry. Device writes are one bounded marker and, only if
the unchanged collector succeeds, its fixed no-clobber report on SD. `/run` is
volatile.

Host simulations cover successful staging/report/cleanup; missing collector;
existing and symlink stage paths; copy and permission failures; firmware
mismatch; missing loader; nonzero and internal-deadline-style exits; cleanup
failure; repeated invocation; and missing/partial reports. They use temporary
host fixtures only.

## Owner-only physical procedure

After separate owner authorization:

1. preserve the P3.2j evidence and confirm a normal stock-only 1.3.0.12 boot,
   UI, audio, pads, and DATA operation;
2. verify all six hashes above and copy only the reviewed `autorun/` directory
   to the dedicated exFAT SD root; confirm that `b1-p3-2k-startup/`,
   `b1-p3-2c-report.txt`, and `.b1-p3-2c-report.partial` are absent;
3. safely eject, insert once, and perform one automatic autorun boot; no prompt
   is expected;
4. observe controls and audio for 30 seconds. Stop for repeated autorun,
   restart indication, regression, or failure to regain control. Do not retry;
5. shut down normally, remove and preserve the SD, inspect the P3.2k marker and
   lifecycle report offline, then perform a stock-only recovery boot and repeat
   UI/audio/pad/DATA checks.

Physical execution is not performed or authorized by this host milestone. B1
DATA-hook testing remains no-go.

## Subsequent physical result

The owner later obtained a complete P3.2k marker with firmware, collector-file,
and RAM-staging passes; invocation attempted; collector exit 1; report absent;
and cleanup pass. This physically confirms the wrapper lifecycle but not ELF
entry. P3.2l audits the collapsed exit paths, corrects escaped `/proc/mounts`
path decoding, and adds bounded volatile stage evidence.

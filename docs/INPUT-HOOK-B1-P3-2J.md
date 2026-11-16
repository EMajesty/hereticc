# B1-P3.2j silent-autorun diagnosis

Status: host-complete marker-only package. Physical execution is owner-only and
was not performed during this milestone.

## First P3.2g result

On stock firmware 1.3.0.12 the first physical P3.2g run had the correct
top-level `autorun/` directory and all six expected files. The MPC booted and
remained responsive, but after about one minute there was no
`b1-p3-2c-report.txt` and no previously observed startup hitch. This is
**inconclusive**: report absence does not prove whether the stock loader entered
the shell. The unchanged P3.2g package must not be retried.

## Actual autorun contract

The pinned SDK generator and the pinned minimod reverse-engineering record
establish the following for firmware 1.3.0.12:

- the exFAT card root contains exactly a directory named `autorun`;
- `autorun/autorun.json` selects a path relative to that directory;
- `askQuestion:false` executes automatically. Message and button strings do not
  imply a prompt;
- the stock application's handler—not `az01-script-runner.service`—validates
  the package and contains the call form `sh "%s"`;
- that form supplies one script path and no script arguments. Under POSIX `sh`,
  `$0` is that supplied path. Available reversing indicates the resolved SD
  script path is supplied, but the caller's working directory is not established;
- the app is the autorun owner. A2–A4 demonstrate that accepted scripts execute,
  while exact blocking/wait semantics are not fully recovered. A4's visible
  restart/hitch is physical evidence of execution, not a general prompt;
- `autorun.check` is 64 lowercase hexadecimal characters with no newline:
  `sha256(autorun.json || autorun.sh ||
  "f7bed1c9-7e90-4731-9194-c996666b4331")`;
- only JSON and the selected shell bytes plus that salt are covered. Payloads,
  manifests, and checksum inventories are not covered by the stock check.

The previous P3.2g/P3.2h prompt instructions were corrected accordingly. The
working directory remains unknown and the new script never depends on it.

## Ranked hypotheses

1. **Collector execution from a `noexec` SD failed.** Recovered device evidence
   says the SD is mounted `noexec`; working A4 copies loadable code to `/run`,
   while P3.2g directly executed `p3_2f_collector` from SD. This best explains a
   normal boot, no hitch, and no report.
2. **The package was not accepted.** Correct names alone do not prove copied
   bytes, modes, JSON/check agreement, or loader recognition.
3. **The shell entered but `$0`/directory or a fixed utility check failed.** The
   P3.2g wrapper was silent and published nothing before those gates.
4. **The collector started but rejected the SD mount.** Its `/proc/mounts`
   parser requires a single safe `/media/<component>` record; encoded spaces or
   unexpected physical mount identity fail closed before publication.
5. **Firmware/hash utility failure.** Possible but weaker: the displayed
   firmware and reviewed prefix are 1.3.0.12, while utility availability remains
   unverified on this invocation.
6. **Loader/ABI or later acquisition failure.** Possible only if direct SD
   execution was permitted; the host-linked ABI remains unproved physically.

## Marker-only design

P3.2j does not invoke the collector. The automatic shell first validates raw
`$0` as `/media/<component>/autorun/autorun.sh`, derives the SD root without the
caller's working directory, requires exactly one writable exFAT
`/dev/mmcblk1p1` record, and requires the mount path to resolve to itself. It
atomically reserves one fixed directory with `mkdir`:

```text
b1-p3-2j-startup/
  marker.partial    # interrupted or failed publication evidence
  marker.txt        # complete marker; mutually exclusive with partial
```

The directory is never reused or overwritten. The script writes a bounded
partial marker immediately after destination validation, then records canonical
directory resolution and the hardened 4096-byte firmware guard. It syncs the
partial, renames it to the fixed final name, syncs the directory, and exits.
Failure may leave `marker.partial`; there is no retry.

Complete schema:

```text
schema=neurogenic-b1-startup-v1
autorun_entered=true
directory_valid=true|false|unknown
firmware_guard=pass|fail|not-reached
collector_invocation=deferred
```

A final `pass` establishes shell entry, raw-path mount validation, canonical
package-directory resolution, the exact firmware prefix, and SD writing. It is
sufficient to diagnose the early P3.2g path. Collector start/failure is
deliberately deferred; adding an executable before marker evidence would
reintroduce the strongest suspected failure.

If neither marker exists, recognition and early SD/path-validation failure
remain ambiguous. The design does not overclaim otherwise.

## Package and hashes

Host-only package: `target/b1-p3-2j/final-a/autorun/`.

| File | SHA-256 |
|---|---|
| `PACKAGE-MANIFEST.json` | `457279bb98ec6d8af18d2ed9ecd20da77f54f938d926256017c7e6bfd8933752` |
| `SHA256SUMS` | `6f22b5eb36ecdbc85cab565abfc1883f7f8dce2d93516c8604140aaee60dabf2` |
| `autorun.check` | `e71c0ed7bcf56db10c5bea5825d398fc299a03c31936c696763790189539125c` |
| `autorun.json` | `4a2544dbf61315e0de319069a6b4f91c66b0593177fc886f75bcff3b7ed589c8` |
| `autorun.sh` | `d537d36aae0423606f4236293635ac3f754d2e57e0971ff6d649b220b8c2106a` |

Ordered-content package digest:
`250b426ff92812bad58d71b15e093f689a2bbf02c5ab462479d1bdba8693288e`.
Two generated trees compared byte-for-byte equal. The inspector rejects extra
files, symlinks, modes, changed recognition bytes, collector references,
preload, service control, listeners, firmware operations, and checksum drift.

```sh
python3 -B tools/b1_p3_2j_package.py --repo . \
  --output target/b1-p3-2j/final-a \
  --revision bdb1e8f316de6a20e12609d6b569b6c0a4944000
python3 -B tools/b1_p3_2j_package.py --repo . \
  --inspect target/b1-p3-2j/final-a
```

## Owner-only physical procedure

After separate authorization, verify the five hashes above, use a dedicated
correctly identified exFAT SD, and confirm no existing `b1-p3-2j-startup` entry.
Copy only the reviewed `autorun/` directory to the card root, safely eject,
insert once, and boot once. Execution is automatic; no prompt is expected.
Observe for 30 seconds, then shut down normally, remove the SD, preserve it, and
perform a stock-only recovery boot with UI/audio/pad/DATA checks. Do not repeat
automatically.

Interpretation:

- final marker with directory `true`, firmware `pass`: early autorun path works;
  P3.2g most likely failed at direct collector execution or later;
- final marker with directory `false`: shell entered but canonical directory
  resolution failed;
- final marker with firmware `fail`: shell/destination/directory succeeded and
  the firmware or hash-utility gate failed;
- partial marker: shell entered and SD destination was validated, but execution
  or publication stopped at or after the recorded stage;
- no marker: inconclusive between loader non-recognition and failure before
  earliest safe publication; do not retry unchanged;
- any UI/audio/control regression: stop, recover stock-only, and preserve all
  evidence.

No hook, preload, executable payload, service operation, listener, internal
write, or firmware modification exists in this package.

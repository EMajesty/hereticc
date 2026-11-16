# B1-P3.2h single zero-hook diagnostic runbook

Status: operator handoff only. **Physical execution is not authorized by this
document.** Use this procedure once only after explicit owner authorization.

## 1. Establish the stock baseline

1. With the diagnostic SD absent, confirm MPC Sample VID:PID `09e8:205c` and
   firmware `1.3.0.12` in the stock UI.
2. Boot normally and record pass/fail for UI navigation, audio output, pads,
   DATA encoder, and other controls. Stop if any baseline check fails.
3. On the repository host, verify the reviewed package:

```sh
python3 -B tools/b1_p3_2g_inspect.py --repo . target/b1-p3-2g/preflight-a
sha256sum target/b1-p3-2g/preflight-a/autorun/*
```

The inspector must report `pass` and package digest
`1c232a1d52890c219996f5163b9c881a920a6169e63f3945ca32e6eea7fede0a`.

## 2. Identify and prepare the dedicated SD

The SD must already be correctly prepared as exFAT. Do not format it here.
Replace the assignment below with the exact stable `/dev/disk/by-id/...-part1`
identity reviewed by the owner; never use a guessed `/dev/sdX` name.

```sh
sd_partition='/dev/disk/by-id/REPLACE_WITH_REVIEWED_SD_PARTITION_ID'
case "$sd_partition" in *REPLACE*) echo 'set the reviewed SD identity' >&2; exit 1;; esac
readlink -f -- "$sd_partition"
lsblk -dn -o NAME,TYPE,FSTYPE,RO,SIZE,MODEL,SERIAL -- "$sd_partition"
sd_mount=$(findmnt -nr -S "$(readlink -f -- "$sd_partition")" -o TARGET)
test -n "$sd_mount"
findmnt -nr -T "$sd_mount" -o SOURCE,FSTYPE,OPTIONS,TARGET
test "$(findmnt -nr -T "$sd_mount" -o FSTYPE)" = exfat
test ! -e "$sd_mount/autorun"
test ! -e "$sd_mount/b1-p3-2c-report.txt"
find "$sd_mount" -mindepth 1 -maxdepth 2 -printf '%P\n' | LC_ALL=C sort
```

Stop unless the identity, size, model, filesystem, mount target, and contents
all match the dedicated card approved for this run. Do not share its serial.
Copy exactly one reviewed directory, then verify the copy:

```sh
cp -a --no-clobber --no-preserve=ownership \
  target/b1-p3-2g/preflight-a/autorun "$sd_mount/"
sync "$sd_mount"
diff -r --no-dereference \
  target/b1-p3-2g/preflight-a/autorun "$sd_mount/autorun"
(cd "$sd_mount" && sha256sum -c autorun/SHA256SUMS)
sha256sum "$sd_mount/autorun/SHA256SUMS"
test ! -e "$sd_mount/b1-p3-2c-report.txt"
```

The final command must print SHA-256
`af76e05fb0e321f318f96128a529ff5faf5c0aeeaaf92ee07bff6b18ace12914`.
Unmount through the host's normal safe-eject UI or its approved unmount method.

## 3. Perform the single authorized invocation

1. Shut down the MPC normally, insert the dedicated SD, and use the one stock
   boot/insertion sequence approved by the owner.
2. Allow the one automatic autorun invocation. With `askQuestion:false`, no
   confirmation prompt is expected. Do not trigger it again or remove the SD
   while the collector may be active.
3. The collector has a ten-second monotonic deadline. Observe UI, audio, pads,
   DATA, and controls for a conservative 30-second window. The deadline does
   **not** forcibly interrupt every possible blocked Linux or SD syscall.
4. Stop for repeated autorun behavior, restart indication, UI/audio/control regression,
   unexpected file/message, collector activity beyond 30 seconds, or failure to
   regain normal control. Do not retry. If responsive, shut down normally; if
   not, follow the owner's separately approved recovery procedure.
5. Remove and preserve the SD. Boot once without it and repeat every stock
   baseline check. Any regression ends the investigation.

Expected evidence is the unchanged six-file `autorun/` directory and, at most,
one fixed root file: `b1-p3-2c-report.txt`.

## 4. Preserve and inspect evidence offline

After safely mounting the SD on the host, re-establish its exact identity as in
section 2. Preserve the evidence before analysis:

```sh
evidence_dir='target/b1-p3-2g/evidence-first-run'
test ! -e "$evidence_dir"
mkdir -m 700 "$evidence_dir"
mkdir -m 700 "$evidence_dir/package"
cp -a --no-preserve=ownership "$sd_mount/autorun" "$evidence_dir/package/"
if test -e "$sd_mount/b1-p3-2c-report.txt"; then
  cp --no-clobber --no-preserve=ownership \
    "$sd_mount/b1-p3-2c-report.txt" "$evidence_dir/"
fi
sha256sum "$evidence_dir/package/autorun/"*
python3 -B tools/b1_p3_2g_inspect.py --repo . "$evidence_dir/package"
if test -f "$evidence_dir/b1-p3-2c-report.txt"; then
  sha256sum "$evidence_dir/b1-p3-2c-report.txt"
  python3 -B tools/b1_p3_2_inspect.py \
    --collection "$evidence_dir/b1-p3-2c-report.txt"
fi
```

Preserve originals and hashes. Redact serials, MAC addresses, credentials,
unrelated addresses, user content, and identifying host paths from derivatives.

## 5. Evidence decision tree

- **No autorun prompt:** expected for `askQuestion:false`; it provides no
  recognition evidence. Use report presence and observed behavior instead.
- **Automatic invocation expected but no report:** stop; result is inconclusive (guard, loader, utility,
  permissions, acquisition, or publication may have failed); no automatic
  second attempt.
- **Malformed, truncated, empty, or oversized report:** preserve and reject it;
  publication may have begun, but it supplies no valid baseline evidence.
- **Valid partial report:** retain only confirmed facts; missing facts remain
  unknown and completeness-dependent gates do not pass.
- **Valid complete report:** require schema validity and cross-field consistency;
  treat it only as lifecycle evidence, never as hook or relay authorization.
- **UI/audio/control regression:** stop, remove the SD after normal shutdown if
  possible, perform the stock-only recovery boot, and do not proceed.
- **Hang or no normal control after 30 seconds:** stop interaction, follow the
  pre-approved recovery procedure, preserve the SD, and do not retry.

Successful collection does not authorize B1 DATA-hook testing, USB relay
deployment, service restart testing, or any additional device modification.

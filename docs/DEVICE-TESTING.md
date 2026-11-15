# M0.3b-A device testing

## Status and authorization boundary

The zero-hook probe is cross-built and packaged but has **not** run on an MPC
Sample. Source review, package inspection, and physical execution are separate
gates. Do not place the package on an SD card or insert it into the device until
the owner explicitly approves physical execution.

The package is
`device/m03b-a/build/hereticc-m03b-a-probe.zip`. The build directory is ignored
by Git and must be regenerated locally. Its current reviewed SHA-256 is:

```text
4866ed0bc8373828fd5628e03a4e9022f89e4a18631c116b3e8d15113b0378d4  hereticc-m03b-a-probe.zip
```

This value applies only to the current patch series and build described in
`device/m03b-a/build/SHA256SUMS`. Rebuilds must be reviewed against their own
generated manifest.

## What the package does

The upstream autorun contract is visible in the extracted
`build/inspect/autorun/autorun.sh`. It:

1. hashes the first 4096 bytes of `/usr/bin/MPC Sample` and exits without action
   unless it matches the upstream 1.3.0.12 package guard;
2. copies the probe library to `/run`;
3. creates a transient systemd drop-in under `/run/systemd/system` with
   `LD_PRELOAD`;
4. reloads systemd configuration and restarts `ac50` once; and
5. returns after the service restart.

The JSON has `askQuestion: false`; despite containing prompt text, it must be
treated as automatic execution when the device's autorun loader accepts the
card. There is no retry loop.

During stock-process startup, the library constructor discovers the main image,
checks exactly 63 table-derived words through read-only `/proc/self/mem` access,
publishes the SDK base only after a complete match, writes at most 768 bytes to
`/run/hereticc-m03b-a.txt`, and returns. Report failure is ignored. No branch,
vtable, audio, input, MIDI, lighting, or UI hook is compiled into the probe
mode. The report is transient and contains no address, firmware bytes, device
identifier, sample name, or project path.

## Prerequisites before separately approved execution

- Physical MPC Sample reporting firmware **1.3.0.12**.
- A saved, normally closed project and computer-side backups of projects and
  samples through normal storage paths.
- Stable power and a spare, verified SD card containing no other `autorun`
  directory.
- A successful stock boot without the test card; verify audio, display, all 16
  pads, DATA encoder/push, K1–K3, fader, buttons, transport, sample loading, and
  project loading/saving through normal stock behavior.
- Owner review of the patch, generated SDK diff, ELF inspection, extracted
  autorun files, and `SHA256SUMS`.
- An explicitly approved, non-destructive way to read the transient report from
  `/run`. HERETICC does not assume SSH or add a report-copy mechanism.

Do not use raw partition backups as a recovery claim and do not enable an
undocumented shell merely to retrieve the report.

## Package inspection

From the repository root:

```sh
device/m03b-a/scripts/check-patches.sh
device/m03b-a/scripts/build.sh
device/m03b-a/scripts/package.sh
cat device/m03b-a/build/SHA256SUMS
(cd device/m03b-a/build && sha256sum -c SHA256SUMS)
unzip -l device/m03b-a/build/hereticc-m03b-a-probe.zip
sed -n '1,200p' device/m03b-a/build/inspect/autorun/autorun.sh
sed -n '1,200p' device/m03b-a/build/inspect/autorun/autorun.json
```

`sha256sum -c` must be run from `device/m03b-a/build` because the manifest paths
are relative to that directory. Verify that the archive contains only:

```text
autorun/autorun.check
autorun/autorun.json
autorun/autorun.sh
autorun/hereticc-m03b-a-probe.so
```

## Physical procedure after separate approval

1. Power off normally and remove existing SD media.
2. Extract the reviewed ZIP on a computer.
3. Copy the resulting `autorun` directory itself to the top level of the spare
   SD card. Do not copy the ZIP and do not nest the directory.
4. Safely eject the card, insert it while the device is off, and power on.
5. Observe one stock-service restart. Do not power-cycle during startup and do
   not repeatedly restart the service.
6. Confirm that the stock application reaches its normal screen and remains
   responsive. The probe deliberately has no custom screen or control action.
7. Using only the separately approved read-only retrieval method, inspect
   `/run/hereticc-m03b-a.txt`.

Expected success fields include:

```text
hereticc_probe_version=0.1.0
upstream_sdk_commit=e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39
patch_set=m03b-a-zero-hook-v1
expected_firmware=1.3.0.12
image_discovery=found
fingerprint_words_expected=63
fingerprint_words_checked=63
fingerprint_status=match
feature_hooks_active=false
final_status=0
```

`final_status` values 10–13 indicate missing image, unreadable word, mismatch,
or unexpected table size. Any missing report is also a failed experiment; it is
not permission to retry repeatedly.

## Normal exit and recovery

The constructor returns immediately and the restarted stock process continues.
The preload drop-in and library remain under `/run` for that boot. There is no
live unload and no `dlclose`.

After the one bounded observation:

1. shut down normally;
2. remove the spare card, or delete its top-level `autorun` directory on a
   computer;
3. boot with no probe package; and
4. repeat the stock preflight checks for audio, all pads, display, encoder,
   knobs, fader, buttons, transport, sample loading, and saved projects.

The upstream SDK documents `/run` deployment and card-removal/reboot recovery.
HERETICC has inspected that mechanism but has not verified it physically.

## Stop conditions

Immediately stop and return to a stock-only reboot on a boot loop, more than one
unexpected service restart, missing stock UI, control loss, audio instability,
unexpected project/sample change, write outside `/run`, overheating, or any
failure to restore normal stock operation. Do not attempt live unpatching,
`dlclose`, firmware flashing, partition repair, or repeated automated retries.

Host tests and ELF inspection prove only the compiled source properties stated
in their reports. They do not prove device loader compatibility, physical
stability, correct runtime image discovery, recovery, or absence of an
undiscovered platform interaction.

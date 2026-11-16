# M0.3b-A device testing

The completed A2–A4 physical observations occurred under the former HERETICC
name. Their stored reports, filenames, namespaces, and hashes retain that name
for traceability; active rebuilt binaries use the NEUROGENIC package name.

The prospective DATA-hook checklist is separate and remains **not authorized
for execution**: see [B1-P0 device access and test preparation](INPUT-HOOK-B1-P0.md).
Nothing in that design changes the A4 procedure below.

## Status and authorization boundary

M0.3b-A2 ran once on the owner's firmware 1.3.0.12 MPC Sample. Four reports
were created in one boot; every report contained `image_discovery=missing`,
zero checked words, and status 10. Stock UI, pads, and audio remained normal,
and a card-free reboot restored the normal temporary project. This verifies the
constructor and direct exFAT writer on that unit; it does not verify firmware
compatibility. A3 subsequently produced four reports:

| Slot | PID | PPID | Loader images |
|---|---:|---:|---:|
| 00 | 409 | 1 | 8 |
| 01 | 410 | 409 | 8 |
| 02 | 411 | 410 | 8 |
| 03 | 411 | 410 | 7 |

All four reported an unexpected executable, no expected executable mapping,
`probe-offset-outside-load-segments`, and zero checked fingerprint words. Stock
UI, pads, and audio remained normal. This is consistent with launcher/helper
inheritance but did not prove that explanation. The owner then ran A4: its
reserved expected-process report reached exact `/usr/bin/MPC Sample`, discovered
the firmware image, and matched all 63 words. Stock UI, pads, controls, and audio
remained normal, and stock-only reboot succeeded. This physically validates the
A4 zero-hook probe on one firmware 1.3.0.12 unit/run only.

M0.3b-B0 is a separate source/host-only DATA-hook feasibility build. It has no
SD package and is not authorized for physical execution. Nothing in this A4
procedure authorizes B0/B1.

The package is `device/m03b-a/build/neurogenic-m03b-a-probe.zip`. The ignored
build directory must be regenerated locally. The authoritative hash for a
particular build is in `device/m03b-a/build/SHA256SUMS`; never reuse a hash from
an older document or package.

## What the package does

The upstream autorun contract is visible in the extracted
`build/inspect/autorun/autorun.sh`. It:

1. hashes the first 4096 bytes of `/usr/bin/MPC Sample` and exits without action
   unless it matches the upstream 1.3.0.12 package guard;
2. copies the probe library to `/run`;
3. creates a transient systemd drop-in under `/run/systemd/system` with
   `LD_PRELOAD` for `ac50.service`;
4. reloads systemd configuration and restarts `ac50` once; and
5. performs no post-restart autorun work. The preload constructor writes `/run` first,
   then independently attempts the verified SD result before returning.

The JSON has `askQuestion: false`; despite containing prompt text, it must be
treated as automatic execution when the device's autorun loader accepts the
card. There is no retry loop.

During each process startup that inherits the preload, the library constructor discovers the main image,
checks exactly 63 table-derived words through read-only `/proc/self/mem` access,
publishes the SDK base only after a complete match, writes at most 768 bytes to
`/run/hereticc-m03b-a.txt`, and returns. Report creation failure does not enable
another code path. No branch,
vtable, audio, input, MIDI, lighting, or UI hook is compiled into the probe
mode. The report is transient and contains no address, firmware bytes, device
identifier, sample name, or project path.

The pinned SDK's original `Firmware::find()` accepts only the
`dl_iterate_phdr` entry with an empty `dlpi_name`, a non-null/nonempty program
header table, and a `PT_LOAD` relative range containing the fixed firmware probe
offset. It returns only `None` for every rejection and does not consult
`/proc/self/exe` or `/proc/self/maps`. A3 preserves those criteria but reports
the terminal reason from its probe-only path.

The generated drop-in applies its environment to processes systemd starts for
the service; descendants normally inherit it. The generated source does not
contain the device's actual `ac50.service` command chain, so it cannot establish
whether the expected application is an `ExecStart` process, a descendant, or
launched through another environment boundary. A4 leaves the autorun unchanged.

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
- An empty `hereticc-results` directory is not required; the probe creates it
  only after mount verification. If it already exists, it must be a real
  directory, not a symlink.

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
unzip -l device/m03b-a/build/neurogenic-m03b-a-probe.zip
sed -n '1,200p' device/m03b-a/build/inspect/autorun/autorun.sh
sed -n '1,200p' device/m03b-a/build/inspect/autorun/autorun.json
```

`sha256sum -c` must be run from `device/m03b-a/build` because the manifest paths
are relative to that directory. Verify that the archive contains only:

```text
autorun/autorun.check
autorun/autorun.json
autorun/autorun.sh
autorun/neurogenic-m03b-a-probe.so
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
7. Shut down normally. Remove the card only after shutdown and inspect
   `hereticc-results/m03b-a4-unexpected-00..03.txt` and
   `m03b-a4-expected-00..01.txt` on a computer. No expected-category file means
   the probe did not observe an exact expected executable identity; it does not
   establish incompatibility or prove the application lacked the preload.

An A4 report retains the A3 fields and adds its isolated report category:

```text
hereticc_probe_version=0.3.0
upstream_sdk_commit=e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39
patch_set=m03b-a4-process-aware-v3
expected_firmware=1.3.0.12
image_discovery=found
discovery_reason=found
process_id=<bounded decimal PID>
parent_process_id=<bounded decimal PID>
executable_identity=expected
report_category=expected
report_category_reason=exact-executable-match
expected_executable_mapped=true
process_mappings_readable=true
expected_executable_mapping_count=<bounded count>
loader_images_seen=<bounded count>
rejected_named_candidates=0
loader_timing_plausible=false
fingerprint_words_expected=63
fingerprint_words_checked=63
fingerprint_status=match
feature_hooks_compiled=false
final_status=0
```

Discovery reasons are `found`, `no-loader-images`, `no-unnamed-image`,
`missing-program-headers`, `no-load-segment`,
`probe-offset-outside-load-segments`, `address-overflow`, and
`loader-iteration-error`. Compare PID/parent PID across all newly created files:
different PIDs prove distinct preload process contexts; repeated identical PIDs
would require another explanation. `executable_identity=unexpected` reports
only the mismatch classification, never the unexpected path. A readable exact
stock mapping combined with `no-unnamed-image` or a rejected named candidate
isolates the SDK's empty-name assumption. `loader_timing_plausible=true` is a
conservative classification for absent loader images/unnamed main image, not
proof of a timing race.

Category reasons are `exact-executable-match`, `executable-mismatch`, and
`executable-identity-unreadable`. Only the first selects expected slots.
Unreadable identity is fail-closed into the unexpected namespace. Full slots
cause a local reporting failure only; there is no overwrite, rotation, scan, or
fallback into the other namespace.

`final_status` values 10–13 indicate missing image, unreadable word, mismatch,
or unexpected table size. `feature_hooks_compiled=false` describes the build;
it is not a runtime census of arbitrary process hooks. A missing result file,
an unchanged pre-existing result, or a `.partial` file is a failed retrieval,
not a successful compatibility check and not permission to retry repeatedly.

## Direct SD reporting and evidence

At pinned minimod commit `3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25`,
source analysis records that autorun is a feature of the stock application,
which runs an accepted script via `sh`. The SDK-generated script performs
`systemctl restart ac50` as its final command; it contains no evidence that its
shell survives that restart. Because the shell is launched by the application
being restarted, NEUROGENIC cannot assume that commands after the restart run.
The independently hardware-validated Am MOD loader at commit
`6cdc67276f43cd39f64871cb7e36782481244c65` checks an exact
`/media/<name>/autorun` path and an `rw` exFAT mount before writing a report to
the card. These are comparative source/physical evidence, not NEUROGENIC hardware
validation: [autorun analysis](https://github.com/ioma8/mpc-minimod/blob/3770fb7edcfb432cd45ca4e8fa8bcbefe0435c25/docs/MODDING.md),
[validated mount check](https://github.com/AlekseyMalykh/am-mod/blob/6cdc67276f43cd39f64871cb7e36782481244c65/sd-clone-trial/src/autorun.sh#L281-L292).

M0.3b-A2 avoids post-restart continuation. The constructor reads at most 64 KiB
from `/proc/mounts` and requires exactly one record whose source is
`/dev/mmcblk1p1`, filesystem is `exfat`, options contain `rw` but not `ro`, and
mount point is `/media/<name>` with one nonempty ASCII alphanumeric/`._-`
component. It rejects missing, duplicate, malformed, differently mounted, or
read-only records. After opening the mount with `O_DIRECTORY|O_NOFOLLOW`, it
also requires exFAT magic `0x2011bab0` from `fstatfs`. It never remounts.

The only SD destination is `<verified-mount>/hereticc-results/`. A4 reserves
four `m03b-a4-unexpected-00..03.txt` slots and two
`m03b-a4-expected-00..01.txt` slots; corresponding namespace-specific hidden
`.partial` names reserve each slot. Descriptor-relative creation uses
`O_CREAT|O_EXCL|O_NOFOLLOW`. The complete report is capped at 768 bytes, synced,
renamed with `RENAME_NOREPLACE`, and the directory is synced. Existing names are
skipped, never replaced. Write/sync/finalization failures remove the incomplete
name where possible and return without blocking stock startup. A storage failure
can still prevent cleanup physically; any `.partial` file or absent final file
is failure and must not be treated as a result.

Before any separately approved A4 run, remove prior A4 files on a computer.
A2/A3 names do not collide with A4 names, but clearing old results avoids review
ambiguity. Unexpected processes cannot consume the two expected slots.

## Normal exit and recovery

The constructor returns immediately and the restarted stock process continues.
The preload drop-in and library remain under `/run` for that boot. There is no
live unload and no `dlclose`.

After the one bounded observation:

1. shut down normally;
2. remove the spare card, copy the result on a computer, then delete its
   top-level `autorun` and `hereticc-results` directories on the computer;
3. boot with no probe package; and
4. repeat the stock preflight checks for audio, all pads, display, encoder,
   knobs, fader, buttons, transport, sample loading, and saved projects.

The upstream SDK documents `/run` deployment and card-removal/reboot recovery.
The owner observed normal stock behavior and card-free recovery after A2 and
A3; this remains evidence from one device, not a general recovery guarantee.

## Stop conditions

Immediately stop and return to a stock-only reboot on a boot loop, more than one
unexpected service restart, missing stock UI, control loss, audio instability,
unexpected project/sample change, write outside `/run` or the verified
`hereticc-results` directory, overheating, or any
failure to restore normal stock operation. Do not attempt live unpatching,
`dlclose`, firmware flashing, partition repair, or repeated automated retries.

Host tests and ELF inspection prove only the compiled source properties stated
in their reports. They do not prove device loader compatibility, physical
stability, correct runtime image discovery, recovery, or absence of an
undiscovered platform interaction.

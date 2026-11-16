# M0.3b-A3 zero-hook discovery probe

This isolated crate is the first device-facing HERETICC diagnostic. It is not a
workspace member and has no relationship to the sequencer core or host adapter.
It discovers the stock process, reads the pinned SDK's complete 63-word
fingerprint table, writes one bounded report to `/run/hereticc-m03b-a.txt`, and
attempts the same report directly on one strictly identified SD data partition,
and returns. It installs no hook and exposes no audio, MIDI, input, lighting,
UI, or firmware-write API.

The A2 physical run proved constructor execution and direct SD publication on
the owner's firmware 1.3.0.12 unit, but all four constructor invocations
reported `image_discovery=missing`. A3 keeps the fingerprint gate unchanged and
adds probe-only classifications for the SDK loader walk, exact executable
identity, expected executable mappings, PID/parent PID, rejected named
candidates, program-header failures, and checked address overflow. It reports
no address, mapping contents, environment, arbitrary path, or firmware word.

Direct SD reporting accepts exactly one `/proc/mounts` record for
`/dev/mmcblk1p1`: filesystem `exfat`, `rw`, and a single safe path component
under `/media`. It then confirms exFAT with `fstatfs`, opens directories relative
to pinned descriptors with `O_NOFOLLOW`, and uses one of four fixed names under
`hereticc-results/`. A report is written exclusively to a hidden partial file,
synced, finalized with `renameat2(RENAME_NOREPLACE)`, and followed by a directory
sync. Existing final or partial names are never overwritten. Failure does not
affect the `/run` report or stock startup.

The dependency pipeline retrieves upstream `mpc-sample-sdk` v0.6.0 commit
`e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39`, verifies the detached revision,
and applies the patches in `patches/` with `git apply --check` before applying
them. Generated SDK source lives under `target/m03b-a/sdk` and is not committed.
The upstream SDK remains MIT-licensed and retains its own license and notices.

From the repository root:

```sh
device/m03b-a/scripts/check-patches.sh
device/m03b-a/scripts/build.sh
device/m03b-a/scripts/inspect-elf.sh
device/m03b-a/scripts/package.sh
```

Inspect the generated source and patch identity with:

```sh
git -C target/m03b-a/sdk diff --check
git -C target/m03b-a/sdk diff
cat target/m03b-a/sdk/.hereticc-patchset
```

The build script does not package, install, or execute anything on a device.
`package.sh` first enforces the ELF zero-hook gate, uses the reviewed upstream
transient autorun generator, extracts the result for inspection, and writes
`build/SHA256SUMS`. It never accesses a device or SD card. M0.3b-A1 confirmed
that a post-restart autorun continuation cannot yet be justified: autorun is a
child of the stock application whose service is restarted. M0.3b-A2 avoids that
lifecycle entirely: the preload constructor performs the bounded SD write and
starts no helper, thread, service, or continuation.

Patch `0002-add-a3-discovery-diagnostics.patch` changes only code compiled by
the SDK's `probe-only` feature. The normal SDK `Firmware::find()` and installer
remain unchanged. Probe version `0.2.0` and patch-set identifier
`m03b-a3-discovery-v2` distinguish A3 artifacts and reports from A2.

The crate directly declares `libc = 0.2.191` (MIT OR Apache-2.0), already pinned
by the patched SDK, for descriptor-relative Linux filesystem calls. It adds no
new package or lockfile version.

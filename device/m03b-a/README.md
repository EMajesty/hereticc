# M0.3b-A zero-hook probe

This isolated crate is the first device-facing HERETICC diagnostic. It is not a
workspace member and has no relationship to the sequencer core or host adapter.
It discovers the stock process, reads the pinned SDK's complete 63-word
fingerprint table, writes one bounded report to `/run/hereticc-m03b-a.txt`, and
returns. It installs no hook and exposes no audio, MIDI, input, lighting, UI, or
firmware-write API.

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
child of the stock application whose service is restarted. No SD retrieval
wrapper is packaged until a separately reviewed process-lifetime design exists.

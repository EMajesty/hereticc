# M0.3d.2 native note properties resolution

Status: implemented, host-tested, cross-built, and package-inspected. Physical
execution requires separate owner authorization.

## Confirmed M0.3d.1 evidence

On stock firmware 1.3.0.12, M0.3d.1 restored pad selection and ownership. Its
overlay reported `H1 I1 P0`, pads were silent while owned, and stock-only
recovery was normal. Thus both displaced note sites and the input object were
available, but the physical note-on callback carried a null properties value.

## Properties contract

Pinned `mpc-sample-sdk` v0.7.1 commit
`9487cbba3648afb29d8e3107e31a7ac930985220` declares the note-on ABI as:

```text
note_on(input, source, pad, properties, velocity)
```

The SDK does not construct or clear `PadEvent.properties` on press. Its note-on
hook copies the firmware's argument directly into the event. When a mod returns
`Flow::Forward`, the SDK calls the displaced stock handler with those exact
five original arguments. Only release events intentionally set the event's
diagnostic properties field to zero because the native note-off ABI has no
properties argument.

`midi::send_note_on` also calls the same displaced handler, but its public unsafe
contract requires `properties` to be a live app object. No SDK helper constructs
or retrieves a replacement properties object. Consequently, calling
`send_note_on(..., 0, ...)` would violate the public SDK contract even though
the M0.3d hardware run proved that the ordinary forwarded physical call accepts
the firmware-supplied null value and plays its stock sample.

## Reuse findings

- **mpc-sample-sdk (MIT):** `Flow::Forward` is the supported stock pad path and
  preserves the exact native arguments. This is the adopted solution.
- **MINIMOD, revision `18d41fd805beace4fed623653a250c46a4fa2243`:** uses
  SDK native forwarding and a bounded completion/ledger for ownership; it does
  not call `send_note_on` with null properties. Its workspace declares MIT, but
  no repository-root license file was found, so no implementation was copied.
- **Am MOD, revision `6cdc67276f43cd39f64871cb7e36782481244c65` (MIT):**
  researches deeper stock sequencer/voice routes using firmware-specific
  objects and offsets. Those paths solve a broader problem and do not provide a
  safe replacement properties object for this callback. They remain reference
  evidence only.

The smallest safe solution is therefore controlled native forwarding, not a
new abstraction or SDK patch.

## Implementation

While the overlay is open:

1. A valid first pad press updates track selection and records ownership.
2. With `H1 I1`, the reducer returns `Flow::Forward`. The SDK invokes the stock
   handler after the mod callback using the original `P0` argument.
3. The matching physical release also returns `Flow::Forward`, preserving the
   native note-off pair.
4. A second pad pair while the first is held is consumed without playback.
5. SHIFT+DATA close remains deferred until the active release.
6. If paired sites or input are unavailable, the owned pad remains silent and
   the overlay reports native unavailable.
7. When closed, every pad forwards exactly as stock.

The explicit `send_note_on`/`send_note_off` calls were removed. No firmware
pointer is retained, no properties layout is invented, and no native object is
fabricated. The overlay says `native forward requested`, not “played”; audible
success still requires physical observation.

This is controlled forwarding of a physical stock pad event. It does not prove
that NEUROGENIC can synthesize a scheduled note without a live physical event.
It establishes the safe stock-engine boundary needed before deciding the later
scheduler integration.

## Tests and package

Reducer tests cover `H1 I1 P0`, first-press native forwarding, matching release,
second-pad suppression, deferred close, capability failure, page loss, repeated
presses, and closed stock forwarding. The callback adapter contains no unsafe
native dispatch call.

Build and inspect:

```sh
device/m03d2/scripts/build-package.sh
```

Artifact:

```text
target/m03d2/build/neurogenic-m03d2-native.zip
```

| Artifact | SHA-256 |
|---|---|
| unstripped ELF | `67bafa77e54a861e96c7c0deb58222589b3bf916b5163d34b70e4156e94dcf93` |
| stripped/package `.so` | `74b367e56b68465859df27483ea7341b9d3f2fa43554c71db6d3b11fd08b1927` |
| ZIP | `72c99195088ce59f75508dc9e69f1067608ad175d041454a0d5abb42561cdeec` |

## One-run hardware procedure

1. Boot stock firmware 1.3.0.12 without the SD. Save work and confirm normal UI,
   audio, pads, DATA, PLUS, and transport.
2. Verify the hashes above and strict-inspect the distinct M0.3d.2 package.
   Install only its `autorun/` on the dedicated SD.
3. Boot once, wait for the SDK-driven stock application restart, and open the
   overlay with SHIFT+DATA.
4. With transport stopped, press and hold one pad. Confirm track selection,
   audible stock playback, `H1 I1 P0`, and `native forward requested`.
5. While holding it, press a second pad. It must neither sound nor change the
   selected track. Release the second, then the first; confirm the first voice
   releases normally.
6. Repeat rapid complete press/release pairs and switch pads only after release.
   Request close while held and confirm it closes only after the release.
7. Repeat a bounded press/release with transport running. Confirm no stock
   project, pattern, sample assignment, transport state, or saved setting is
   modified.
8. Close, verify stock behavior, shut down normally, remove the SD, boot
   stock-only, and repeat baseline checks.

Stop for a stuck/extra voice, second-pad sound, selection failure, missing
release, crash/restart, project mutation, audio/control loss, or incomplete
restoration. Remove the SD and use the normal stock-only recovery boot. Do not
begin background sequencing based on this test alone.

## Remaining boundary

M0.3d.2 resolves physical-pad audition without violating the properties
contract. It does not supply a properties object for synthetic note-on, a voice
handle, scheduler timing, independent polyphony, or off-callback dispatch.
Those are separate stock-sequencer integration questions, not reasons to relax
the null-pointer guard.

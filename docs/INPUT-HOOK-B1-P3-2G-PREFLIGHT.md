# B1-P3.2g final preflight and operator checklist

Status: host-preflight complete. Physical execution is not authorized by this
document.

## Reviewed package

Host package directory:

```text
target/b1-p3-2g/preflight-a/autorun/
```

The dedicated test SD must contain exactly this top-level `autorun/` directory:

```text
autorun/PACKAGE-MANIFEST.json  0dbb9d5bafbee44292f6bc494f56109a5c206d75014dc004971ab1f00d88f33e
autorun/SHA256SUMS             af76e05fb0e321f318f96128a529ff5faf5c0aeeaaf92ee07bff6b18ace12914
autorun/autorun.check          eec78fc186c4ea6c219bf43fcdd4804f413a478b694fbf42b0e2b1515de7d8a9
autorun/autorun.json           591725af84e1606de0c459d610bd650a8ebf178c3c0a3a8d3521b92df48deaf4
autorun/autorun.sh             c246de35286fa324d328bd618ef14613329b98079b7809616811aa7301bc6d1e
autorun/p3_2f_collector        58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c
```

The strict inspector's deterministic ordered-content digest is
`1c232a1d52890c219996f5163b9c881a920a6169e63f3945ca32e6eea7fede0a`.
The expected report is the single fixed file `b1-p3-2c-report.txt` at the
validated SD mount root. It must not exist before the run.

The stock application's autorun invocation contract is not available in source.
The wrapper deliberately stays in the foreground and replaces itself with the
collector, so the stock autorun caller may remain blocked for several seconds.
It is unknown whether that caller is also responsible for UI dispatch; audio
and UI responsiveness during collection are therefore physical observations,
not host-confirmed properties. This is why UI/audio disruption and failure to
regain control are immediate stop conditions.

## Preconditions

- Obtain separate written authorization for one zero-hook run. This is not a
  DATA-hook, relay, or service-restart experiment.
- Confirm the device is an MPC Sample (`09e8:205c`) and its UI reports stock
  firmware `1.3.0.12`.
- Boot without the diagnostic SD. Confirm normal UI navigation, audio output,
  pad response, DATA encoder, and other controls. Record only bounded,
  non-identifying observations.
- On the host, rerun the package inspector and compare all six hashes above.
  Confirm the SD is dedicated to this test, contains only the reviewed package,
  and has no old or partial result file.

## Single authorized run

1. Shut down normally. Prepare the dedicated SD from the reviewed host package;
   do not alter package files after hashing.
2. Insert the SD and perform one normal stock boot/insertion sequence specified
   by the separately approved procedure.
3. Accept the single stock autorun prompt once. Do not invoke it again.
4. Observe the UI, audio, pads, and controls without removing the SD. The
   collector has a ten-second monotonic deadline. Use 30 seconds as a
   conservative operator observation window, not as a promise of forced
   termination.
5. Stop the experiment for a repeat prompt, stock restart indication, UI/audio
   disruption, unexpected file or message, failure to regain normal control,
   or activity beyond the observation window. Do not retry. If the UI remains
   responsive, use normal shutdown; follow the separately approved recovery
   procedure if normal shutdown is unavailable.
6. Remove and preserve the SD. Boot once without it and repeat the stock
   UI/audio/pad/DATA/control baseline. Any regression keeps the experiment
   stopped.

The ten-second deadline is checked around all acquisition and publication
stages, but it cannot forcibly interrupt every possible blocked Linux or
removable-filesystem syscall. A run exceeding the observation window is a stop
and recovery condition, not permission to repeat or power-cycle casually.

## Offline validation and interpretation

Preserve the package, report—including a zero-length or truncated file—and
operator observations before analysis. Do not edit the original. Validate with:

```sh
python3 -B tools/b1_p3_2g_inspect.py --repo . target/b1-p3-2g/preflight-a
python3 -B tools/b1_p3_2_inspect.py --collection /path/to/preserved/b1-p3-2c-report.txt
```

- **Absent report:** indeterminate. Autorun may not have been recognized, the
  loader or utility may be missing, a guard may have failed, acquisition may
  have failed before publication, or execution may not have started. Do not
  repeat the run merely because the report is absent.
- **Malformed, truncated, or oversized report:** reject it. It may demonstrate
  that exclusive publication began, but supplies no valid baseline evidence.
- **Valid `collection_status=failed`:** collection ran far enough to publish a
  bounded failure envelope; it does not pass baseline gates.
- **Valid `collection_status=partial`:** retain confirmed fields, but missing
  evidence remains unknown and no gate requiring complete collection passes.
- **Valid `collection_status=complete`:** accept only if schema validity and
  cross-field consistency also pass. It is lifecycle evidence, not permission
  for a hook, relay, service restart, or B1 physical test.

Redact serials, MAC addresses, credentials, unrelated addresses, user content,
and identifying host paths from any shared derivative. Preserve hashes of the
unredacted originals privately.

## Decision

The package is ready for a separate physical-authorization decision. Physical
execution remains **no-go until that authorization is explicit**. B1 DATA-hook
testing, USB relay deployment, and service-restart experiments remain no-go.

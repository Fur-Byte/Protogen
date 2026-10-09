# Efficiency and latency corrective audit

Starting revision: `2f3972e8fbdaece9183d59d9e85d33097ef87591`.
The audit includes efficiency revision `7f284577` and its subsequent changes.
No physical energy or LED latency qualification was performed.

## Confirmed corrections

| Issue | Correction and regression coverage |
| --- | --- |
| Clang rejects four arduinoFFT 2.0.4 floating-point `abs` diagnostics under `-Werror` | Firmware, efficiency and preview builders compile the unchanged dependency separately. Only its Clang `absolute-value` diagnostic remains a warning; first-party checks retain `-Werror`. A negative compilation test proves first-party warnings still fail. |
| Analyzer applied 100 ms to completed expressions, including the default 160 ms transition | Schema 2 separately measures initial press to completion (A), triggering edge to first optical reaction (B), and triggering edge to completion (C). Only B has p99 < 100 ms acceptance in both qualified conditions. |
| Old CSV cannot distinguish reaction and completed artwork | Explicit optical columns, action/edge classification, exact headers and chronological validation reject ambiguous old captures, malformed numbers and missing fields. Missed endpoints remain counted failures. Nearest-rank p99 and 1,000 attempts per condition are enforced. |
| Wearer deadline candidate rounded sub-tick deadlines up to a full tick | Consume pending notifications and yield for sub-tick waits, matching foreground behavior. Actual worker code runs against a 100 Hz RTOS model with short deadlines. Production polling is unchanged. |
| OLED control-command failure could leave pages marked valid; waking could reuse page cache | Invalidate pages and controller-state knowledge after any failed display operation, reissue the power state, and send the complete image on wake. Actual worker/display tests cover partial transfer failure, control failure, warnings, sleep and stale-link restoration. |
| A failed OLED initialization could be reported healthy after an ordinary frame send | Keep the initialization fault latched, retry full GPIO reset/controller configuration after five seconds, reuse existing bus/device handles, and require successful initialization and a frame transfer before clearing it. A regression fails on the previous implementation. Track the warning actually drawn so urgent candidates also clear recovered warning pixels immediately. Healthy screen cadence is unchanged; the fault-recovery path explicitly includes the existing 20 ms reset sequence. |
| Wireless regression script was not executable although Actions invokes it directly | Restore executable permissions on wireless, v4 and DTLS entry points. |
| Clock comparison changed its input's validation result | Report CPU headroom independently without mutating captures. A 49% rejection followed by a 60% reanalysis verifies immutable inputs. Energy reduction beyond uncertainty remains mandatory. |
| Source matching alone allowed incompatible UART modes in recovery bundles; an incomplete ZIP could pass previous-release checksum validation | Check enabled bridge/UART modes, record compatibility, require complete helmet/paw recovery images and matching successful evidence, and retain checksum/path checks. Tests reject incompatible modes, incomplete bundles and corrupted data. |

## Related paths reviewed

The visual cache compares every pose field and all four active FFT mouth buffers.
Wake and visor reconnection invalidate `frameValid`; transition/blink/boop updates
change the composed pose before cache comparison. Existing animation/audio stress
tests now also run with caching enabled, including wireless/visor extensions.
No artwork, animation cadence, transition or microphone defect was confirmed.
Comparison against all four archived faces reports zero logical/physical pixel
differences. The default Teensy HEX is identical to the pre-audit verification
bundle: SHA-256 `f63ade843d97af4efc7b304f20575f31765a2d631881d422a799fb64b62970c8`.

Release-triggered navigation, 30 ms paw debounce, retry/expiry, reconnection
arming, command deduplication and anti-replay code remain unchanged. Protocol
regressions cover loss, recovery, rollover and malformed/replayed records.
The 25 ms Teensy debounce, 160 ms compiled transition default, 16 ms frame
eligibility, 4 kHz microphone timer and saved-settings override remain unchanged.

Temperature/environment/energy sampling defaults and warning thresholds remain
unchanged. Worker tests exercise valid temperature readings, warning restoration
and unknown charge estimates after reset. Checkpoint rollover/retry tests and
actual NVS set/commit/readback fault tests remain in the suite; immediate pairing
saves and credential wiping are preserved. Diagnostic counters retain their
documented saturation or unsigned-wrap behavior and do not measure physical LED
latency or whole-system power.

All eight unqualified switches remain zero by default. The pinned SDK, board and
dependency versions are unchanged. The FFT source matches the retained pinned
download (SHA-256 `82e94aa7ead7c5c596ab0a570980fde5e2912d48b41f9b3dea03ae74f8854bd9`).
The S31/ESP platform negative compilation guards still reject activation. No
production cryptographic parameters, ML-KEM-1024 backend, pairing format, secure
storage gates or radio sleep settings were changed. No hardware was flashed.

## Verification and limits

Local firmware and expanded efficiency suites passed under Clang UBSan, as did
normal and optimized Python tooling, analyzer and compiler-isolation tests.
Local GCC and Clang ASan fail during allocator initialization on this ARM host,
including outside the sandbox; those executions are recorded as blocked, not
passing. GitHub's clean x86 runner requires ASan plus UBSan. Host protocol CMake
builds now instrument crypto dependencies as well as project code.

[The workflow](../.github/workflows/firmware.yml) requires the host suites,
independent v4 vectors, all existing sensor/extension target matrices, authenticated
Teensy variants, default Teensy and both C3 roles, both authenticated ESP roles,
and each of the eight bench switches built independently. The current delivery's
run status and exact commit must be checked in GitHub Actions; historical checks
are not evidence for a new revision. Logs/manifests are retained as build evidence.

Physical online/idle-wake first-reaction p99, completed-expression latency, energy
savings, RF reliability/range, actual flash interruption/endurance and sustained
hardware behavior remain unmeasured. Synthetic tests cannot establish these.
S31 SDK/security/hardware qualification remains a separate gated task. See
[EFFICIENCY_QUALIFICATION.md](EFFICIENCY_QUALIFICATION.md) for physical capture
requirements and [RELEASE_RECOVERY.md](RELEASE_RECOVERY.md) for recovery checks.

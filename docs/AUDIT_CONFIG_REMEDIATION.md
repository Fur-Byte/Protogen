# Configuration, USB serialization, and brightness remediation

> Follow-up: [release brightness qualification](RELEASE_BRIGHTNESS_QUALIFICATION.md)
> supersedes this initial report’s unchanged-EEPROM-layout statement: explicit
> storage format 3 now saves a separate brightness approval while retaining USB
> schema 2 and read-only compatibility for legacy records.

Scope: audit sections 2, 4, 5, and the duplicated display-count loop in section 9.
Implementation and host verification were performed on 2026-10-16/17.

## Confirmed findings and root causes

| Finding | Verification | Resolution |
| --- | --- | --- |
| Brightness had no hardware-specific ceiling | Saving brightness 15 and running the actual sketch's `setup()` programmed intensity 15. `SET`, `BOOST`, and reconnect paths passed their requested values directly to the driver, which limited only to the chip's 0–15 range. | `Settings::MaxBrightness` defaults to 1. `effectiveBrightness()` clamps requests, and `Max72xxPanel::setIntensity()` enforces the same ceiling at the register-write boundary. Startup initialization uses that path too. |
| `replyConfig()` accumulated `snprintf()`'s wanted length | A copy of the original accumulation loop with two 1023-character field names produced both UBSan out-of-bounds-index and ASan stack-buffer-overflow diagnostics. The current 17-field table fits; this was a latent table-growth defect, not a demonstrated remote overflow with existing fields. | `BoundedReply` verifies negative and truncated `vsnprintf()` returns before advancing its cursor. No subsequent append writes after failure. `replyConfig()` replaces incomplete output with a complete `response_too_large` JSON error. |
| Duplicate tracking depended on a 32-bit mask | Repeating the original algorithm for field index 32 failed to remember it on the 64-bit host because assignment truncated the bit; on the 32-bit target the shift itself exceeds the operand width. Current 17 fields do not trigger it. | `ConfigFieldSeen<configFieldCount>` uses a bounded byte array and shifts by 0–7 only. All existing fields and indices through 64 are tested for first-use acceptance and duplicate rejection. |
| Rotation setup repeated the 14-tile count | The setup loop used 14 independently of the panel constructor's dimensions. | Iterate over `numberOfHorizontalDisplays * numberOfVerticalDisplays`; mapping and rotation remain unchanged. |

## Brightness behavior and compatibility

Requests and persisted values remain 0–15. An existing schema-1 or schema-2 saved
brightness of 15 remains loadable and reports configured brightness 15, while
the programmed intensity is capped at 1 in the default build. The configuration
table, byte layout, schema version, command syntax, and wireless protocol remain
unchanged. The ceiling is validated as an integer before conversion to a byte;
compile-time values -1, 16, and 256 fail compilation.

`GET` and `STATUS` add a `brightness` object containing `configured`, `effective`,
and `maximum`. `BOOST` retains its existing success/temporary fields and adds
the same object plus `requested`. Effective intensity comes from the driver's
last intensity setting, so it reflects an active boost rather than merely the
configured base value. Existing visibility/readiness fields still describe
whether the display can emit light. Face Studio labels the input as requested
brightness and shows configured, active, and maximum values. It handles older
firmware without this optional reporting field.

Every intensity write is capped: driver begin, setup, SET, loaded configurations,
boost start/cancellation/expiry, direct driver calls, and connector reinitialization.
Sleeping/waking does not increase intensity. The driver disables display test mode
while shutdown and initializes brightness before enabling output. Connector
interlock/reinitialization changes are documented separately in the sensor report.

The ceiling deliberately limits output even if software callers omit their own
clamp. Raising it requires changing the firmware and qualifying the actual helmet.
It is not remotely configurable. No specific intensity setting is certified
as electrically safe by these changes.

## Electrical assumptions and remaining qualification

The [MAX7219/MAX7221 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7219-MAX7221.pdf)
explains that RSET determines segment current while the intensity register controls
PWM brightness. Intensity zero still lights enabled LEDs; it is not shutdown.
A software ceiling cannot correct an unsuitable RSET resistor, excessive peak
LED current, undersized wiring, bad contacts, insufficient supply capacity, or
thermal problems. The existing design uses a 14-module chain; actual module
variants and resistor values were not measured during this work.

Before raising the ceiling, inspect each module's RSET and identify the LEDs and
driver variant. Measure module and chain current, near/far-end supply voltage,
startup/inrush, supply current limiting, connector/wire/contact temperature,
and enclosure temperature with all-on patterns and normal expressions. Exercise
startup, wake, boost, and detach/reattach. Derate the supply, conversion stage,
protection, wiring, and connectors using their actual component ratings and
measured worst cases. No hardware was connected and no electrical power or
thermal result is claimed.

## Serialization and resource limits

The new writer covers configuration, status, frame, performance, boost, and
error responses. The added sensor diagnostic response uses it as well. Error
strings are JSON escaped; oversized strings become the fixed error response.
Fixed success strings remain bounded literals. Formatting failures cannot wrap a
signed return into an unsigned cursor or underflow remaining capacity.

Production keeps its fixed 1024-byte reply and 384-byte input buffers. Large
future schemas fail with complete error JSON rather than extending beyond the
reply buffer. The serializer can be tested against alternate bounded field tables
without altering the production schema. The duplicate tracker takes three bytes
for the current 17 fields and nine bytes for 65 fields. Increasing the persistent
field table still requires an explicit storage/schema review: the existing
64-byte slot and `RecordBytes <= SlotBytes` assertion remain in force.

## Regression verification

Before the fix, actual-sketch tests failed when stored brightness 15 reached the
register and when a 2048-character error message lost its closing JSON. The old
serializer loop separately triggered ASan/UBSan as described above. The old
32-bit duplicate algorithm separately failed its field-32 regression.

`bash scripts/test-configuration-safety.sh` passes with AddressSanitizer and
UndefinedBehaviorSanitizer enabled and recovery disabled. It compiles the actual
sketch and driver for ceilings 0, 1, and 15, tests every capacity from zero through
one byte above the exact serialized size, and validates all 35 emitted replies
per variant with Python's JSON parser. Coverage includes:

- saved high brightness at startup, SET, BOOST, expiry, sleep/wake, driver begin,
  direct intensity 255, SAVE/DEFAULTS/LOAD, and the largest observed intensity;
- all 17 existing fields at their maximum valid values and atomic duplicate
  rejection for each field;
- 65 distinct tracker indices followed by all 65 duplicates and out-of-range
  indices;
- an 80-field table, five-digit values, 40-character names, a 2048-character name,
  exact fit, truncation, zero/null output capacity, and sentinel bytes;
- a real negative formatting return using an unencodable wide character in the
  C locale, prohibition of further writes after failure, and escaped/oversized
  error strings;
- invalid compile-time ceilings -1, 16, and 256.

The runner is included at the end of `scripts/test-firmware.sh`. Existing matrix,
configuration, and efficiency assertions now compare hardware output to the
central ceiling, while retaining assertions about the unmodified requested value.
The Face Studio transport test verifies configured 15/effective 1 and temporary
boost 0 followed by restoration. JavaScript syntax was checked with `node --check`.

Final complete-suite results:

- `bash scripts/test-firmware.sh`: PASS, exit 0. This includes the real sketch's
  3584 mapping/control/audio cases, both configuration feature variants,
  interrupted EEPROM writes, panel driver tests, protocol/replay cases,
  connector and sensor/microphone recovery fault injection, authenticated UART,
  all eight sensor feature masks, 18 tooling tests, eight PQ-worker tests in
  both normal and optimized Python, 13 Face Studio tests, pairing checks,
  unchanged archived artwork, and the new configuration safety runner.
- `bash scripts/test-efficiency.sh`: PASS, exit 0, including boost expiry,
  actual firmware/cache behavior, connector/UART extensions, NVS failures,
  wearer I/O variants, and its Python checks.
- `PYTHONDONTWRITEBYTECODE=1 python3 scripts/tests/test-helmet-tool.py`: PASS,
  13 tests, including configured versus effective brightness and temporary
  boost reporting through the real firmware preview.
- `node --check` on Face Studio's extracted script: PASS.
- `git diff --check` on all configuration/brightness changes: PASS.

Intermediate full-suite runs exposed an outdated matrix expectation (255 used
to program 15), missing new sensor interval validation, stale-boop expectations,
and a PQ-worker test import-path error. These were corrected by the respective
owners; the final complete firmware run above passed. Host tests simulate
peripherals and do not establish measured electrical safety, WFI power savings,
or physical response latency. Target builds and broader hardware/platform
qualification are tracked by the main remediation report.

## Changed locations

- `firmware/PROT_OS7_MAX7219/Settings.h`: ceiling, clamp, compile-time checks.
- `firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.{h,cpp}`: driver clamp,
  safe initialization, effective-intensity getter.
- `firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino`: setup/boost-restoration clamp
  and dimension-based rotation loop.
- `firmware/PROT_OS7_MAX7219/Configuration.h`: scalable bounded duplicate tracker.
- `firmware/PROT_OS7_MAX7219/UsbReply.h` and `UsbControl.ino`: bounded formatting,
  complete error fallback, duplicate tracking, brightness reporting.
- `tools/helmet/index.html`: requested/configured/active/maximum brightness text.
- `scripts/tests/{configuration-safety,configuration,matrix,efficiency}.cpp`,
  `scripts/tests/stubs/SPI.h`, `scripts/tests/test-helmet-tool.py`,
  `scripts/test-configuration-safety.sh`, `scripts/test-firmware.sh`: regressions
  and suite integration.

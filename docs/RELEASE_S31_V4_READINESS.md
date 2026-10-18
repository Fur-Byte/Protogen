# S31 and hybrid-protocol release preparation — 2026-10-17

## External support gate

Official GitHub API rechecked for this follow-up: the latest ESP-IDF release is
[v6.1](https://github.com/espressif/esp-idf/releases/tag/v6.1), published2026-08-27;
`git/ref/tags/v6.1.1` returns404. The [S31 FAQ](https://esp32-s31.espressif.com/en/faq)
states that official supported tags start atv6.1.1. The taggedv6.1 S31
`esp_cpu_configure_region_protection()` still has IDF-15238 and the permissive
external-RAM PMA setup described in WIRELESS_S31_PREFLIGHT.md. The seven pinned
SDK source hashes were checked by the isolated build scripts. Master contains a
fuller implementation; it is not selected, copied or represented as stable.

The [official S31 feature status](https://developer.espressif.com/hardware/esp32s31/)
and [ESP-NOW API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/network/esp_now.html)
provide platform information, not board qualification. ESP-NOW callback work
stays bounded; native encrypted unicast must be attested by a future transport,
independently of application AEAD. Hardware AES/SHA/ECC/RNG, Secure Bootv2,
flash encryption and key-management capability flags do not establish configured
protection. The stable SDK must implement and support the necessary boot,
PMP/PMA/peripheral isolation, entropy, encrypted storage, anti-rollback and debug
controls for the actual chip/module revision. Secure boot remains classical;
hybrid radio establishment does not make the firmware trust chain post-quantum.

## Implemented isolated platform/UART components

`firmware/esp32/platform_research` is outside production CMake selection:

- `BoardProfile.hpp` defaults to rejection. A reviewed caller must supply target,
  module/profile identity, flash/PSRAM review, UART role/port/baud, exposed pin
  masks and reservations for flash/PSRAM/straps/USB/JTAG/other fitted hardware.
  Checked pin masks reject negative/out-of-range shifts, reused RX/TX, unavailable
  directions and console collisions. No S31 pin assignment is supplied.
- `S31Uart` requires an explicit research macro and the S31 target. It refuses
  unreviewed profiles and console/already-owned UART ports before driver I/O.
  It uses bounded nonblocking reads/writes, owns/removes only its installed
  driver, has no application task/automatic instance/upload entry point, and is
  cross-linked only as an uncalled research function in ignored output.
- `AuthenticatedUart` reuses the unchanged bidirectional UART authentication,
  framing and epoch/session safeguards. A single fixed TX slot,64-byte RX and
  32-byte TX service budgets, partial-I/O handling and100ms write deadline bound
  work/storage. Transport errors clear authority and wipe queued bytes. It owns
  no authentication key. CRC-only mode is not substituted for authenticated
  S31 preparation.
- `scripts/test-s31-platform.sh` runs actual host protocol/authentication tests
  under ASan/UBSan: rejected default/conflicting profiles, partial reads/writes,
  capacity/backpressure, duplicate/replay rejection and failed transport recovery.
- `scripts/check-s31-uart.py --preview` verifies pinned SDK sources, negative
  activation compilation and the guarded adapter's target compile/uncalled
  relink. It also compiles an **unlinked type-only S31 ABI probe** for v4 types.
  The ESP_PLATFORM macro is absent only in that isolated type invocation; no
  activation guard is changed and no v4 code is linked into the app.

The helmet UART milestone is deliberately narrower than a complete board port.
The Paw has no approved UART requirement; its profile cannot masquerade as a
qualified helmet. Its controls/ADC/USB/indicator mapping remains a board-review
and hardware-validation task. The existing C3/v3 firmware remains functional.

## V4 integration and resource evidence

The complete existing protocol/core and previous review were examined: P-256 and
ML-KEM-1024 contributions, canonical transcript, separated KDF/traffic keys,
Finished/Commit gates, fragment bounds/expiry, replay/sequence renewal,
cancellation generations, error recovery, storage provenance and secure wiping.
No algorithm, combiner, key strength or upstream cryptographic implementation is
weakened. Both production compile-time activation barriers remain intact.

`UartIntegration.cpp` now composes the **real v4 simulator/core and primitives**
with the independent authenticated UART endpoints and Teensy application gate.
It exercises pairing/data, radio rekey while UART authority persists, injected
KEM failure/recovery, Teensy reboot epoch change, and old radio/UART traffic
rejection. These tests are included in the v4 host suite. They are not an
ESP-NOW/RTOS/physical Teensy integration or measured UART/radio scheduling test.

S31 ABI compiler observations (GCC15.2, existing target ISA/ABI): Core30,640B,
SlotWorker6,392B, Exchange9,440B, Material340B, Reassembly1,744B and Result6,360B.
Exchange/material/result/workspace components already owned by Core must not be
added again to its size. The explicit Core+worker+49,152B candidate worker stack
reservation is86,184B before platform task/heap/queue/key allocations. The
resource checker estimates44,721B unlinked text; this is not a final firmware
size increase. Known nested native maximum26,336B plus6,144B platform allowance
leaves16,672B in that worker candidate. Static frames do not measure registers,
interrupt/RNG/library call depth or RTOS high-water. No worker stack or CPU-clock
change is activated in production.

Run `scripts/check-v4-resources.py` after a fresh isolated S31 baseline for
source-bound frame/object/role estimates. Existing12288B main task stack is not
approved for direct KEM execution. A future worker must execute off the ingress
path, retain bounded queue/rate budgets, retire in-flight cancellation safely,
and prove watchdog/idle progress, stack/heap margin and UART service under load.
Host operation timings and scheduler gaps are not converted into S31 estimates.

## Precise remaining release gates

**Blocked by external dependency:** an official supported stable S31 SDK tag
with reviewed working memory/peripheral hardening. A preview/master switch or a
successful compiler probe does not satisfy this requirement.

**Implemented but requiring hardware validation:** approved module/revision and
board mapping; both-role UART/USB/boot/flash/PSRAM/ADC/calibration/indicator checks;
ESP-NOW encrypted-peer/RF/channel/antenna behavior; fresh entropy under actual
radio/power states; complete protocol worker scheduling/watchdog/runtime stack,
heap minimum/largest block, cancellation/rekey and command-latency measurements.

**Provisioning/storage/update dependencies:** qualified hardware-backed encrypted
NVS, separate per-helmet UART key, secure boot/flash-encryption release process,
signed update/anti-rollback/recovery and debug policy. No key generation/eFuse
burn/flash/lockdown is performed by these scripts. Development remains recoverable.
See PRODUCTION_PROVISIONING.md and WIRELESS_V4_HARDWARE_GATES.md.

For helmet qualification, with explicit authorization and an approved unpowered
fixture, cross the reviewed3.3V UART TX/RX to Teensy Serial1 RX0/TX1 and common
ground. Verify authenticated epoch/status handshake, accepted/deduplicated
commands, unplug/reset/rekey/fault recovery and optical response under load.
For Paw qualification, verify actual buttons/debounce/combined gestures, ADC
calibration/battery measurement, strap-safe boot, buffered six-digit pairing LED,
USB/console ownership, antenna placement and current draw. Compare/confirm all
six pairing digits on both roles. Never flash images from `build/s31-probe` or
`build/s31-uart` as a board definition; they contain unqualified C3 reference
assumptions. Physical S31/UART/v4 production readiness is not claimed.

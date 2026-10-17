# Platform, idle and maintenance review — 2026-10-17

## Hidden-controller CPU activity

Confirmed: the hidden/sleep path returned from every loop without a wait; the
pinned Teensy core immediately invokes loop again. Controls already poll at
most once per millisecond and display composition already has a 16 ms gate.

`ControllerIdle.h` now uses shallow Cortex-M WFI after servicing hidden-state
controls, USB/UART and sensor acquisition, only when no buffered serial work
remains. Interrupts remain enabled. It refuses to wait in an ISR, with PRIMASK
set, with SysTick disabled, with SLEEPDEEP enabled, or with CCM low-power mode
selected. No oscillator, CPU frequency, ADC timer, UART or USB configuration is
changed. The pinned Teensy 1.62 `startup.c:236-238` configures a 1 ms SysTick;
that periodic interrupt bounds the added wait for polled inputs to one tick,
including the check/WFI race. This is a timing argument, not a measured wake
latency. UART/USB interrupts may wake it sooner. Visible audio/animation work
never uses this path; ADC1 safety sampling still runs before each hidden wait.

Test-first `controller-idle.cpp` exercises actual loop policy, drained/backlogged
serial queues, visible work and debounced button wake. Its host implementation
does not execute ARM WFI. `PERF` adds `idle_waits`, `idle_total_us`, `idle_max_us`;
on the board these are interrupt-wait counts and elapsed times, not CPU power
or a calibrated utilization percentage. Existing render counters remain.

Estimate only: eliminating busy instructions during each otherwise idle interval
should reduce core switching activity. No mA, battery-life or temperature benefit
is claimed. Measure hidden steady-state current with a meter, record PERF deltas
and UART/button-to-photodiode latency, then compare matched workloads. CPU clock
reduction remains deferred because no physical headroom or timing measurements
justify changing the established 600 MHz baseline. SPI remains 4 MHz.

## S31 status and isolated preparation

On 2026-10-17, official GitHub API `releases/latest` returned **v6.1**
(published 2026-08-27); the official `git/ref/tags/v6.1.1` endpoint returned
**HTTP 404**. [v6.1 release notes](https://github.com/espressif/esp-idf/releases/tag/v6.1)
explicitly call S31 support preview. The [S31 FAQ](https://esp32-s31.espressif.com/en/faq)
names v6.1.1 onward as officially supported. A release-calendar prediction is
not evidence of a published supported tag.

The [tagged S31 region-protection implementation](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_hw_support/port/esp32s31/cpu_region_protect.c)
still contains IDF-15238 and installs the permissive external-RAM PMA entry;
the pinned source hash matches the prior preflight. Public master has fuller
PMP/PMA setup, but is not substituted for the supported-stable requirement.
The [official S31 ESP-NOW API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/network/esp_now.html)
exists; the receive callback executes in the Wi-Fi task and must do bounded
queueing, not crypto/long work. Context7 corroborated the official receive-info
source/destination interface and queued-callback guidance. Document availability
and successful preview linking do not qualify board security or RF behavior.

The full historical `WIRELESS_S31_PREFLIGHT.md` and later v4 implementation were
reviewed. Its old statements that v4 did not exist are historical: the isolated
host core now exists, with both production activation guards retained.
`DeviceConfig.h` now rejects unqualified ESP-IDF targets; C3 is accepted, and
S31 is accepted only with `PROTOGEN_S31_PREFLIGHT_ONLY` in the isolated research
CMake project. The exception permits compiler research using existing C3 pin
constants, and is expressly not a board definition or upload candidate. Other
targets remain rejected even with that flag. Four host compile tests cover it.

### Board differences and UART milestone

No S31 module/revision, flash/PSRAM wiring, antenna or approved GPIO map is
available, so inventing a production pinout would be unsafe. The current C3
mapping is centralized in `DeviceConfig.h`:

| Function | C3 reference | Required S31 qualification |
| --- | --- | --- |
| Helmet UART | RX18 / TX19, UART1, 115200 8N1 | Check module pin exposure, GPIO matrix, USB overlap and safe boot state; cross TX/RX to Teensy Serial1 0/1 at 3.3 V with common ground. |
| Helmet I2C / screen | SDA0/SCL1; SCK4/MOSI6/CS7/DC5/reset10 | Verify GPIO capabilities and flash/PSRAM/strap exclusions before enabling peripherals. |
| Pairing / unlock | LED2 (buffered strap), unlock9 | Keep local pairing gestures and indicator; inspect S31-specific straps/boot/download paths. C3 strap notes cannot be copied as S31 rules. |
| Paw controls / ADC | next4/previous5/boop10, battery ADC3; helmet temperature ADC3 | Reassess channel map and calibration support. Never substitute guessed voltage conversion when calibration fails. |
| Entropy / memory | ESP-IDF hardware entropy with radio, pinned PSA primitives | Review actual stable target entropy requirements, secure boot, flash encryption, NVS key provisioning, debug, anti-rollback and memory-protection implementations. |
| USB / boot / flash | Helmet console UART, paw USB Serial JTAG; 4 MiB reference | S31 peripheral/USB selection, module flash mode/size and boot routing require explicit board configuration; research console defaults are not an approved paw USB port. |

The safe UART software milestone is the existing bounded COBS/CRC bridge plus
optional bidirectional HMAC endpoint, cross-built in isolated S31 research and
exercised by the host protocol/authenticated-rendering suites. Before physical
UART use: approve the S31 board mapping, obtain a supported stable hardened SDK,
run both role and UART-auth builds, then on an unpowered fixture wire the
qualified UART pins. Verify fresh Teensy epoch/status exchange, malformed frames,
replays, unplug/reboot and commands under load with auth modes matched. Do not
use a firmware image from `build/s31-probe` on hardware. A real S31 UART, entropy,
USB, ADC or security milestone is not claimed without those gates.

## Maintenance disposition

- CI now explicitly grants only `contents: read`, pins both actions to verified
  full commit hashes, disables persisted checkout credentials and retains all
  existing suites. New config/sensor/idle/board/host-portability checks join CI.
- Private `ProtogenUartKey.h` files and local `.pio-mcp-workspace` artifacts are
  ignored; the public regression fixture is explicitly retained. A bounded
  tracked-file signature scan found no private-key blocks, GitHub token shapes,
  AWS access IDs or private UART headers outside that fixture. It is not proof
  that every possible secret representation is absent. No secret values printed.
- Source fingerprints now ignore generated PlatformIO MCP logs. Test-first
  reproduction proved those logs previously changed build identity even when
  firmware did not change; source edits still change it.
- macOS preflight failed under `-Werror` because Linux `-no-pie` was rejected.
  `host_support.py` retains sanitizer flags, selects non-PIE only on Linux,
  and resolves installed/explicit Clang analyzers rather than a Linux-only path.
  Tests compile/run a sanitized warning-clean binary and verify both policies.
- `SecureSession`/DTLS is excluded from production CMake, but directly used by
  `scripts/tests/dtls.cpp`, its CMake target and CI. It is not unreferenced dead
  code, so the historical compatibility suite and SDK options remain. Removing
  it would violate the requirement to retain necessary tests. No partial TLS
  dependency pruning is mixed into these safety fixes.
- The four committed validation JSON snapshots total 261,964 bytes. They contain
  historical hashes, compiler/host observations and SDK/resource assessments
  that cannot be recreated exactly from current inputs/time/toolchains. Retain
  them and their provenance; all new transient evidence remains in ignored build.
- Original PROT_OS7 sketch license permission remains unverified. Upstream
  licenses were preserved and head-model attribution improved without assigning
  a blanket license; see `THIRD_PARTY_NOTICES.md`. No third-party permissions
  were invented. Historical 8.0 validation labels remain historical; live USB
  responses already identify 8.2, so there was no stale live version to replace.
- `Icons.h` CRLF and trailing whitespace normalized; source initializer/artwork
  comparisons still show zero pixel differences for all four original faces.
- SECURITY reporting guidance requests a private channel through a minimal
  repository issue and conditionally points to GitHub private reporting if
  available; no address, enabled feature or response promise was invented.

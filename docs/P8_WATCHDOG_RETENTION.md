# P8 watchdog and warm-reset diagnostics feasibility

**Disposition.** Watchdog activation and target retention remain unqualified.
The feasibility review uses pinned platform sources (F0) and a host-only
retention model (F1). Both physical reset-domain characterization and timeout
selection remain open.

## Watchdog coverage at the pinned baseline

| Target | Existing mechanisms and coverage | Reset and recovery limitations |
| --- | --- | --- |
| Teensy 4.0 | NXP i.MX RT1062 has WDOG1/2 and RTWDOG (WDOG3). WDOG1 can signal the SRC functional reset path. WDOG2 has a security-violation path to SNVS. WDOG1/2 have 0.5-second timeout granularity. RTWDOG has a separate configurable clock and reset request. Protogen and the inspected Teensy 1.62.0 startup contain no application watchdog configuration or feed. The core fault handler can record exceptions and, if it keeps executing with PIT and USB access, request a software reset after roughly eight seconds. It cannot recover a foreground stall that never enters that handler. | SRC POR and COLD sequences reset the ARM and functional modules, while COLD leaves certain blocks such as IOMUXC outside its reset scope. `SRC_SRSR` distinguishes watchdog 1/2 from watchdog 3. Bootloader behavior and which bytes survive the selected path require board evidence. Teensy core exception breadcrumbs only characterize that specific handler and software-reset path. [NXP reference manual, §§21.3.4, 51.1.6–7 and watchdog chapters](https://www.pjrc.com/teensy/IMXRT1060RM_rev3.pdf). |
| ESP32-C3 helmet and paw | Both inspected local `sdkconfig` outputs select a 9 s bootloader RTC watchdog disabled before `app_main`, a 300 ms interrupt watchdog (TG1, fed by the FreeRTOS tick), and a 5 s task watchdog (TG0, runs at startup, monitors idle CPU0, panic disabled). The task watchdog reports starvation in this mode. Protogen does not subscribe `app_main` or application service progress. An idle tick or yielding task can leave a stalled service undetected. Interrupt watchdog panic behavior and its hardware fallback have different restart paths. | The C3 has CPU, core, system and chip reset domains. MWDT stage actions can reset CPU or digital core, the latter excluding RTC. RTC watchdog system reset includes RTC. `esp_restart()` in the pinned C3 source resets selected peripherals before requesting a CPU software reset and configures RTC watchdog recovery stages. Ordinary power-on and brownout have chip/system-level effects. [ESP-IDF watchdog guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c3/api-reference/system/wdts.html), [C3 reset sources and watchdog domains, §§6.1 and 12](https://documentation.espressif.com/esp32-c3_technical_reference_manual_en.pdf). |

Reviewed Teensy files from `teensy:avr@1.62.0` include `boards.txt`,
`cores/teensy4/startup.c`, `imxrt1062.ld`, `CrashReport.cpp`, and `eeprom.c`.
Reviewed C3 files from local PlatformIO `framework-espidf` package `4.60100.0`
(ESP-IDF `6.1.0`) include `components/esp_system/startup_funcs.c`,
`port/soc/esp32c3/system_internal.c`, and `port/cpu_start.c`. The repository
pins `espressif32@7.1.3` in [the lock](../toolchain.lock.json) and selects
both C3 roles in [platformio.ini](../firmware/esp32/platformio.ini). The local
generated `sdkconfig.helmet` and `sdkconfig.paw` are configuration references.
Reconfirm their effective watchdog, panic and bootloader settings in the
exact candidate build evidence. Capture raw and SDK-mapped cause early on
each approved bench reset since the paths can report different causes.

### Startup, storage and peripheral behavior

- Teensy [setup and loop](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino)
  start USB without waiting for a host, initialize entropy, read settings,
  configure SPI and the display, then initialize ADC/dock/UART. Normal loop
  owners service USB, UART, controls, sensors, display and audio before
  returning or entering WFI. A sleeping display or intentionally paused ADC
  does not require progress at the active rendering/acquisition rate.
- Teensy [configuration storage](../firmware/PROT_OS7_MAX7219/ConfigStore.h)
  uses two 64-byte slots with CRC and commits the destination marker last.
  A save makes a bounded number of EEPROM calls. The Teensy core emulates
  EEPROM in flash. Its compaction can erase and rewrite sectors and polls
  flash busy state. Neither call count nor the host's interrupted-byte model
  establishes a wall-clock bound or atomicity during real flash compaction.
- C3 [startup](../firmware/esp32/src/main.cpp) opens NVS, loads configuration,
  initializes role-specific UART/GPIO, radio and the wearer worker, then
  enters the owning application loop. Storage or selected GPIO/UART errors
  return with wireless disabled. NVS load/write/commit/readback and radio
  startup must be measured on both role builds, including occupied NVS pages,
  optional workers and recovery. [Storage policy](../firmware/esp32/src/SecureStorage.cpp)
  preserves existing pairing on initialization failure by declining to erase
  NVS. Repeated restarts will not clear an underlying storage or boot failure.
- MAX7219 digit RAM and the scan engine are on the display driver. A powered
  enable-state driver can keep displaying the previous pixels while MCU
  foreground execution, SPI service, or the MCU itself restarts. Shutdown
  stops scanning while leaving register contents intact. Verify actual gate,
  power and light behavior independently on installed panels. Even if the
  watchdog resets promptly, immediate fail-dark behavior is unqualified.
  [MAX7219/MAX7221 datasheet, shutdown and initial power-up](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7219-MAX7221.pdf).
- A reset on either controller does not reset the other controller or
  establish UART/RF freshness. Re-admit commands only through the current
  authenticated session, generation and expiry checks. A powered OLED or
  other installed peripheral can also keep its own state. Check signal and
  rail sequencing for installed hardware using the
  [qualification procedure](P8_HARDWARE_QUALIFICATION.md).

### Separately reviewable activation proposal

The 250 ms deadline is experimental. It is shorter than the WDOG1/2 hardware
step and has no measured startup, flash, sleeping, RF reconnect or worst-case
service margin. Select the watchdog mechanism, scope and deadline in a
separate activation review with numerical budgets based on identified board
measurements. A foreground-only observation observes gaps when foreground
execution resumes. Use the device watchdog clock for independent enforcement
only in an independently approved activation change.

| Gate | Evidence required before activation |
| --- | --- |
| Required progress | Define per-state service epochs and maximum allowed age. On Teensy, qualifying work includes the foreground loop and applicable control, render, audio, sensor and link service. On C3, qualify application-owner radio and local control/UART work and any worker that is required in the current mode. Advance epochs after completed useful work or explicit, valid state transitions. Require a distinct foreground epoch to feed. Tick, ISR, USB traffic and task yields alone provide inadequate coverage. |
| Arming and finite maintenance | Measure reset-to-ready, IRQ blocking, EEPROM compaction, NVS reads/commit/GC, first pairing/reconnect, overload, sleeping/wake and optional features. Record distributions, observed maxima and measurement error. Choose an approved positive margin over the longest healthy interval and a measurable upper recovery bound. Allow a save only with explicit start, finite deadline and progress conditions. An expired operation or stalled foreground cannot feed indefinitely. Cover pre-arm stalls using separately measured startup behavior. |
| Recovery from real faults | With a separately reviewed bench build verify the measured fault-to-reset and reset-to-safe-output bounds. Confirm distinct fault detection for stuck foreground with live IRQs and for blocked interrupts. Verify legitimate inactivity and bounded saves yield zero unexpected resets and no additional ADC drops over the agreed trial count. Measure with probes/debugger detached where required by the platform. |
| Reset storms and recovery | Demonstrate a bounded retry policy before repeated reset attempts, access to existing USB or physical loader recovery, preserved settings and user control, safe local display behavior, and no stale or duplicated command authority. Startup failures before a retry marker can be recorded and loss of retained memory are explicit cases. Until retry arbitration is independently qualified, do not derive reset policy from the host diagnostic counter. |
| Physical output | Measure time from stall to last emitted pixel and first safe subsequent frame under CPU reset, power and dock states. Approve a documented maximum stale output interval or block activation. Source reset events and SPI writes alone do not prove optical blanking. |

## Warm-reset retention feasibility

P3 already bounds its records and uses a single owner per device. Boot starts
with empty cursors and sequence zero. Teensy places the physical record array
in `DMAMEM` (`.dmabuffers` mapped by `imxrt1062.ld` to `NOLOAD` `.bss.dma` in
OCRAM). `startup.c` initializes `.data` and clears the DTCM `.bss` range.
The P3 ring cursor and owner state reinitialize even if an OCRAM byte survives.
The core itself writes a fixed fault report at `0x2027FF80` and breadcrumbs
at `0x2027FFC0`, with explicit D-cache handling. A future layout must reserve
an independent linker range and check non-overlap with crash data, DMA, heap,
startup scratch and bootloader behavior. `NOLOAD` only describes how the
section is loaded. It gives no power-loss or reset-domain guarantee.

On C3 the P3 ring uses ordinary initialized application state. IDF supports
`RTC_NOINIT_ATTR` in `.rtc_noinit` and `__NOINIT_ATTR` in DRAM `.noinit`.
The pinned `cpu_start.c` clears ordinary BSS and clears RTC BSS when the
reset reason is outside deep-sleep wake. Linker templates in
`components/esp_system/ld/esp32c3` and `ld/ld.rtc.sections` describe separate
RTC placement. The pinned IDF system test `test_reset_reason.c` exercises
RTC no-init values for selected reset paths. That evidence motivates a
candidate region and does not cover the actual helmet/paw images, all ROM
paths, all watchdog stages, brownout, or power removal.

### Executable host model

Run `python3 scripts/tests/test-reset-retention.py`. The model is under
`scripts/` and has no device firmware linkage. A model image is two 124-byte
slots. Each little-endian slot has `P8RR` magic, format version 1, record
count, 32-bit publication sequence, 32-bit boot generation, saturating
attempt count up to three, and a 32-byte expected firmware identity. Four
16-byte P3-format records occupy the remaining body, with zero padding for
unused records. CRC-32 covers the full 116-byte body. The four-byte `DONE`
marker follows the checksum. Publication invalidates the alternate slot,
writes its body and CRC, and commits the marker last. The last good slot is
available after any modeled partial prefix in publication. Two valid slots
must hold consecutive publication IDs and a boot generation that is unchanged
or advances by one, both modulo 2³². Conflicting slots fail closed. P3 sequence
order admits gaps and wrap because retained events are filtered.

The caller supplies the firmware identity and an independently trusted
warm-reset decision. Rejected domains or installations of a different build
would require a fresh journal before publication, including rollback to a
previous revision, to prevent rediscovery of older compatible bytes. The
model's generation and attempt counter describe only observed publications,
so a reset before the first commit can go uncounted. The boot attempt counter
can clear after a documented healthy-progress milestone in a later design.
Tests demonstrate iteration bounds and saturating/reset observations with
fixtures. They never request a reset.

The model retains only an allowlist of numeric P3 boot, audio drop and main
gap records. Any eventual producer must use those typed recorder call sites.
Never copy arbitrary RAM, stack, raw audio, keys, nonces, tags, pairing
material, packets or decrypted payloads. These observations confer no
settings, brightness, pairing, replay or still-valid command authority.
CRC-32 detects accidental corruption in the modeled bytes. Set no trust
policy based on CRC integrity.

### Target retention gates

1. Review the exact ELF, map, section flags, program headers, startup stores,
   ROM/bootloader behavior, D-cache attributes and write ordering. On Teensy
   prove crash-report, heap, cache-line and DMA isolation. On C3 check RTC
   budget and region ownership for both builds and ensure RTC/DRAM startup and
   bootloader loading exclude the selected region. Establish a deterministic
   build identity representing revision, flags and toolchain in the real export.
2. On identified hardware in an approved bench program, record known sentinel
   values and early exact reset classes, then verify which eligible software,
   exception and individually configured watchdog actions preserve the
   reservation. Check cold startup, firmware change and rollback reject
   previous state. Treat reset-cause uncertainty and resets outside the observed
   retention domain as invalid. Do not depend on power-loss retention or use
   destructive power fault tests.
3. Verify ordered buffer writes, cache maintenance/barriers, incomplete
   marker rejection, repeated resets, safe fallback and readout with target
   tests and timing budgets. Capture before/after bytes and supporting hashes.
   Discard corrupt data and preserve existing rollback and radio freshness.
   Keep routine events in the volatile P3 recorder. There is no EEPROM, flash
   or NVS persistence path for this model.

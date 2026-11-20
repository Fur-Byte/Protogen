# Firmware 8.2 — changes, reasons and verified boundaries

The Tier 1 / Tier 2 prototype software is implemented. Teensy 4.0 still owns the outward face; separate ESP32-C3 firmware adds a dedicated wireless paw and wearer status. The working matrix renderer, original artwork, hardware-random blink implementation and existing pin assignments were preserved. Nothing was flashed, purchased or physically rewired.

The default Teensy image still has all optional hardware disabled. New interfaces become active only when their compile-time flags are enabled after connecting and qualifying the corresponding hardware. ESP32 boards start unpaired with optional peripherals disabled. This makes the upgrade available without assuming that unwired sensors or a removable connector already exist.

## Changes and why

| Area | Change | Purpose |
| --- | --- | --- |
| Original artwork | Kept `Icons.h`, the original four faces, tile order and rotations unchanged; repeated the archive comparison | Preserve Feronium's appearance, including the original `vwv` boop |
| Physical boop | Kept analog IR on A7/21 as the baseline, using hysteresis, release-before-trigger/retrigger and the original held/proximity behavior | Start with the cheapest existing interface and avoid adding capacitive/piezo hardware before IR is tested |
| Boop prototype settings | Added `boop_mode` and `boop_duration_ms`; held mode defaults to a 6,000 ms cap, timed mode is selectable | Compare held versus tap behavior on the actual visor without redesigning the expression |
| ADC1 acquisition | Converted boop and optional dock-voltage reads to asynchronous, bounded conversions; a stuck conversion becomes a fault | A failed analog peripheral cannot leave the Teensy loop waiting indefinitely |
| Paw commands | Added optional Teensy Serial1 control and two ESP32-C3 targets, with three paw buttons | Remotely select existing expressions, sleep/wake and request a 1,200 ms original-art boop |
| Wireless architecture | Chose ESP-NOW for a dedicated controller pair; ESP32 sends high-level commands over UART | Keep RF and its failures separate from real-time matrix rendering |
| Cryptography | Used encrypted Espressif unicast peers plus Mbed TLS DTLS 1.2 PSK/AES-128-CCM with full authentication tags | Replace the earlier custom HMAC/handshake idea with established library cryptography |
| Pairing | Added physical USB unlock, OS-generated unique keys and one versioned NVS configuration record | Prevent nearby devices from pairing by radio or using a universal default key; avoid printing/storing key files on the host |
| Command validity | Added bounded COBS/CRC UART frames, exact payload validation, session/boot IDs, sequence dedup, 300 ms receiver tickets, execution ACKs and rate limits | Reject corrupted, stale, replayed or malformed commands and prevent retry from repeating an action |
| Reconnect | Added two-second loss detection, fresh DTLS sessions, discarded pending actions and release-all-buttons rearming | Wireless failure preserves the current local face and does not produce a held-button action on return |
| Board resources | Defined board-specific maps for helmet DevKitM-1 and paw XIAO C3, separating their different USB interfaces | Avoid C3 boot straps, console conflicts and assumptions that every development board exposes the same pins |
| Hall timing | Changed logical visor qualification to 150 ms | Match the sampled DRV5032FC, whose period can reach 75 ms; frequent GPIO reads cannot establish independent magnetic samples |
| Removable face | Added optional pilot/power/buffer/voltage sequencing, voltage sampling through sleep and a full-frame reinitialization before showing the face | Restore a detached MAX7219 chain cleanly without trusting a stale rail reading, and complement an independent hardware interlock |
| Matrix startup | Added an option to initialize the driver while remaining in shutdown | Load a complete valid frame before waking the matrices |
| Microphone failures | Added flat-rail/missing-acquisition detection, a latched fault and session mute/retry over USB or the command interface | Failed speech input disables speech while local expressions and blinking continue |
| Wearer sensors | Added SHT40 CRC/range/staleness handling, INA228 hardware energy accumulation and optional NTC conversion | Report internal conditions and delivered energy with explicit invalid/unknown states |
| Battery estimate | Added calibrated delivered-mWh capacity and a physically unlocked known-full reference; gaps invalidate the percentage | A regulated USB-PD output voltage cannot reveal the bank's state of charge |
| Wearer screen | Added a separate low-priority SSD1306 SPI task with face/link/sensor status and advisory warnings | Show useful information privately without replacing outward expressions or occupying the Teensy render loop |
| Paw battery | Added optional cell-voltage ADC and USB `INFO` reporting; deferred capacity selection to current measurements | Expose a real voltage reading without inventing a cell percentage or requiring a 1,000 mAh cell |
| Settings compatibility | Added schema 2 while accepting existing schema 1 EEPROM records/profiles; retained atomic validated saves | Keep saved settings usable and make the new physical-boop fields default to original behavior |
| Face Studio | Added held/timed boop controls, duration, microphone mute/retry and paw/fault diagnostics | Make prototype tuning and fault recovery accessible through the existing local tool |
| Build evidence | Pinned the ESP toolchain; checked resolved console/security/buffer settings and the actual generated partition; added target and native tests | Ensure exported firmware matches the intended configuration rather than relying on toolchain defaults |
| Engineering package | Added power/load estimates, candidate components, connection schematics, resource map, physical layout and qualification order | Turn the reviewed architecture into a concrete prototype package while leaving measured limits conditional |

## Original boop and blinking

A physical IR boop uses the existing `vwv`/`vwvL` reaction. Held mode ends on release, has a six-second maximum, and requires release before another boop. Timed mode uses the same artwork for the configured interval. The selected expression is preserved underneath the temporary reaction. Boop takes priority over speech/blink overlays; its lifetime continues while the face is asleep or hidden.

The initial prototype is Feronium's original-style IR module, after checking its supply/output levels. The plan includes a documented QRE1113 candidate but does not assume it will work through tinted plastic or thick fur. Test the actual visor, paw, motion and lighting first. Capacitive touch plus differential piezo remains a fallback; its extra front end and classifier were not installed or implemented prematurely.

The 8.1 hardware-random blink implementation is retained unchanged. Each sequence consumes fresh Teensy hardware entropy rather than a seeded software random generator. Default ordinary pauses range from about two to six seconds and cluster near four. Closing takes 60–90 ms, the closed hold 20–35 ms, and reopening 130–180 ms. About 8% of sequences have one follow-up blink, with no chain of repeated doubles. Entropy is acquired without making the face wait for a blocking random read. This is a human-inspired animation, not a universal physiological model.

The existing 100,000-schedule test still passes: mean ordinary pause 4,000.29 ms, mean motion duration 257.354 ms, and modeled rate about 15.04 blinks/minute including doubles. Simulation tests the timing sampler; physical hardware entropy/electrical behavior remains a bench check.

## Why SPI remains at 4 MHz

SPI is the speed at which the Teensy sends display instructions. The [MAX7219 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7219-MAX7221.pdf) permits up to 10 MHz under its specified conditions, so both 4 MHz and 8 MHz can be valid. The practical difference is timing margin in the complete helmet: fourteen chained drivers, wiring, level shifting, grounding and edge quality may be less forgiving than a short laboratory connection.

At 4 MHz each bit has twice as much time as at 8 MHz. This is a conservative starting point while the actual chain is unmeasured; it is not evidence that 8 MHz fails, and it does not guarantee that poor wiring works. A full eight-row update sends 14 × 8 × 16 = 1,792 bits. Nominal wire time is 448 microseconds at 4 MHz versus 224 microseconds at 8 MHz. The difference is about 1.4% of the 16 ms rendering interval, so the slower default costs little while providing margin.

Conflicting recommendations usually answer different questions: an 8 MHz recommendation follows the IC's rated capability and prioritizes transfer speed; a 4 MHz recommendation allows margin for the assembled wiring. Scope the far end and test the full chain before raising it. The 4 MHz setting came from the earlier upgrade and was retained in 8.2.

## Power and hardware decisions

- External protected 15 V / 3 A USB-C PD input, with separate face 5 V, core 5 V and auxiliary 3.3 V converters. Branch protection and an isolated UART prevent a local peripheral fault from directly shorting/backfeeding the controller.
- Core 5 V powers Teensy and the 74AHCT125. Hardware power-good/pilot gating keeps buffer outputs disabled when matrices are unpowered. Teensy inputs remain 3.3 V only.
- The display ceiling is approximately 4.48 A LED load plus overhead at the datasheet's maximum segment current. Measure real RSET, brightness/load, inrush, voltage drop and enclosure temperature before setting protection. No fixed 5 A limiter is approved.
- The candidate INA228 breakout has a **15 mΩ shunt**; firmware calibration matches it. Delivered energy is measured before all converters. Remaining percentage is an estimate with a known-full/calibrated reference, not USB-PD voltage-derived SOC.
- The candidate sensor/screen set is SHT40, INA228 and a 128×64 SPI SSD1306, with optional external bank-case NTC. Near-eye readability, heat thresholds and physical placement need fitting and measurement.
- Start removable wiring with keyed XT30-class power and locking signals. The magnetic 14-contact map remains provisional until contact current, keying, partial engagement, pilot timing and detached-side discharge are qualified.
- A paw cell must match the XIAO's charger current as well as measured runtime. The implementation intentionally starts with predictable awake RF; it does not claim low-power sleep performance.

The [engineering specification](ENGINEERING_PLAN.md) contains component links and load/runtime calculations. The [wiring package](HARDWARE_WIRING.md) provides five connection-level schematic sheets. [The contact map](../hardware/connector-map.csv) and [wireless protocol](WIRELESS_PROTOCOL.md) define their interfaces. These are prototype schematics, not a KiCad/ERC/PCB fabrication release or a final purchase list.

## Validation

| Check | Result |
| --- | --- |
| Sanitized native firmware suite | PASS with AddressSanitizer, UndefinedBehaviorSanitizer and warnings treated as errors |
| Original archive faces | Happy, surprised, angry and `vwv`: **zero differences** among 896 logical and 896 physical pixels per face |
| UART/application protocol | 20,000 framed round trips plus corruption, truncation, overflow, expiry, replay/dedup, rate, epoch/session and sensor/provisioning conversion checks |
| Actual Teensy behavior in host simulation | Commands/ACKs, held/timed/remote boops, disconnect, mute/retry, pilot, bad rail/reinitialize and stuck-ADC isolation pass |
| Settings/tools | Existing-save migration, interrupted saves, strict USB parsing, preview/profile integration and pairing tool tests pass |
| Real Teensy builds | All eight existing sensor combinations and five additional radio/dock combinations compile with zero warnings |
| Actual Mbed TLS execution | Cookie handshake, packet loss, nonblocking writes, tampered tags, replay, prior-session ciphertext and wrong keys pass using the shared adapter and real library |
| Real ESP32 targets | Helmet and paw compile with zero warnings; resolved SDK/version, console, 1 KB TLS buffer and generated 1500 KiB application partition checks pass |
| Browser controls | Applied timed boop/1,200 ms, muted/re-enabled microphone, then restored held/proximity/6,000 ms defaults; observed the resulting UI states |
| Export | Default Teensy HEX and both ESP32 role images generated with source identity and artifact hashes |

Current source fingerprint: `5da83eba4c8847decdb2928992e5de1762436d441b15e56527653c8fc40d4cff`. This identifies the tested firmware, tooling, profiles and dependency lock despite an uncommitted working tree. Documents outside those source paths do not change it.

| Target | Flash / application use | Static variables |
| --- | ---: | ---: |
| Default Teensy, optional hardware off | 72,700 bytes flash | 14,304 bytes RAM1 variables |
| Teensy, all five optional hardware features | 86,012 bytes flash | 16,864 bytes RAM1 variables; 441,888 bytes reported available for local variables |
| Helmet ESP32-C3 | 936,628 bytes / 1,536,000-byte application partition | 36,172 bytes reported static RAM |
| Paw ESP32-C3 | 863,176 bytes / 1,536,000-byte application partition | 34,292 bytes reported static RAM |

ESP static-memory reports do not include all runtime heaps, DTLS allocations, RTOS stacks or radio memory. Peak heap/latency measurements remain part of the two-board test.

Evidence is generated in `build/tests-8.2.log`, `build/dtls-test.log`, `build/feature-results.json`, `build/extensions-results.json`, `build/current-build.json` and `build/esp32-build.json`. The final cross-check is recorded in `build/upgrade-8.2-validation.json`, which is retained locally outside repository checkouts. It covers matching source identities, all 13 exported artifact hashes and preserved artwork/blink/renderer sources. Default Teensy artifacts are in `build/teensy40/`; ESP role artifacts are in `build/esp32/{helmet,paw}/`. Build/test scripts do not upload either platform. [ESP32 setup/provisioning instructions](../firmware/esp32/README.md).

## Remaining physical prototype work

Start with current-limited power/load qualification, then IR alone through the real visor. Decide held versus timed from those interactions. Next qualify the two radios, pairing/reconnect and the isolated UART before adding Tier 2 peripherals. Measure RF latency/range and paw current in the suit, then size the cell. Fit the screen, calibrate energy/sensors, and qualify the connector last.

Host tests cannot prove antenna range, final current limits, four-hour runtime, charger compatibility, thermal comfort, visor IR transmission, screen focus or hot-plug safety. Fault behavior is implemented and tested in software; electrical isolation depends on building and validating the specified hardware. The original face remains available throughout these staged prototypes.

# ESP32-C3 helmet and paw prototype firmware

Two ESP-IDF targets: **helmet**, Espressif ESP32-C3-DevKitM-1; **paw**, Seeed XIAO ESP32-C3. ESP32 never drives the outward LED face. The Teensy sketch and all original artwork remain separate.

The first firmware is unpaired, RF power saving is off for measurable latency, and optional peripherals are disabled. Build success is not a claim of RF range, sensor calibration, safe power, or readable near-eye focus. No physical upload has been performed. Read [the engineering plan](../../docs/ENGINEERING_PLAN.md) and [wiring package](../../docs/HARDWARE_WIRING.md) before connecting supplies.

## Build

Pinned platform: PlatformIO 6.1.19, `espressif32@7.1.3`, ESP-IDF 6.1.0, bundled Mbed TLS 4.1.0. This workspace uses a local isolated environment:

```sh
python3 -m venv build/esp32-venv
build/esp32-venv/bin/python -m pip install platformio==6.1.19
python3 scripts/build-esp32.py
```

PlatformIO installs the platform and its required Python/compiler dependencies on first use. `PIO` can name another installed PlatformIO executable. The wrapper disables IDF's external component manager because this project has no external managed components, avoiding unnecessary network/process inspection. Firmware uses only the ESP-IDF's existing drivers, ESP-NOW, NVS and Mbed TLS/PSA.

Both targets build to `build/esp32/{helmet,paw}`; generated sdkconfigs also stay in `build/`. Paw console is native USB Serial/JTAG; helmet console is its onboard USB-to-UART bridge. Pins are fixed in `src/DeviceConfig.h` for those specific boards. Do not reuse the same C3 GPIO map on a different board without checking its schematic.

`build/esp32-build.json` records source identity, artifact SHA-256, the actual 1500 KiB factory partition, resolved SDK/security settings and build result; `build/esp32-build.log` contains the exact compiler output. The wrapper checks the board's console choice, DTLS options and 1024-byte TLS buffers as well as dependency versions. A run whose sources changed is explicitly failed as current evidence even if its compilation succeeded. Real target compilation and native cryptographic tests are separate checks. If defaults are changed, regenerate the affected `build/sdkconfig.helmet` or `build/sdkconfig.paw` rather than silently retaining old generated settings.

## Upload later on the bench

Use the correct USB port and role; disconnect external MCU supply first unless power isolation has been qualified. These commands deliberately are **not** part of the build/test scripts:

```sh
IDF_COMPONENT_MANAGER=0 PLATFORMIO_CORE_DIR="$PWD/build/platformio" \
  build/esp32-venv/bin/pio run --project-dir firmware/esp32 -e helmet -t upload --upload-port /dev/cu.YOUR_HELMET
IDF_COMPONENT_MANAGER=0 PLATFORMIO_CORE_DIR="$PWD/build/platformio" \
  build/esp32-venv/bin/pio run --project-dir firmware/esp32 -e paw -t upload --upload-port /dev/cu.YOUR_PAW
```

Teensy needs `WirelessPaw` enabled only when its isolated 115200-baud UART is connected. Existing local face controls operate with an absent or unpaired receiver. The paw cell/charger and power rails must be qualified before on-body testing.

## Pairing

After each firmware is running, physically unlock: helmet BOOT held for two seconds; paw all three buttons held for three seconds. Holding BOOT at reset enters the chip's bootloader; unlock is an **after-boot** operation. The console prints a 60-second provisioning window. Release buttons, close serial monitors, and run:

```sh
python3 scripts/pair-paw.py --helmet /dev/cu.YOUR_HELMET --paw /dev/cu.YOUR_PAW --channel 6
```

The tool verifies board roles/MACs, generates unique keys from OS randomness, provisions both, then both restart. It saves no secrets to disk and does not flash boards. Partial provisioning is safe but disconnected; unlock both and rerun to install a fresh common pair. `FORGET` is also physically unlocked and removes the configured pair; re-key both if a paw is lost.

## Local console commands

One ASCII line, maximum 191 bytes, 2-second partial-line timeout. JSON responses have `ok`; boot/event lines can occur between them. There is no secret readback or logging.

| Command | Meaning |
| --- | --- |
| `INFO` | Board role/MAC, paired/connected state, peripheral mask, usable energy calibration, drop counter, sensor faults and optional paw cell voltage (`null` when unavailable); no physical unlock required |
| `PAIR peer12hex psk64hex lmk32hex pmk32hex channel` | Physically unlocked local provision; ordinarily use the tool |
| `FORGET` | Physically unlocked clear pair and restart |
| `CONFIG mask usable_energy_mwh` | Physically unlocked peripheral configuration and restart; use measured delivered energy |
| `FULL` | Physically unlocked known-full battery reference; requires valid helmet energy monitor and nonzero calibrated capacity |

Helmet peripheral mask: SHT40=1, INA228=2, SSD1306 SPI screen=4, case NTC=8. Example **only after wiring and calibration**: mask 7 enables env/energy/screen; case NTC stays off. `CONFIG 7 60000` means 60,000 mWh of experimentally qualified energy, not a default promise that this bank delivers 60 Wh. Every mask starts at zero. Paw mask 2 enables the optional protected cell-voltage ADC divider; it reports voltage, not a claimed cell percentage. No helmet peripherals exist on the paw.

Remaining percentage is deliberately unknown after an unmeasured reset/gap, sensor failure or missing full reference. `FULL` must correspond to an actually charged/replaced bank; it is not a “make battery say 100%” convenience. The estimate tracks INA228 hardware input energy. Firmware matches Adafruit #5832's **15 mΩ** shunt; other boards require recalibration.

## Paw operation and failures

- Next/previous on button release cycle the six existing expressions.
- Both navigation buttons held about 1.2 seconds sleep/wake without navigation first.
- Boop button requests the original `vwv` for 1.2 seconds; physical IR still defaults to original held behavior on the Teensy.
- All three buttons held three seconds unlock local provisioning and suspend remote actions.
- Connection loss clears pending commands. Release all buttons after reconnect before controlling again.

The paw only reports an execution ACK after Teensy applies/rejects it. Commands have a 300 ms receiver-ticket expiry and sequence dedup. Two-second disconnect detection and a fresh DTLS session protect reconnects. Wrong keys, unknown peers, old sessions and malformed packets cannot become face actions. ESP/I2C/OLED crashes do not take over or directly block Teensy rendering. Electrical rail/backfeed isolation still needs the hardware specified in the wiring plan.

## Checks

```sh
bash scripts/test-firmware.sh
bash scripts/test-dtls.sh
python3 scripts/check-features.py
python3 scripts/check-extensions.py
python3 scripts/build-esp32.py
```

Radio range, on-body attenuation, latency, replay captures, power draw, charging, focus, thermal behavior and hot-plug contacts require real hardware. The next physical stage starts with current-limited power and original IR, then a two-radio bench setup.

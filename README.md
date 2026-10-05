# Protogen helmet — Teensy 4.0

Open **[firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino](firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino)** in Arduino IDE.
This is the current sketch for the 14-module MAX7219 face. All optional sensors
are disabled initially; expressions, the button, blink, and angry glitch work
without them. Your manually mapped bitmap graphics are preserved.

Firmware **8.2** retains expression transitions, hardware-random blink timing, sleepy/wink
expressions, buffered audio, runtime USB settings, and the local preview/configuration
tool. It adds optional authenticated paw control, protected dock support,
nonblocking IR acquisition, held/timed original boop controls, microphone fault isolation,
and separate ESP32 status firmware. See [the complete upgrade summary](docs/UPGRADE.md) for what changed and why.

The original four resting faces are pixel-identical to the ZIP archive, including
the rotated physical module data. See [the archive comparison](docs/ARCHIVE_FACE_COMPARISON.md).
Ordinary blink pauses now vary between 2 and 6 seconds, centered near 4 seconds;
the eyelids close quickly and reopen more slowly, with occasional double blinks.
Previously saved timing stays saved: use **Restore defaults**, then **Save**, or
apply and save the updated Everyday profile to adopt the new defaults.

Preview the actual firmware without connecting a helmet:

```sh
python3 scripts/helmet-tool.py
```

Then open **http://127.0.0.1:8765**. After uploading, add `--port` with the Teensy's
serial device to configure the physical helmet. Changes apply temporarily until
explicitly saved. Saved startup/settings override compiled defaults on reboot.

The [engineering specification](docs/ENGINEERING_PLAN.md), [schematic/wiring package](docs/HARDWARE_WIRING.md),
[wireless protocol](docs/WIRELESS_PROTOCOL.md), and [8.2 change summary](docs/IMPLEMENTATION_8_2.md)
cover the new hardware and software. New hardware is disabled until physically qualified;
no upload or wiring change was performed. [ESP32 setup instructions](firmware/esp32/README.md).

## Project layout

- `firmware/PROT_OS7_MAX7219/`: the one active sketch, including its MAX7219 driver and license.
- `firmware/esp32/`: separate helmet/paw ESP32-C3 targets; DTLS, ESP-NOW and wearer status.
- `docs/UPGRADE.md`: current features, full change summary, USB protocol, configuration, and validation.
- `tools/helmet/`, `scripts/helmet-tool.py`, `profiles/`: local Face Studio and named settings profiles.
- `toolchain.lock.json`: pinned CLI, core, and library versions.
- `docs/README_RECOVERY.md`, `docs/CODE_AUDIT.md`, `docs/OPTIMIZATION.md`: historical recovery, audit, and optimization notes.
- `docs/FINAL_CHECK.md`: earlier recovery/build review.
- `docs/historical/PROT_OS7_Nano_Schematic.png`: original Nano schematic; use the Teensy pinout below for this build.
- `assets/head-model/`: both original head-model ZIP parts, including fabrication files and build instructions.
- `archive/PROT_OS7_original.zip`: original software and bundled libraries, kept for recovery.
- `scripts/build.sh`: reproducible Teensy 4.0 CLI build.
- `build/`: generated firmware and check logs; can be regenerated.

## Upload from Arduino IDE on this Mac

1. Disconnect helmet battery/external power for the initial USB upload. Connect
   the Teensy 4.0 using a **micro-USB data cable**. A charging-only cable will not work.
2. Open the sketch linked above. All `.ino` tabs and the `src/` folder must stay
   together inside `PROT_OS7_MAX7219`.
3. In Boards Manager, confirm **Teensy 1.62.0** is installed. This Mac already has it.
   For another computer, add `https://www.pjrc.com/teensy/package_teensy_index.json`
   to Additional Boards Manager URLs, then install Teensy.
4. Select **Tools → Board → Teensy → Teensy 4.0**. Set **USB Type: Serial**,
   **CPU Speed: 600 MHz**, and **Optimize: Faster** (the normal defaults used here).
5. Library Manager must have **arduinoFFT 2.0.4**, **Adafruit GFX Library 1.12.6**,
   and **Adafruit BusIO 1.17.4**. These are already installed on this Mac. The
   MAX7219 driver is bundled inside this sketch and needs no separate installation.
6. Select the Teensy entry under **Tools → Port** if one appears, then click **Upload**.
   Teensy Loader should open. If automatic reboot/upload cannot find the board,
   briefly press the **button on the Teensy itself** when the loader is ready.
   This is separate from the helmet's expression button on pin 2. A new board may
   not show a serial port until it has been programmed.
7. Wait for programming to finish and the board to reboot. Once the display supply
   is connected correctly, the face should start in the happy expression. The
   expression button cycles happy → surprised → angry → vwv → sleepy → wink → happy on short release.

PJRC references: [Teensy installation](https://www.pjrc.com/teensy/td_download.html),
[upload and program-button workflow](https://www.pjrc.com/teensy/td_usage.html).

## Wiring for the current sketch

| Connection | Teensy 4.0 pin |
| --- | --- |
| MAX7219 first module DIN | 11 / MOSI |
| MAX7219 CLK | 13 / SCK |
| MAX7219 CS / LOAD | 8 |
| Expression button | 2 to GND (internal pull-up) |
| RGB indicator red / green / blue | 5 / 9 / 10, with suitable current limiting |
| Optional visor sensor | 3; internal pull-up, LOW displays face, HIGH blanks it |
| Optional boop sensor | A7 / 21 |
| Optional microphone | A0 / 14 |
| Ground | Common between Teensy, displays, and sensors |

Chain each module's DOUT to the next module's DIN, with clock, LOAD, and ground
connected through the 14-module chain. Preserve your existing physical module
order and orientations. The default brightness is low (`Settings::Brightness = 1`); saved/runtime settings can override it.

The MAX7219 modules need a suitably sized regulated supply, normally 5 V; do not
power the 14 displays from the Teensy's 3.3 V pin. Teensy GPIO is **3.3 V only and
not 5 V tolerant**. For bare MAX7219 inputs powered at 5 V, use a **3.3 V-to-5 V
logic buffer on DIN, CLK, and LOAD** (unless your modules already include one).
The specified HIGH input minimum is 3.5 V, so direct 3.3 V GPIO is outside the
guaranteed range. See the [MAX7219/MAX7221 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max7219-max7221.pdf).
Leave the final module's DOUT disconnected from Teensy. Sensor outputs must
stay within 0–3.3 V. The RGB values assume active-high/common-cathode-style drive;
check your indicator wiring and limit each directly driven channel to PJRC's
recommended 4 mA GPIO output current. PWM brightness alone does not limit peak current.

Keep external 5 V off Teensy VIN while USB is connected unless VIN/VUSB have been
properly isolated according to PJRC's instructions. You may power the matrices
separately with a shared ground, without tying their 5 V rail to Teensy VIN.
See [PJRC's Teensy 4.0 electrical and power details](https://www.pjrc.com/store/teensy40.html).

## Button controls and settings

- **Tap and release:** cycle happy → surprised → angry → vwv → sleepy → wink → happy.
- **Hold about 1.2 seconds:** put the display to sleep. Keep holding without repeats.
- **Tap while asleep:** wake to the same expression. A hold can also wake it.
- With a visor sensor enabled, the face stays off while the visor input is HIGH,
  even if you wake it with the button.

Sleep switches the MAX7219 panels and RGB indicator off and pauses boop/microphone
work. The Teensy remains running to detect the wake button. Sleep and the current
expression are session-only; a saved startup expression is used after power cycling.

Use Face Studio to adjust brightness, startup expression, microphone response,
boop thresholds, transitions, and blink timing. **Apply** is temporary; **Save**
persists the applied configuration. Quiet-room calibration can also be saved.
Settings are validated and protected by alternating EEPROM records with a CRC.

[Settings.h](firmware/PROT_OS7_MAX7219/Settings.h) supplies defaults and the
compile-time control timings. Sensor-enable switches remain at the top of the
main `.ino`. See [the upgrade guide](docs/UPGRADE.md) for all settings and controls.

## Enable sensors after the basic face works

At the top of `PROT_OS7_MAX7219.ino`, remove the leading `//` only from the
features whose hardware is connected, then upload again:

```cpp
#define VisorSensor
#define BoopSensor
#define Microphone
```

Enable one at a time. The boop triggers at or below **500** and releases/rearms at **550** or above;
the firmware must observe a released reading before the first boop. Verify its
polarity against your sensor. The visor input uses a 3.3 V internal pull-up and
supports open-drain sensors; an unconnected enabled visor input blanks the face.
Remove or translate any sensor-module pull-up to 5 V before connecting its output.
Microphone sensitivity starts at **65**. Use quiet-room calibration and adjust
the response for your microphone gain and helmet acoustics. For a MAX4466,
use a supply/output configuration that stays within Teensy's input range.

Test visor blanking and restoration, sensor reactions, every expression, blinking,
and microphone animation on the actual helmet. A successful compile confirms
software compatibility; it cannot verify your wiring, power supply, sensors, or
physical panel order. If panels show rotated or misplaced parts, check chain order
and tile rotations before changing your intentional bitmap data.

## Build from Terminal

From the project root:

```sh
./scripts/build.sh
```

The HEX for the **currently selected feature flags** is exported under `build/teensy40/`;
the supplied sketch has all optional sensors disabled. The build explicitly selects
Teensy 4.0, Serial USB, 600 MHz, and Faster optimization. Use Arduino IDE for
normal editing/upload; the HEX can also be opened in Teensy Loader if you prefer
loading an already compiled image.

## Audit and regression checks

See [the current upgrade and validation guide](docs/UPGRADE.md). The earlier
[independent audit](docs/INDEPENDENT_CODE_AUDIT.md) describes the preserved
pre-upgrade baseline; its two sensor behavior findings are addressed in 8.0.
Optional sensors remain disabled until enabled in the sketch.

```sh
./scripts/test-firmware.sh        # Sanitized host behavior + tooling/preview integration
python3 scripts/check-features.py # All eight real Teensy sensor combinations
python3 scripts/check-extensions.py # Five new UART/dock combinations
bash scripts/test-dtls.sh          # Actual Mbed TLS authentication/replay tests
python3 scripts/build-esp32.py     # Helmet and paw ESP32 builds
./scripts/build.sh                # Selected features; export HEX + source manifest
```

Build scripts check the pinned dependencies in `toolchain.lock.json` and require
zero compiler warnings. Every run records its sources, selected dependencies,
commands, status, and memory metrics in `build/runs/`. The latest attempt is in
`build/current-build.json` or `build/feature-results.json`; previous exported
firmware retains its own manifest. No check uploads firmware.

Host tests and preview require Clang/C++17, Python 3, and the installed arduinoFFT
source; set `ARDUINO_FFT_SRC` if it is outside
`~/Documents/Arduino/libraries/arduinoFFT/src`. `ARDUINO_CLI` overrides the CLI
executable path or command, and `CXX` overrides the host compiler. Source feature
flags are set in temporary copies for test coverage.

SPI now defaults to **4 MHz**. Audio uses hardware-triggered ADC sampling with
buffering and diagnostics; real interrupt latency and electrical behavior still
need helmet validation. Test the new transitions, every expression, microphone
response, visor restoration, and boop expiry on the assembled helmet. Compiling
and simulation cannot verify the wiring, power supply, or physical appearance.

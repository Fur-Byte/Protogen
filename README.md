# Protogen helmet — Teensy 4.0

Open **[firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino](firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino)** in Arduino IDE.
This is the current sketch for the 14-module MAX7219 face. All optional sensors
are disabled initially; expressions, the button, blink, and angry glitch work
without them. Your manually mapped bitmap graphics are preserved.

## Project layout

- `firmware/PROT_OS7_MAX7219/`: the one active sketch, including its MAX7219 driver and license.
- `docs/README_RECOVERY.md`: dependencies, actual Teensy pinout, feature configuration, and hardware checks.
- `docs/CODE_AUDIT.md`: latest full audit, fixes, validation, and remaining hardware checks.
- `docs/OPTIMIZATION.md`: current Teensy optimization changes, measurements, and tests.
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
   expression button cycles happy → surprised → angry → vwv → happy on short release.

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
order and orientations. The firmware uses low brightness (`Settings::Brightness = 1`).

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

- **Tap and release:** cycle happy → surprised → angry → vwv → happy.
- **Hold about 1.2 seconds:** put the display to sleep. Keep holding without repeats.
- **Tap while asleep:** wake to the same expression. A hold can also wake it.
- With a visor sensor enabled, the face stays off while the visor input is HIGH,
  even if you wake it with the button.

Sleep switches the MAX7219 panels and RGB indicator off and pauses boop/microphone
work. The Teensy remains running to detect the wake button. Sleep and the selected
expression are not saved across power cycles.

Edit [Settings.h](firmware/PROT_OS7_MAX7219/Settings.h) to adjust brightness,
startup expression, microphone sensitivity, boop thresholds, and animation/control
timings. Defaults preserve the low brightness and existing animation intervals.
The feature-enable switches remain at the top of the main `.ino`.

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
Microphone sensitivity starts at **65** and
will need adjustment for microphone gain and helmet acoustics. For a MAX4466,
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

See [the latest code audit](docs/CODE_AUDIT.md) for findings and validation, and
[the earlier optimization report](docs/OPTIMIZATION.md) for historical measurements.
Optional sensors remain disabled until you enable them.

```sh
./scripts/test-firmware.sh       # Host simulation with address/undefined-behavior checks
python3 scripts/check-features.py # Compile all eight sensor combinations in temporary copies
```

The host test requires Clang, Python 3, and the installed arduinoFFT source; set
`ARDUINO_FFT_SRC` if it is outside `~/Documents/Arduino/libraries/arduinoFFT/src`.
Feature builds use the same Arduino CLI installation as the normal build, with
`ARDUINO_CLI` available as an executable-path or command-name override. Both checks
set all eight sensor combinations in temporary copies, even if you have already
enabled sensors in your sketch. Target build results are in `build/feature-results.json`
and `build/feature-checks.log`. Neither check uploads firmware.

The 2026-09-28 audit was re-verified on 2026-10-03: all three checks pass, the
exported HEX was rebuilt from current sources, and the recorded `Icons.h` digest
and SPI clock were corrected. One item is still open and needs the helmet:
`Settings::SpiFrequencyHz` is now **8 MHz** where the sketch previously used
4 MHz. That is under the MAX7219 limit but leaves less timing margin across the
14-module chain, and the host test still models 4 MHz. Check the assembled chain
for display artifacts, or set it back to 4000000UL and rebuild. Details are in
the [evidence-integrity section](docs/CODE_AUDIT.md#evidence-integrity-pass-2026-10-03).

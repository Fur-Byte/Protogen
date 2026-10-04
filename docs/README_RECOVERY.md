# PROT_OS7_MAX7219 recovery notes

The canonical sketch is now `../firmware/PROT_OS7_MAX7219/`. Open its
`PROT_OS7_MAX7219.ino` file in Arduino IDE. Keep all sketch tabs and `src/`
together. The original project ZIP is retained under `../archive/`; it is a
historical backup, not the sketch to upload. Both head-model ZIP parts are in
`../assets/head-model/`.

Every byte of the manually mapped `Icons.h` has been preserved during the final
organization. Expressions, mouth mapping, and per-tile rotations remain intact.

## Dependencies and setup

Target: **Teensy 4.0**, FQBN `teensy:avr:teensy40`.

Verified package versions on this Mac:

- Teensy platform **1.62.0** from PJRC Boards Manager.
- Native arm64 `teensy-compile` **15.2.1**, Arm GNU Toolchain 15.2.Rel1.
- Arduino CLI **1.5.1** (bundled with Arduino IDE).
- Library Manager: **arduinoFFT 2.0.4**, **Adafruit GFX Library 1.12.6**,
  **Adafruit BusIO 1.17.4**.
- The project's original Max72xxPanel driver is included directly in the sketch
  under `src/Max72xxPanel/`, with its BSD license. No separate Max72xxPanel install
  is needed. Its header now includes Adafruit_GFX and exposes inherited `write`
  overloads, resolving the previous overloaded-virtual warnings.

Arduino's `ctags` 5.8-arduino11 helper still requires Rosetta on this Mac.
Rosetta was installed successfully; the ARM firmware compiler itself is native
arm64. Do not substitute the Mac's clang compiler for `arm-none-eabi-g++`.

From the project root, run `./scripts/build.sh`. It builds with all warnings
and a clean temporary build directory, saves the log in `build/compile.log`,
and exports firmware to `build/teensy40/`. Temporary object files are removed.
The CLI path can be overridden with the `ARDUINO_CLI` environment variable.

See [the upload guide](../README.md) for Arduino IDE instructions and
[the code audit](CODE_AUDIT.md) for the latest actual check results.

## Historical source corrections

Trailing-backslash section comments were corrected during the toolchain repair:
C++ line splicing had hidden function declarations. The final review also fixed
visor restoration: after blanking clears the framebuffer, `rising` requests a
full redraw, and `boopActive` is cleared so an active boop can be redrawn.

## Optional feature flags

All optional features are **off by default**. Uncomment only the features that
are connected in PROT_OS7_MAX7219.ino:

- VisorSensor: pin 3 with an internal 3.3 V pull-up. LOW shows the face; HIGH
  blanks it. Supports open-drain sensors; an unconnected enabled input stays HIGH.
- BoopSensor: analog sensor on A7. The existing threshold treats a reading
  at or above 550 as released and a reading at or below 500 as a boop.
  A released reading must be observed to arm the first reaction. The gap prevents
  threshold chatter; see `Settings.h`.
- Microphone: analog microphone output on A0. The input must stay in the
  Teensy's 0–3.3 V range.

When a feature is disabled, its sensor is not read. Features may be enabled
individually or together. Adjust the boop threshold or comparison only if the
connected sensor has a different range or polarity.

## Teensy 4.0 pinout used by this source

| Function | Teensy pin | Notes |
| --- | ---: | --- |
| Button | 2 | Button connects between pin and GND; configured INPUT_PULLUP, active-low, polled stable-state debounce |
| Visor sensor | 3 | Optional INPUT_PULLUP; HIGH blanks the display |
| RGB indicator, red | 5 | PWM output |
| RGB indicator, green | 9 | PWM output |
| RGB indicator, blue | 10 | PWM output |
| Boop / IR sensor | A7 | Optional analog input; readings at or below 500 trigger the reaction |
| Microphone | A0 | Optional analog input; also used for FFT samples |
| MAX7219 chip select | 8 | pinCS in the sketch |
| MAX7219 data / DIN | 11 | Teensy 4.0 SPI MOSI |
| MAX7219 clock / CLK | 13 | Teensy 4.0 SPI SCK |
| MAX7219 MISO | — | Not used |

The matrix driver uses hardware SPI with its default Teensy SPI pins, MOSI 11
and SCK 13. Pin 8 is the current chip-select GPIO. No source pin assignment
needs to change for the Teensy 4.0. The 14 per-panel rotation settings in
setup() are physical layout settings and should be checked against the
assembled helmet.

### Teensy 4.0 migration note

The included `historical/PROT_OS7_Nano_Schematic.png` is the historical Nano-era schematic. It
is retained, but it may not match this Teensy controller, the pinout above, or
the current wiring.

## Electrical and hardware notes

- Teensy 4.0 GPIO and analog inputs are **not 5 V tolerant**. Keep all signals
  entering Teensy pins within 0–3.3 V.
- MAX7219 inputs at a 5 V supply require at least 3.5 V for a guaranteed HIGH.
  Buffer DIN, CLK, and LOAD from 3.3 V to 5 V unless the modules already include
  this translation. The final DOUT is unused; do not connect it back to Teensy.
  See the [MAX7219/MAX7221 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max7219-max7221.pdf).
- Provide the matrix modules with a suitable supply for the 14-panel chain and
  connect the Teensy and display grounds together. Do not use the Teensy's
  3.3 V pin as the matrix supply.
- Use appropriate current limiting or driver hardware for the external RGB
  indicator and the connected sensors. PJRC recommends at most 4 mA from a GPIO;
  the RGB PWM duty cycle does not limit the instantaneous LED current.
- Confirm sensor outputs are safe for 3.3 V inputs before enabling their
  feature flags.

PJRC's Teensy 4.0 pin and voltage details:
[Teensy 4.0 technical page](https://www.pjrc.com/store/teensy40.html).

## FFT and spectrum behavior

The microphone path targets 4,000 samples per second, collecting a 64-sample
frame incrementally with at least 250 microseconds between reads. The loop does
not wait for a 16 ms block. Sampling is cooperative: loop work introduces jitter
and lowers the effective rate below the 4 kHz target. A delay of a full period
beyond a deadline restarts the partial frame. Each next deadline is based on the
actual read time, preventing catch-up bursts. Pausing clears amplitude history;
resuming initializes the DC estimate from the new sample. The FFT removes each
frame's mean before applying its cached forward Hamming window and uses float
buffers with the arduinoFFT 2.0.4 API. Bins 0–31 (the lower 32 bins) are mapped
into the four 8-bin display groups; the Nyquist bin
32 is not rendered.

After magnitude conversion, bin 0 is replaced with bin 4 and bin 1 with bin 8.
This existing low-frequency presentation is retained in addition to DC removal.
Amplitudes keep the original approximate 0–800 to 1–8 row mapping, clamped to 1–8. Within every
group, the right and left columns use opposite bit directions to retain the
mirrored mouth spectrum.

## Hardware test checklist

1. Verify the Teensy board selection and upload the sketch with all optional
   features disabled.
2. Check the full 14-panel chain, the existing physical rotations, and the
   normal, surprised, angry, and vwv expressions.
3. Tap/release the button and confirm one expression step, including wraparound.
   Hold about 1.2 seconds to sleep, then tap to wake to the same expression.
   Confirm the visor input hides the face only when configured and does not flash
   an old expression when reopening.
4. Enable and test the boop sensor; check its idle level and threshold polarity.
5. Enable and test the microphone; confirm mouth motion and that all FFT bars
   stay within the matrix.
6. Observe the blink and angry glitch animations, plus the RGB indicator, while
   each optional sensor feature is tested alone and in combination.
7. Recheck power, shared ground, sensor voltage levels, and any required
   MAX7219 logic-level interfacing before wearing the helmet.

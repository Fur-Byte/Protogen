# Final project check — 2026-09-26

Historical recovery review. See [the code audit](CODE_AUDIT.md) for current
validation, and [the optimization report](OPTIMIZATION.md) for the intervening work.

The reorganized sketch completed a clean Arduino CLI build for **Teensy 4.0**
with **zero compiler errors and zero warnings**. No board was detected by
`arduino-cli board list`; firmware was not uploaded or tested on hardware.

## Build environment and output

- Mac host: arm64.
- Arduino CLI: 1.3.1, bundled with Arduino IDE.
- Teensy platform: 1.62.0.
- Native arm64 compiler: Arm GNU Toolchain 15.2.Rel1, GCC 15.2.1.
- Arduino ctags 5.8-arduino11 runs using the installed Rosetta runtime.
- Target FQBN: `teensy:avr:teensy40`; Serial USB, 600 MHz, Faster optimization.
- Dependencies: arduinoFFT 2.0.4; Adafruit GFX Library 1.12.4; Adafruit BusIO 1.17.4.
- MAX7219 driver: project-local original driver, `firmware/PROT_OS7_MAX7219/src/Max72xxPanel/`.
- Run `./scripts/build.sh` from the root. It invokes `arduino-cli compile
  --fqbn teensy:avr:teensy40 --warnings all --clean --build-path <temporary-dir>
  --output-dir <project>/build/teensy40 <project>/firmware/PROT_OS7_MAX7219`.
- Default-feature image: `../build/teensy40/PROT_OS7_MAX7219.ino.hex`.
- ELF inspected with `file`: ELF 32-bit LSB executable, ARM, EABI5, statically linked.
- Build evidence: `../build/compile.log`, `../build/feature-checks.log`,
  `../build/feature-results.json`. Optional-feature builds used disposable sketch
  copies; the real sketch and exported HEX still have optional features disabled.

## Sensor-option compilation checks

Each enabled-feature combination was compiled cleanly for Teensy 4.0.

| Features enabled | Compile | Warnings |
| --- | --- | --- |
| none | Pass | 0 |
| Microphone | Pass | 0 |
| BoopSensor | Pass | 0 |
| BoopSensor+Microphone | Pass | 0 |
| VisorSensor | Pass | 0 |
| VisorSensor+Microphone | Pass | 0 |
| VisorSensor+BoopSensor | Pass | 0 |
| VisorSensor+BoopSensor+Microphone | Pass | 0 |

## Static review

Read all seven sketch source/header files and the bundled matrix driver.
Checked button INPUT_PULLUP/FALLING configuration, the short volatile-event ISR
and interrupt-protected event transfer, elapsed-time arithmetic, feature guards,
left/right bitmap references, FFT array bounds, and sensor/display interactions.
The last review found and corrected the visor restoration issue described below.

- Checked all 34 `drawBitmap` call sites against declared bitmap byte counts.
- Enumerated 32 FFT bins and eight rows: every bit index is 0–7 and every
  destination byte index is 0–15.
- Confirmed 14 tiles and 112×8 drawing surface; the driver supports this size.
- Disabled features do not read their sensor inputs.
- Complete `Icons.h` SHA-256 remained unchanged throughout this cleanup:
  see the code audit for the current digest. At this point the file still
  carried the later-removed `buffer0L`/`buffer0R` buffers.

## Changes made during organization

- Moved the canonical sketch into `firmware/PROT_OS7_MAX7219/` and updated its
  documentation link. All sketch tabs remain together.
- Included the original bundled driver directly under sketch `src/` and changed
  the include to that local header. This prevents selection of a different global
  Max72xxPanel library by Arduino IDE or CLI. Kept its BSD license and attribution.
- Made that header self-contained with Adafruit_GFX and added
  `using Adafruit_GFX::write;`, resolving both inherited-write hiding warnings.
  Matrix drawing/transfer code and rotations are unchanged.
- Fixed visor unblanking: `rising = 1` requests a full face redraw and
  `boopActive = 0` allows an active boop to redraw after blanking erased the buffer.
- Kept the old schematic under `docs/historical/`; it describes the Nano build.
- Kept both head-model ZIP parts under `assets/head-model/`.
- Kept one original software ZIP under `archive/`. Before deleting the extracted
  duplicate folder and old GFX/driver copies, compared every substantive file
  against its archived entry: all were covered byte-for-byte.
- Removed the redundant bundled arduinoFFT copy after comparing its source files
  with installed arduinoFFT 2.0.4. The required Library Manager version is documented.
- Removed Finder `.DS_Store` files and empty old folders.
- Added root upload guide, updated recovery notes, this report, build script, and
  `.gitignore` for generated build files and Finder metadata.

## Hardware checks still required

Upload the default image first. Confirm power and common ground, signal levels,
the full 14-module chain, physical orientation, every expression, button cycling,
blinking, and the angry glitch. Enable connected sensors one at a time and test
visor blanking/restoration (including during a boop), boop threshold/polarity, and
microphone gain/sensitivity. No visual bitmap remapping was performed.

Compilation and static checks do not establish electrical or physical operation.
See the root README for upload steps and the recovery notes for the full pinout.

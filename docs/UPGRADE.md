# Existing-hardware upgrade — firmware 8.0

This upgrade uses the existing **Teensy 4.0, 14 MAX7219 modules, button, RGB indicator, and optional sensors**. The original four expression bitmaps, module order, rotations, pin assignments, fabrication assets, and archive are preserved. Optional sensors remain disabled in the supplied sketch. No firmware was uploaded and no physical helmet measurements were made.

## Changes and reasons

| Change | Result / reason |
| --- | --- |
| Animation state separated from rendering | Eyes and mouth compose into one logical frame. Speech owns the mouth, blinking owns the eye mask, and boop takes priority over both. Inputs, USB, and audio run independently of the display's 16 ms frame interval. |
| Expression transitions | A configurable close/swap/open transition hides the expression change. The default is 160 ms; 0 disables it. Rapid selections, sleep, and visor changes reset animations cleanly. |
| Six selectable expressions | Happy, surprised, angry, and vwv keep their exact resting artwork. Sleepy and wink derive eye masks from the happy artwork. The button cycles all six. |
| Natural blink timing | The default 9-second interval varies by up to ±2 seconds. Variation and the interval are configurable; timing remains deterministic and testable. |
| Optional idle glances | Small horizontal eye movements add character. Off by default so the original resting face remains exact. The Expressive profile enables them. |
| Buffered microphone acquisition | ADC0 is triggered by QuadTimer4 submodule 0 at a nominal 4 kHz. An ADC-completion ISR copies samples into a fixed 512-sample ring; the loop processes bounded batches. Rendering no longer schedules ADC conversions. |
| Separate sensor ADCs | Boop uses ADC1 on the same A7 pin. It cannot redirect the microphone's ADC0 conversion. ADC 9.1 is bundled with the pinned Teensy core. |
| Short-sound detection fixed | Activity is latched throughout each 64-sample frame. A six-ms burst early in the frame now activates speech just like one near its end. |
| Two mouth styles | Spectrum preserves the existing FFT mouth. Amplitude draws an opening mouth driven by a smoothed envelope, with configurable attack and release. It is an amplitude animation, not phoneme recognition. |
| Adaptive gate and quiet calibration | Quiet frames refine a runtime noise estimate. CALIBRATE measures half a second of actual samples, updates the configurable noise floor, and can be saved explicitly. The gate is max(sensitivity, 2 × noise floor + noise margin). |
| Overrun diagnostics | A full ring records dropped samples and discards discontinuous queued data before restarting a frame. STATUS reports drops, frames, envelope, threshold, and timer frequency. |
| Boop lifetime fixed | The six-second lifetime continues while asleep or hidden. Showing the visor does not restart the timer or bypass the release-before-retrigger rule. |
| Conservative SPI default | 4 MHz restores timing margin across the 14-module chain. Nominal wire time is modeled from the configured clock; a slower-transfer stress test is retained. |
| Runtime USB configuration | Settings can be changed without recompiling. Requests use fixed buffers, strict integer parsing, atomic validation, and bounded serial work. The face starts without waiting for USB. |
| Explicit persistent settings | SAVE writes alternating EEPROM records with a schema, sequence, CRC, and commit marker. An interrupted write retains the previous valid record; identical saves cause no writes. The first 128 EEPROM bytes are reserved. |
| Local Face Studio | Browser controls preview the actual firmware through simulated GPIO/ADC/SPI, or connect to a physical helmet over USB. Profiles import/export as JSON. It uses Python's standard library and local C++, without a cloud service or package downloads. |
| Reproducible builds and evidence | Dependencies are pinned in toolchain.lock.json. Each build records a source fingerprint, selected library versions, commands, results, and memory metrics. Failed preflight checks replace current success evidence with a failure report. Prior HEX files retain their own manifests. |
| Recoverable history | Git's initial commit preserves the pre-upgrade project. A second independent baseline copy is in build/upgrade-baseline. Original historical audit evidence remains available. |

## Use the preview and configuration tool

Run from the project root:

```sh
python3 scripts/helmet-tool.py
```

Open **http://127.0.0.1:8765**. Desktop simulation enables the three virtual sensors regardless of the real sketch's feature flags. It runs an isolated copy of the actual firmware with the same renderer, configuration protocol, FFT, and control logic. The nominal ADC/SPI model does not establish real electrical timing or visor appearance. Simulated saves last for the preview process; download a profile to retain them.

After uploading firmware 8.0 using Arduino IDE, find the serial device and connect:

```sh
python3 scripts/helmet-tool.py --list-ports
python3 scripts/helmet-tool.py --port /dev/cu.usbmodemYOUR_DEVICE
```

USB mode supports macOS/Linux. Close Arduino Serial Monitor before using it. It opens only the device explicitly supplied with `--port`; it never uploads firmware. Optional sensors must still be enabled in the sketch when their hardware is connected. Disabled sensors perform no reads.

**Apply settings** changes RAM. **Save to helmet** retains the applied settings after power-off. **Load saved** restores the latest valid saved record. **Restore defaults** changes RAM to Settings.h defaults; it does not erase EEPROM. The current expression and sleep state remain session-only; saved `startup_expression` selects the face on the next boot. Saved settings override Settings.h defaults on boot. To retain changed compiled defaults, restore defaults and explicitly save.

Choose a preset or import a profile, apply it, and save if desired. Everyday uses the spectrum mouth and original resting artwork. Expressive uses the amplitude mouth, shorter varied blink intervals, and idle glances. Brightness stays at 1 in both. Quiet calibration needs an enabled, visible, awake microphone with no active boop; remain quiet until the calibration indicator clears, then save. Interrupted calibration is canceled on sleep, expression reset, or settings changes. Idle noise adaptation stays in RAM; an explicit calibration updates the saved-setting candidate.

## USB interface and configuration

One ASCII request per newline; one JSON response per request. CRLF is supported. The maximum complete request is 383 bytes. Overlong, abandoned, malformed, duplicate, or unknown settings are rejected. A partial line times out after two seconds and is discarded through its newline. A stalled response does not stop button, animation, or audio servicing.

| Request | Meaning |
| --- | --- |
| GET | Current configuration and schema version |
| SET brightness=1 sensitivity=65 | Apply one or several settings atomically; reject the whole request if invalid |
| EXPR 0 through EXPR 5 | Select happy, surprised, angry, vwv, sleepy, or wink |
| SLEEP 1 / SLEEP 0 | Sleep / wake; an enabled hidden visor still keeps the face blank |
| CALIBRATE | Begin quiet microphone calibration |
| SAVE / LOAD / DEFAULTS | Explicit save / load / compiled defaults |
| STATUS | Features bitmask (visor=1, boop=2, microphone=4), controls, audio, sensor, and display diagnostics |
| FRAME | Last composed logical frame, width 112, height 8, row-major hexadecimal bitmap; hidden displays return zeros |

Configuration schema 1 uses integers: `brightness` 0–15; `startup_expression` 0–5; `mouth_mode` 0–1; `sensitivity` and `noise_floor` 0–1023; `noise_margin` 1–1023; `boop_trigger` 0–1022; `boop_release` 1–1023; `blink_interval_ms` 1000–60000; `blink_jitter_ms` 0–5000; `transition_ms` 0–1000; `attack_ms` and `release_ms` 1–2000; `natural_blinks` and `idle_motion` 0–1. Trigger must be less than release, and jitter less than the blink interval. Pin assignments, SPI clock, hardware enable flags, debounce/hold timing, and boop lifetime remain compile-time settings. Change `configSchemaVersion` if the persisted layout changes in a future release.

Profiles have `{ "schema": 1, "name": "My helmet", "config": { ... } }`. The named files in profiles/ are examples and are checked against the actual firmware by the integration tests.

## Build and validation

```sh
./scripts/test-firmware.sh
python3 scripts/check-features.py
./scripts/build.sh
```

Arduino CLI 1.5.1, Teensy 1.62.0, arduinoFFT 2.0.4, Adafruit GFX 1.12.6, Adafruit BusIO 1.17.4, and the bundled ADC 9.1 are pinned. Environment overrides for ARDUINO_CLI, CXX, and ARDUINO_FFT_SRC remain supported. The host checks and preview require Clang/C++17, Python 3, and the installed FFT source. No new hardware or downloaded library is required on this Mac.

Build results are versioned under build/runs/. `build/current-build.json` identifies the latest build attempt; `build/feature-results.json` is now an object containing state, source, toolchain, and results. `build/latest-successful-build.json` intentionally retains the last successful build. `build/teensy40/` contains the selected-feature HEX plus its source manifest. A failed build preserves the prior export and records the failure separately; use the manifest to establish whether an image matches the current source. Editing source during a run invalidates that run as current evidence.

The host suite covers the original four complete faces, 3,584 physical mapping cases, driver bounds/allocation checks, transitions and new expressions, blink variation, control debounce and rollover, shutdown ordering, quiet hidden sensors, boop expiry while hidden/asleep, early/late bursts, buffered acquisition during modeled SPI/ADC work, slower-SPI recovery, overruns, counter wrap, DC bias, FFT equivalence and bounds, both mouth styles, calibration persistence, USB validation/backpressure/timeouts, CRC and interrupted saves, failed-build provenance, all eight feature masks, preview/profile integration, and serial framing through a disposable pseudo-terminal. Target checks compile all eight combinations with zero warnings.

### Completed validation

The full host suite passed with AddressSanitizer, UndefinedBehaviorSanitizer, and compiler warnings treated as errors. All eight Teensy feature combinations compiled with zero warnings and verified dependency versions. The default firmware was exported successfully. Face Studio's expression controls, simulated microphone, profiles, and save/default/load sequence were exercised in the browser. Original Icons.h remains byte-for-byte identical to the baseline.

| Target configuration | Flash | RAM1 variables | RAM1 available for local variables |
| --- | ---: | ---: | ---: |
| Default, optional sensors disabled | 70,652 bytes | 13,216 bytes | 445,536 bytes |
| Visor, boop, and microphone enabled | 79,868 bytes | 16,032 bytes | 442,720 bytes |

The final build and feature manifests match source fingerprint `3bdda5c40bd93f7e07275a7155d727cec2c7b8778103fc6a3cb74f934f904de7`. Artifact hashes were checked against the exported files. Build evidence records the baseline Git HEAD and the then-uncommitted upgrade; the source fingerprint identifies the exact tested files independently of the later upgrade commit. The evidence index is build/upgrade-validation.json, with host logs in build/upgrade-host-tests.log and build/upgrade-tool-tests.log, and the browser capture in build/face-studio.jpg.

Before helmet use, upload the default build and inspect every expression, transition, and wake. Enable each connected sensor individually; test voice response during blinks, boop release and hidden expiry, and quiet calibration. Observe STATUS for dropped samples under real load. The reserved QuadTimer4/ADC0 resources must not be shared with future audio/timer libraries. Neither host tests nor successful compilation establishes power draw, thermal behavior, real ADC cadence/interrupt latency, wiring quality, or display appearance.

Hardware references: [ADC timer API](https://pedvide.github.io/ADC/docs/Teensy_4_html/class_a_d_c___module.html), [Teensy 4.0 electrical and EEPROM details](https://www.pjrc.com/store/teensy40.html). Wiring and power instructions remain in the root README.

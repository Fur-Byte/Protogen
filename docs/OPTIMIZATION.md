# Teensy 4.0 optimization — 2026-09-27

Historical optimization record. See [the 2026-09-28 code audit](CODE_AUDIT.md)
for subsequent corrections and current build/test results.

## Final review and quality-of-life additions

The final review adds the following controls without delays, runtime settings
parsing, EEPROM writes, or new heap allocations:

- Tap/release the expression button to advance one face. Hold about 1.2 seconds
  to sleep the display; a tap while sleeping wakes the same selected expression.
  A held press triggers once, and its release does not also change the face.
  Press/release needs 25 ms of stable input, sampled at most once per millisecond.
  This replaces the interrupt/event handoff and 150 ms lockout. Selection changes
  now occur on release so a long hold does not accidentally advance the face.
- Display sleep and a hidden visor use MAX7219 shutdown once on entry, switch
  RGB off, and skip all ADC/audio/drawing work. The button and optional digital
  visor input remain polled. This is display sleep, not a stopped Teensy CPU.
  The selected face is completely composed and sent while the panels remain
  off, then shutdown is released. Blink and glitch delays restart on wake.
- A 20 ms stable visor input prevents rapid off/on transitions from chatter.
  Its actual initial pin state is read during setup.
- Boop hysteresis triggers at 500 or below and releases/rearms at 550 or above.
  Values in between retain the prior state. The six-second reaction timeout
  requires release before retriggering. A boop redraw restores mouth/nose/eyes
  together, and its RGB indicator now also respects the one-second timeout.
- Speech has priority over angry glitches so the two renderers cannot overwrite
  one another's mouth. Angry glitches resume after the normal seven-second gap
  following speech. Visor hiding no longer leaves a colored RGB indicator on.
- `Settings.h` groups brightness, startup expression, sensitivity, thresholds,
  and timing constants. Compile-time checks reject invalid brightness,
  expression, sensor thresholds, sensitivity, and basic control timing values.
  Feature-enable flags remain in the main `.ino`. Defaults keep brightness at 1,
  startup at happy, all optional sensors disabled, and the familiar animations.
- Shared base-face drawing and expression rendering remove duplicated work/code.
  The existing one-byte dirty-register tracker remains; no shadow framebuffer
  was added. Artwork, driver source, physical rotations, and wiring are unchanged
  by this final pass. No archive files were opened or changed.

Measured final builds, compared with the preceding optimization pass:

| Configuration / measurement | Previous pass | Final QoL build | Difference |
| --- | ---: | ---: | ---: |
| Default: total flash | 35,836 B | 34,812 B | 1,024 B smaller |
| Default: RAM1 variables | 8,032 B | 7,040 B | 992 B smaller |
| Microphone only: total flash | 39,932 B | 39,932 B | unchanged |
| Microphone only: RAM1 variables | 9,248 B | 9,280 B | 32 B added |

This small microphone-enabled state cost supports the debounced controls.
The settings are compile-time constants; no configuration menu runs in the loop.
Totals include compiler/linker alignment. No on-board latency or power savings
have been measured. The default HEX is regenerated, but has not been uploaded.

Host tests now model independent button/visor pins and the MAX7219 shutdown
register, including the retained display bytes and the frame present at wake.
They test the controls, sleep/hidden idle behavior, sensor threshold stability,
boop timeout/rearm, speech/glitch ownership, and timer rollover in addition to
existing mapping/FFT checks. The eight real Teensy feature builds and host tests
are recorded in `build/optimization-feature-*` and
`build/optimization-host-tests.log`. Run the commands in the root README to
repeat them. Hardware timing and display appearance still require helmet tests.

Changed in this final pass: `PROT_OS7_MAX7219.ino`, `Blink.ino`, `Glitch.ino`,
`Microphone.ino`, new `Control.ino` and `Settings.h`, host tests/stubs, README,
these notes, and recovery notes. Sleep and expression selection are session-only;
power cycling uses `StartupExpression`. An enabled hidden visor keeps the display
off even after a button wake.

## First optimization pass (historical record)

The active sketch is `firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino`.
Its supporting `.ino` tabs and bundled MAX7219 driver are part of the same build.
The archive directory was not opened, extracted, or modified during this work.
No firmware was uploaded.

## What changed and why

1. **Send display data only when it changes.** The bundled driver tracks the
   eight MAX7219 digit registers with a one-byte dirty mask. Changing a pixel
   marks its physical register after tile rotation/mapping. Drawing a pixel that
   already has the requested value does nothing; clearing an already blank
   screen does nothing. `write()` sends just the marked registers. This reduces
   time the Teensy spends waiting for SPI and leaves more time for inputs.
   A changed-then-restored register can still be sent once; no extra shadow
   framebuffer is allocated.

2. **Compose animations before transmitting.** Removed intermediate writes from
   blink, glitch, and microphone code, and removed the duplicate ordinary-loop
   write. One final flush sends the composed face. Removed microphone calls
   inside blink frames; the main loop services it once. A hidden visor clears
   the screen only on entry. Previously, an unchanged visible face sent two
   complete frames per loop: 448 bytes across 14 panels. It now sends zero.
   A single changed register sends 28 bytes; a full frame is 224 bytes.
   MAX7219 chips retain and scan their display memory without continuous refresh
   from the Teensy.

3. **Remove the microphone's 16 ms sampling wait.** Each normal loop accepts at
   most one sample when the next 250 microsecond deadline is due. A complete
   64-sample frame is then processed if the smoothed amplitude exceeds the
   threshold. Buttons, visor detection, and animations can run between samples.
   Deadlines use rollover-safe arithmetic. If a whole period is missed, the
   partial frame is discarded and sampling restarts; no back-to-back reads are
   passed off as evenly spaced samples. Boop/hidden states pause collection and
   discard partial frames before resuming.

4. **Use smaller FFT values and cache the Hamming window.** Replaced two
   64-element `double` buffers (1,024 bytes) with `float` buffers (512 bytes).
   The symmetric window needs 32 floats (128 bytes), calculated once at setup
   with `cosf`, for a net 384-byte reduction in these arrays. Applying those
   coefficients directly avoids per-frame cosines and linking the library's
   generic multi-window selector. Teensy 4.0 supports both float and double in
   hardware; this does not fix a missing FPU. Its float arithmetic is faster,
   and 32-bit precision is ample for 10-bit ADC input displayed as eight-row
   bars. The existing 64-sample size, 4 kHz target rate, amplitude scaling,
   low-bin substitution, and left/right mouth mapping remain intact.

5. **Make microphone code truly optional.** FFT includes, buffers, object,
   smoothing state, and processing are compiled only with `Microphone` enabled.
   A disabled feature no longer leaves an FFT constructor/data in the build.
   The unnecessary runtime `micState` flag was removed.

6. **Reduce repeated sensor and PWM work.** Boop readings are limited to every
   5 ms (200 Hz), with at most roughly 5 ms added polling delay. This avoids
   unnecessary ADC work in the much faster idle loop. RGB PWM writes are skipped
   when all three values already match. Microphone detection shares the FFT
   samples instead of performing separate, loop-speed-dependent ADC reads.
   Its DC estimate starts from an actual microphone reading. Explicit 10-bit
   ADC resolution preserves the existing 0–1023 thresholds.

7. **Initialize the display after Teensy startup.** Added `matrix.begin()` in
   `setup()`. The global matrix constructor now only prepares storage and mapping.
   Initialization holds the panels in shutdown, sets their mode and low
   brightness, loads the blank framebuffer, then enables them. SPI commands use
   explicit 4 MHz, MSB-first, mode-0 transactions rather than inheriting bus
   settings. The speed is below the MAX7219's specified 10 MHz limit; it is not
   an attempt to overclock the display chain. Newly allocated bitmap storage is
   zeroed before changed-byte comparisons.

   Superseded: the clock is no longer hardcoded. `begin()` now takes a frequency
   argument and `Settings::SpiFrequencyHz` selects 8 MHz. The 4 MHz figure above
   describes this pass only; see the code audit for the current hardware
   guidance.

8. **Clean up animation transitions.** A button expression change resets a
   partial blink/glitch and restarts their delays. A completed blink clears its
   active flag. Boop and visor transitions reset partial blink state. Entering
   a boop redraws the base mouth/nose as well as the eyes, so reopening the visor
   during a boop restores the whole face. Previously that path could restore
   only the eyes. Fixed icon coordinates are now `const int16_t`; their values
   and every tile rotation are unchanged. Removed an always-false unsigned
   coordinate comparison in the driver. Edited source tabs use consistent LF
   line endings.

## Measured build results

Arduino CLI, Teensy platform 1.62.0, Teensy 4.0 target, 600 MHz, Faster, Serial USB;
arduinoFFT 2.0.4, Adafruit GFX 1.12.4, Adafruit BusIO 1.17.4.
Baseline measurements came from a temporary copy taken before edits.

| Configuration / measurement | Before | After | Saved |
| --- | ---: | ---: | ---: |
| Default: total flash (code + data + headers) | 41,980 B | 35,836 B | 6,144 B |
| Default: RAM1 variables | 9,632 B | 8,032 B | 1,600 B |
| Microphone only: total flash | 41,980 B | 39,932 B | 2,048 B |
| Microphone only: RAM1 variables | 9,632 B | 9,248 B | 384 B |

RAM2 variables remain 12,416 bytes in these builds. Flash includes linker/image
padding, so compare the totals rather than assuming each source edit saves a
particular number of bytes. These are build measurements, not measured frame
rates, CPU utilization, battery life, or temperature improvements.

## Validation and changed files

- Clean Teensy 4.0 builds for all eight combinations of VisorSensor, BoopSensor,
  and Microphone: zero warnings and zero errors.
- Host simulation with AddressSanitizer and UndefinedBehaviorSanitizer passes:
  3,584 pixel/rotation cases across all 14 tiles; SPI byte counts/order; no idle
  display traffic; no repeated unchanged PWM writes; blink completion; button
  debounce/animation reset; complete glitch sequence; visor blanking and whole
  boop-face restoration; microphone deadlines, missed-period restart, pause,
  microsecond rollover, loud input, and silence recovery.
- A synthetic ADC signal's float/cached-window spectrum agrees with the original
  double/Hamming calculation within 0.02 magnitude units for the rendered bins.
  This test is not a proof that every input rounds to exactly the same LED height.
- Every bitmap initializer in `Icons.h` is unchanged from the archived original
  sketch. The file also had two comment lines reworded and the unused
  `buffer0L`/`buffer0R` transition buffers removed, so it is not byte-for-byte
  identical. Current SHA-256:
  `b977dbde17338799f8cdf658e13e08c91e20d7a67e68f255ac0c24f60e5bb645`.
  `WriteExpression.ino`, wiring, feature defaults, and artwork remain unchanged.
- Firmware edits: the main `.ino`, `Blink.ino`, `Glitch.ino`, `Microphone.ino`,
  `FFT.ino`, and `src/Max72xxPanel/Max72xxPanel.{h,cpp}`.
- Added repeatable checks: `scripts/test-firmware.sh`, `scripts/tests/` (simulated
  Arduino/GFX/SPI interfaces plus behavior tests), and `scripts/check-features.py`.
  Updated README and linked this report from the earlier recovery review.
- Build logs: `build/compile.log`, `build/optimization-baseline-default.log`,
  `build/optimization-baseline-microphone.log`,
  `build/optimization-feature-checks.log`, and
  `build/optimization-feature-results.json`.
- The exported default-feature HEX is refreshed in `build/teensy40/`.
  Sensor-combination checks use disposable copies and do not enable sensors
  in the active sketch or overwrite its default HEX.

## Physical verification still needed

The host simulation uses simplified Arduino/GFX/SPI interfaces. Target builds
use the real installed libraries. Neither establishes real wiring, display
appearance, ADC timing, or electrical behavior on the helmet.

Sampling is cooperative, not timer/DMA driven: ADC conversion, FFT computation,
and SPI transfers still take execution time. A full display transfer can cause
sampling to restart, and interrupts can introduce jitter. The important change
is eliminating the deliberate 16 ms busy-wait, not guaranteeing hard real-time
4 kHz acquisition. Test mouth response while blinking and switching expressions.
The new fixed-rate microphone smoothing may require retuning `sensitivity` for
your microphone gain and helmet acoustics.

Upload the default sketch first, then enable connected sensors individually.
Check all expressions, blink, angry glitch, boop release/timeout, and visor
restoration during a boop. No power-saving or clock changes were made; lower
SPI traffic alone does not establish lower helmet power consumption.

Hardware references: [PJRC Teensy 4.0 processor/ADC documentation](https://www.pjrc.com/store/teensy40.html)
and [Analog Devices MAX7219 specifications](https://www.analog.com/en/products/MAX7219.html).

# Code audit — Teensy 4.0 — 2026-09-28, verified 2026-10-03

> Historical notes for the pre-8.0 firmware. See [UPGRADE.md](UPGRADE.md) and
> the root README for current behavior, settings, build evidence, and checks.

The active firmware, bundled MAX7219 driver, build scripts, existing tests, and
operating instructions were reviewed. Fixes are applied in place. The target is
**Teensy 4.0 at 600 MHz, Serial USB, Faster optimization**, with the original
14-panel layout. All optional sensors remain disabled in the supplied sketch.

The workspace has no Git repository; the 2026-09-28 review is recorded as a
patch in [`build/audit-changes.patch`](../build/audit-changes.patch). A second
pass on 2026-10-03 audited the evidence trail rather than the sketch logic, and
found that later edits had invalidated parts of that record. Those results are
in [Evidence-integrity pass](#evidence-integrity-pass-2026-10-03).

`archive/PROT_OS7_original.zip` was read for comparison only, to confirm that
the `Icons.h` bitmap data still matches the original; nothing was copied out of
it into the project. Head-model ZIPs and the historical Nano schematic remain
outside the active firmware audit.

## Findings and corrections

| Finding | Trigger and effect | Correction |
| --- | --- | --- |
| Driver memory corruption | Invalid tile identifiers write outside allocated mapping arrays. At 32 panels, an 8-bit framebuffer size wraps to zero and drawing accesses outside storage. The supplied 14-panel layout does not trigger these size faults. | Validate dimensions and mappings, retain 16-bit offsets, allocate one checked block, release owned storage on destruction, and prohibit shallow copies. Invalid layouts/allocation failures cause safe no-ops and `begin()` returns false. |
| Off-screen pixels wrap into the face | `drawPixel(0, 256, 1)` or negative multiples of 256 narrowed the Y coordinate to an 8-bit value before clipping. Global rotation also truncated coordinates on wider/taller canvases. | Clip signed logical coordinates first and use full-width coordinates through global rotation. Tile mapping and rotation retain the original orientation. |
| False speech after a pause | With an old DC estimate near 510, a silent microphone resuming at a constant 900 triggered the speech animation after one frame. Pausing retained stale smoothing history. | Clear the envelope history when paused and initialize the DC estimate from the first resumed sample. |
| Audio catch-up bursts | A loop delayed by slightly less than one sampling period could take a sample and then take another almost immediately at the old deadline. | Schedule the next read from its actual start time, enforcing at least 250 microseconds between reads. Longer stalls still discard partial frames. |
| Microphone bias contaminates FFT bins | Windowing the raw DC-biased ADC frame spreads bias into bins beyond the two replaced display bins. | Subtract the frame mean before the Hamming window. Preserve the existing bin substitutions, amplitude scale, and mouth mapping. |
| Floating enabled visor input | `INPUT` has no pull-up on Teensy 4.x, so open-drain sensors or an unconnected enabled visor input could float. | Use the internal 3.3 V pull-up. An open/disconnected input reads HIGH and blanks the face; a LOW input shows it. |
| Invalid settings escape validation | Brightness/startup values such as 256 narrowed to zero before their assertions. Several timing settings accepted zero or negative values converted to unsigned. | Validate brightness/expression before narrowing and require every millisecond interval to be within 1–2147483647. |
| False feature-build coverage | The checker only enabled requested flags; it never disabled flags already enabled in the sketch. With Python optimization, its final `assert` also disappeared. | Set every flag explicitly in temporary copies, require exactly one declaration per flag, and return failure explicitly for compiler errors or warnings. CLI overrides now support executable paths and command names and cannot silently fall back when invalid. |

The first four baseline probes confirmed both heap overflows under AddressSanitizer,
the wrapped pixel, and the false speech trigger before editing. Their output is in
[`audit-baseline-regressions.log`](../build/audit-baseline-regressions.log).
The timing simulation subsequently exposed the catch-up burst; its regression now
asserts the minimum spacing between every recorded microphone read.

Additional target protections: the sketch rejects a board selection other than
Teensy 4.0, build scripts specify the CPU/USB/optimization options explicitly, and
SPI is initialized only after configuring chip select as an output and driving it
HIGH. The test GPIO model follows Teensy's behavior: writing an input pin changes
its pull resistor, not its output latch.

## Validation

Run from the project root:

```sh
./scripts/test-firmware.sh
python3 scripts/check-features.py
./scripts/build.sh
```

The host suite passes with AddressSanitizer, UndefinedBehaviorSanitizer,
`-Wall -Wextra -Werror`, and nonrecovering sanitizer failures. It covers:

- The original 3,584 pixel/tile-rotation cases and idle SPI/PWM behavior.
- All canvas and tile rotations on multirow layouts, reversed chain mappings,
  off-screen/extreme coordinates, 32/255-tile addressing, invalid dimensions,
  invalid mappings, allocation failure, and initialization ordering.
- Debounced button press/release/hold, sleep/wake ordering, visor debounce,
  boop hysteresis/timeout/rearming, blink completion, and speech/glitch priority.
- Millisecond and microsecond rollover, missed sample recovery, subperiod
  catch-up prevention, changed microphone DC bias after pausing, and silence recovery.
- FFT agreement with an independently DC-corrected double-precision reference,
  zero spectrum for constant input, and bounded/mirrored bar buffers.
- Full-loop audio and blinking with modeled 10-microsecond ADC reads and
  2-microsecond SPI byte timing. That byte cost models a 4 MHz SPI clock, while
  `Settings::SpiFrequencyHz` now selects 8 MHz, so the simulated SPI load is
  lighter than the target's. This is simulation, not a hardware timing
  measurement.
- Eight separately compiled/executed sensor combinations, checking that disabled
  inputs have zero reads and enabled inputs respond.
- Seven Python test groups, including all 64 initial/requested feature-flag
  combinations, compiler error/warning propagation under `python -O`, CLI lookup,
  29 rejected settings configurations, and wrong-board rejection.

All eight real Teensy 4.0 sensor configurations compile with **zero errors and
zero warnings**. The exported HEX is regenerated from the supplied default flags.

| Enabled sensors | Compile | Warnings |
| --- | --- | ---: |
| None | Pass | 0 |
| Microphone | Pass | 0 |
| Boop | Pass | 0 |
| Boop + Microphone | Pass | 0 |
| Visor | Pass | 0 |
| Visor + Microphone | Pass | 0 |
| Visor + Boop | Pass | 0 |
| Visor + Boop + Microphone | Pass | 0 |

Verified environment: Arduino CLI 1.5.1, Teensy platform 1.62.0,
arduinoFFT 2.0.4, Adafruit GFX 1.12.6, and Adafruit BusIO 1.17.4.

| Memory measurement | Default sensors off | All sensors enabled |
| --- | ---: | ---: |
| Flash (code + data + headers) | 34,812 B | 39,932 B |
| RAM1 variables | 7,456 B | 9,280 B |
| RAM1 available for local variables after code/padding | 484,064 B | 482,240 B |
| RAM2 static variables | 12,416 B | 12,416 B |

The driver additionally allocates 140 bytes for the 14-panel buffer/mapping at
startup, plus allocator overhead. These are compiler measurements, not runtime
stack high-water measurements. Default flash usage is unchanged from the audit
baseline; default RAM1 variables increased by 416 bytes.

Evidence:

- [`audit-host-tests.log`](../build/audit-host-tests.log)
- [`feature-results.json`](../build/feature-results.json) and [`feature-checks.log`](../build/feature-checks.log)
- [`compile.log`](../build/compile.log) and [`audit-toolchain.txt`](../build/audit-toolchain.txt)
- Upload image: [`PROT_OS7_MAX7219.ino.hex`](../build/teensy40/PROT_OS7_MAX7219.ino.hex)

The face artwork, expressions, pin assignments, physical tile rotations, low
brightness, startup selection, and optional-feature defaults are preserved.

Every bitmap initializer in `Icons.h` is unchanged from
`archive/PROT_OS7_original.zip`; a direct diff shows only two reworded comment
lines and the removal of the unused `buffer0L`/`buffer0R` all-zero transition
buffers. The file itself is therefore not byte-for-byte identical to the
original, so record its actual digest rather than an inherited one. Current
SHA-256, also written to [`icons-sha256.txt`](../build/icons-sha256.txt):
`b977dbde17338799f8cdf658e13e08c91e20d7a67e68f255ac0c24f60e5bb645`.

## Evidence-integrity pass (2026-10-03)

This pass audited whether the recorded evidence still described the code. It did
not. Five sources of drift were found; all are resolved except the last, which
is a hardware question.

| # | Drift | Evidence | Resolution |
| --- | --- | --- | --- |
| 1 | Exported firmware was stale | `build/teensy40/*` was built 2026-10-01 19:08:56; nine firmware sources and two test stubs had later mtimes. The old `.sym` still contained `_GLOBAL__sub_I_buffer0L`, and the old `.lst` showed `matrix.begin();` | Rebuilt with `./scripts/build.sh`. The new `.sym` has no `buffer0*` symbols, the `.lst` shows `matrix.begin(Settings::SpiFrequencyHz)`, and no source is newer than the image |
| 2 | `Icons.h` digest was wrong | Claimed `fed059af…` in this file, in `FINAL_CHECK.md`, in `OPTIMIZATION.md`, and in `build/icons-sha256.txt`; actual `b977dbde…` | Actual digest written to `icons-sha256.txt` and to all three documents. See the note below on what did and did not change |
| 3 | Sensor matrix was incomplete | `feature-results.json` and `feature-checks.log` held 7 of 8 entries; `VisorSensor + BoopSensor + Microphone` was missing because the run was interrupted | `check-features.py` re-run to completion: 8 of 8, exit 0, zero warnings |
| 4 | SPI rate doubled without documentation | The audit patch has `SPISettings(4000000, …)` and `void begin();`; current source has `Settings::SpiFrequencyHz = 8000000UL` and `bool begin(uint32_t)`. Both audit documents still said "the 4 MHz clock" | Corrected in this file and in `OPTIMIZATION.md`, which now marks 4 MHz as superseded and warns about the reduced margin |
| 5 | Patch does not cover later edits | `build/audit-changes.patch` is dated 2026-09-28; the SPI rework and several other edits came after it | Recorded below. The patch cannot be regenerated because its baseline is the 2026-09-27 state, which no longer exists on disk |

### What actually changed in `Icons.h`

The bitmap data is intact. Diffing the current file against
`archive/PROT_OS7_original.zip` shows only three differences, none of them
artwork:

- two comment lines were reworded (one of them fixed a trailing-backslash line
  splice that had hidden following declarations);
- the unused `buffer0L` and `buffer0R` arrays were deleted. Both were 16 bytes
  of `0x00` and nothing referenced them.

So the earlier "byte-for-byte unchanged" wording was wrong about the file while
being right about the artwork. Every face, mouth, and glitch initializer still
matches the original exactly.

### Changes made after the 2026-09-28 audit

Confirmed by checking which added lines from `audit-changes.patch` no longer
appear in the source. Every other audit edit is still present.

- **SPI clock.** The driver gained a configurable clock: `begin()` now takes a
  frequency, `spiClock` is a member, and `Settings::SpiFrequencyHz` selects
  8 MHz. This is the only change to any file the patch already covered.
- **`PROT_OS7_MAX7219.ino`.** Calls `matrix.begin(Settings::SpiFrequencyHz)`
  instead of `begin()`, and adds `static_assert(samples == 64, …)`.
- **`Settings.h`.** Adds `SpiFrequencyHz` and its range check.
- **`Blink.ino`, `Glitch.ino`, `WriteExpression.ino`.** The animation functions
  take an explicit `now`; the previous no-argument forms were kept as `inline`
  aliases. `writeExpres()` is still compiled and is still unreferenced.
- **`Icons.h`.** As described above.
- **Test harness.** `scripts/tests/sketch.h` gained declarations for the
  `now`-taking overloads, `drawMouth`, and `drawExpression`. `sketch.h` and
  `stubs/SPI.h` both have post-audit mtimes; the `sketch.h` delta is accounted
  for, and no content difference for `stubs/SPI.h` could be confirmed or
  excluded, so it is listed as unverified rather than clean.

### Refreshed validation, 2026-10-03

All three checks re-run from the project root. The host suite passes under
AddressSanitizer, UndefinedBehaviorSanitizer, `-Wall -Wextra -Werror`, and
nonrecovering sanitizer failures: 3,584 pixel/rotation cases, the driver layout
matrix, all 8 sensor masks, and 7 Python test groups. All 8 real Teensy 4.0
sensor configurations compile with zero errors and zero warnings. The default
build is clean.

| Memory measurement | 2026-09-28 | 2026-10-03 |
| --- | ---: | ---: |
| Flash (code + data + headers) | 34,812 B | 34,812 B |
| RAM1 variables | 7,456 B | 7,456 B |
| RAM1 available for local variables | 484,064 B | 484,064 B |
| RAM2 static variables | 12,416 B | 12,416 B |

Identical, so the post-audit edits were behaviour-neutral with respect to size
and the earlier memory figures need no correction. The removed `buffer0L` and
`buffer0R` arrays were all-zero and unreferenced, and the SPI change swaps a
literal for a member read.

### Open item: the 8 MHz SPI rate needs a hardware check

This is the one change in the drift that has a real Teensy 4.0 consequence. The
MAX7219 is rated to 10 MHz, but 8 MHz over a 14-module daisy chain with long
wiring leaves much less margin than 4 MHz, and the DIN/CLK setup and hold
windows are the part most likely to fail first. The host simulation still models
2 microseconds per SPI byte, which is a 4 MHz clock, so the test suite does not
exercise the configured rate. Decide between these before relying on the image:

1. measure the assembled chain and keep 8 MHz if DIN and CLK are clean, or
2. set `Settings::SpiFrequencyHz` back to 4000000UL and rebuild, or
3. raise the modelled byte cost in `scripts/tests/firmware.cpp` to 1 microsecond
   so the simulation matches the shipped configuration, then re-run.

Option 3 alone is a test-fidelity fix and does not validate the hardware.

## Teensy hardware requirements and remaining checks

For MAX7219 chips powered at 5 V, the specified minimum logic HIGH is 3.5 V.
Use 3.3 V-to-5 V buffering on DIN, CLK, and LOAD unless the modules already
provide it. `Settings::SpiFrequencyHz` is 8,000,000, below the chip's 10 MHz
maximum but with far less timing margin than the 4 MHz rate this sketch
originally used. Confirm DIN and CLK timing at the assembled chain length and
drop the setting back to 4 MHz if the display shows artifacts. Leave the last
DOUT disconnected from Teensy. These requirements come from the
[MAX7219/MAX7221 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max7219-max7221.pdf).

Teensy 4.0 inputs accept at most 3.3 V, including analog inputs. A sensor module
with a 5 V output pull-up needs adaptation before connection. PJRC recommends
at most 4 mA per directly driven GPIO; size RGB resistors for peak current, not
average PWM brightness. Provide suitable regulated display power, common ground,
and correct VIN/VUSB isolation when combining external power and USB. See
[PJRC's Teensy 4.0 electrical and power specifications](https://www.pjrc.com/store/teensy40.html).

No firmware was uploaded and no physical helmet was tested. Verify power and
signal levels, then upload the default image and exercise all expressions,
button controls, blink/glitch, and every panel. Enable connected sensors one at
a time and check their polarity, thresholds, visor recovery, and microphone gain.

Audio remains cooperative and targets 4 kHz; execution time and interrupts cause
jitter and a somewhat lower effective sample rate. This is a visual speech effect,
not a calibrated spectrum analyzer. Actual sample timing, electrical noise, panel
appearance, supply capacity, and temperature require measurements on the helmet.
Sleep blanks the displays and pauses analog work; it does not stop the Teensy CPU.

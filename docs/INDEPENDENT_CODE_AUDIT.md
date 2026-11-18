# Independent code audit — Protogen

Date: **2026-10-03, America/Phoenix**. Target: **Teensy 4.0, 600 MHz, Serial USB, Faster optimization, 14 MAX7219 modules**.

This is a report-only review. The firmware, scripts, original archive, earlier reports, and pre-existing build artifacts were preserved. Fresh builds and diagnostic code are in an isolated copy under [the audit evidence directory](../build/independent-audit/20261003-214127/snapshot-info.json). No firmware was uploaded.

## Verdict

**The current project is materially better than the archived original. Most of the changes should be kept.** The strongest gains are concrete: corrected driver memory hazards, corrected FFT bit indexing, less redundant display traffic, appropriate button input configuration, and control servicing between microphone samples. These are supported by source comparison, reproduced original defects, and current tests.

**Two optional-feature behavior defects remain:** short sounds can be missed by the microphone's frame-end detection gate, and hiding/revealing the face can restart a boop reaction without a sensor release. Neither path runs in the supplied sensor-disabled configuration. The **8 MHz display clock remains a hardware validation gap affecting every configuration**.

The existing tests are useful and reproducible. Their passing status does not cover the two new observations, actual FFT execution time, signal integrity, or appearance on the helmet. The existing exported default HEX is current: a clean isolated rebuild produced an **identical file**.

There is no reliable percentage by which the project became “better.” There is no Git history, the September 27 benchmark source is unavailable, and the old archive is a different historical source state. The available evidence supports specific improvements and tradeoffs; it does not identify which agent made every edit or establish measured battery life, heat, CPU utilization, or hardware response time.

## Findings, ranked by priority

P2 means a normal-priority behavior fix or hardware validation item. P3 means a lower-priority tooling or documentation issue. **No P0 or P1 software defect was identified in the reviewed active code.** Hardware has not been tested.

| ID | Priority | Finding | Status | Affected configuration |
| --- | --- | --- | --- | --- |
| F1 | P2 | A short sound can exceed sensitivity yet never activate the mouth | Reproduced; high confidence | Microphone enabled |
| F2 | P2 | Hiding/revealing restarts an active boop's six-second duration | Reproduced; high confidence | Boop plus visor; shared sleep lifecycle |
| F3 | P2 | The 8 MHz default lacks assembled-chain validation; tests do not bind timing to the selected clock | Validation gap; physical failure unverified | All configurations |
| F4 | P3 | The earlier “verified” audit contains incorrect current-state claims | Verified documentation discrepancies | People relying on those reports |
| F5 | P3 | Stored success evidence can survive a failed later invocation without identifying its source | Reproduced tooling behavior; high confidence | Build and audit workflow |

### F1 — Microphone detection only considers the envelope at the end of a frame

Source: [Microphone.ino:37](../firmware/PROT_OS7_MAX7219/Microphone.ino#L37) and [frame-end gate:41](../firmware/PROT_OS7_MAX7219/Microphone.ino#L41).

The firmware updates its ten-sample amplitude average on every sample, but only tests that average for speech when the 64-sample FFT frame finishes. At nominal 4 kHz, a frame spans about 16 ms and the ten-sample envelope spans about 2.5 ms. Sound earlier in the frame can disappear from the envelope before the test.

An isolated probe supplied eight quiet samples, 24 samples alternating between ADC values 310 and 710, then 32 quiet samples at 510. The six-millisecond sound burst reached an envelope of **198**, above the configured threshold of **65**. At frame completion the envelope was **0**, and `speakingActive` remained false. Moving the same 24-sample burst to the end of the frame activated speech.

Impact: short valid stimuli can produce no mouth response, depending on their alignment with an FFT frame. This is a detection behavior change from the original microphone code, which checked the envelope on each invocation. The probe does not quantify how often normal speech will exhibit this on the helmet.

Recommendation: latch threshold activity during collection and consume that flag when processing the completed frame. Clear the latch on pause, discarded frames, and completion. Preserve the DC-bias reset and cooperative sampling. Promote the early/late burst pair into the permanent regression suite when fixing this.

Evidence: [early burst](../build/independent-audit/20261003-214127/probes/early-burst.log), [late burst control case](../build/independent-audit/20261003-214127/probes/late-burst.log), [diagnostic source](../build/independent-audit/20261003-214127/probes/behavior.cpp). Existing tests use sustained loud input or already active speech; they do not test this alignment difference.

### F2 — Display restoration starts a new boop timer for a continuous obstruction

Source: [blanking clears boopActive:202](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino#L202), [waking clears it again:211](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino#L211), and [reaction entry resets boopTime:224](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino#L224).

Blanking clears the rendered reaction's `boopActive` flag while preserving the sensor state and its armed flag. On restoration, a still-obstructed sensor enters the reaction again and overwrites `boopTime`. Time spent hidden never reaches the timeout check. Repeated restoration before timeout can extend the reaction repeatedly.

The probe armed the sensor with a released reading, triggered a boop, left it active for 5.5 seconds, hid the visor, waited 700 ms, and reopened it without releasing the sensor. At **6,249 ms after the original reaction began**, the boop was active and armed, with a new start time.

Impact: the documented six-second duration and release-before-retrigger rule are bypassed by display restoration. Sleep uses the same blanking/restoration path. Ordinary visible timeout and rearming work correctly.

Recommendation: keep the reaction lifetime separate from whether its frame is currently displayed. Preserve the original start time across hiding, check expiry before restoring the reaction, and require a released reading after expiry. Retain the existing complete-frame redraw on wake.

Evidence: [hidden-timeout reproduction](../build/independent-audit/20261003-214127/probes/boop-hidden-timeout.log). The existing suite tests brief visor restoration during a boop and ordinary timeout separately, leaving this combination uncovered.

### F3 — The faster default is electrically unverified, and the nominal test cost is independent of configuration

Source: [Settings.h:26](../firmware/PROT_OS7_MAX7219/Settings.h#L26), [matrix.begin call:149](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino#L149), [existing timing test:254](../scripts/tests/firmware.cpp#L254), and [SPI settings stub](../scripts/tests/stubs/SPI.h).

The shipped setting requests 8 MHz. The host suite assigns two microseconds per SPI byte, corresponding to a nominal 4 MHz clock. Its SPI settings stub checks a permissible range and mode, but does not retain the requested clock or derive transfer duration from it.

The chip specifies a 10 MHz maximum clock, 25 ns DIN setup time, and 50 ns minimum high/low clock pulses. Eight MHz is within the nominal clock limit; wiring, clock skew, buffers, and module loading still determine margin on the assembled helmet. Those physical conditions are unknown. [Analog Devices MAX7219/MAX7221 datasheet, timing table](https://www.analog.com/media/en/technical-documentation/data-sheets/max7219-max7221.pdf#page=3).

This audit separately exercised one- and two-microsecond byte costs with a ten-microsecond ADC cost. Both supported speech, a complete blink, button selection, and at least 250 microseconds between reads. This extends software evidence for the nominal faster setting. The model omits actual FFT/graphics execution time, interrupts, SPI software overhead, and physical waveforms.

Recommendation: use **4 MHz as the conservative helmet setting until the assembled chain has been measured**, or retain 8 MHz after a documented hardware check. Make the primary test derive nominal byte duration from the selected clock and retain a separate slower-transfer stress case. Changing a simulated duration cannot validate the wiring.

At nominal continuous-clock rates, one changed register takes 28 µs at 8 MHz versus 56 µs at 4 MHz; all eight take 224 µs versus 448 µs. These are wire-time calculations. Idle display traffic is already zero, and no measured user benefit from doubling the clock is available.

Evidence: [nominal 8 MHz probe](../build/independent-audit/20261003-214127/probes/timing-8mhz.log), [nominal 4 MHz probe](../build/independent-audit/20261003-214127/probes/timing-4mhz.log). No physical clock measurement or display failure was observed.

### F4 — Some “verified” report statements still disagree with the current project

Sources: [CODE_AUDIT.md:69](../docs/CODE_AUDIT.md#L69), [memory table:98](../docs/CODE_AUDIT.md#L98), and [post-audit inventory:166](../docs/CODE_AUDIT.md#L166).

Three discrepancies are directly checkable:

- The report says two-microsecond byte timing makes the modeled SPI load lighter than the 8 MHz target. Two microseconds is the larger nominal byte cost. Overall target workload is unmeasured because other costs are omitted.
- The all-sensors RAM1 figure is **9,312 B**, with **482,208 B** available for local variables. The earlier table reports **9,280 B / 482,240 B**, which matches microphone-only builds.
- The report says no-argument animation aliases and an unreferenced `writeExpres()` still exist. The current source contains only the explicit-time animation functions and `drawExpression()`.

The same report's instruction to “raise” byte duration from two to one microsecond also reverses the direction of that change. These discrepancies reduce confidence in inherited inventories and measurements.

Recommendation: update the actively maintained overview from fresh source-bound measurements and clearly label older documents as historical. Keep this independent report and its evidence intact. Avoid interpreting equal total flash size as proof of equal behavior.

Evidence: [fresh build metrics](../build/independent-audit/20261003-214127/build-metrics.json) and the source snapshot. Historical statements explicitly restricted to an earlier pass were not treated as current defects.

### F5 — A failed new check can leave an older success record at the normal path

Source: [feature preflight:53](../scripts/check-features.py#L53), [result publication:73](../scripts/check-features.py#L73), and [build export:15](../scripts/build.sh#L15).

The feature checker resolves its CLI and validates source declarations before refreshing its report. A duplicate feature declaration makes the new run fail while an older eight-success JSON report remains unchanged. A failed default build likewise preserves an older HEX. The scripts return failure correctly; a reader inspecting only the retained artifacts cannot determine which source or invocation they describe.

A separate warning probe confirmed that `build.sh` returns success for a zero-exit compiler emitting a warning, while `check-features.py` returns failure. The latter enforces its documented zero-warning policy.

Impact: especially in this unversioned workspace, old success files can be reused as evidence for later edits. **The actual existing default HEX was verified fresh in this audit**, so this finding concerns workflow reliability rather than a currently stale image.

Recommendation: keep outputs per run and record source fingerprint, feature flags, toolchain versions, command exit status, and completion state. Publish a “current successful build” reference only after successful validation. Preserve previous outputs with their identity. Apply the zero-warning policy consistently if it is intended to gate exported firmware.

Evidence: [tool probe results](../build/independent-audit/20261003-214127/tool-probe-results.json), [preflight failure log](../build/independent-audit/20261003-214127/probes/feature-preflight-failure.log), [failed-build log](../build/independent-audit/20261003-214127/probes/build-failure.log), and [probe implementation](../build/independent-audit/20261003-214127/probes/tools.py). Mock compiler cases ran only in disposable projects.

## Which changes are worth keeping?

| Change | Assessment | Evidence and practical limit |
| --- | --- | --- |
| Checked driver bounds and owned allocation | Demonstrated improvement | Archived invalid tile writes and 32-tile drawing reproduce heap overflows; current sanitized tests reject those paths. The larger-layout faults are outside the supplied 14-tile layout. |
| Signed coordinate clipping | Demonstrated improvement | The unmodified archived driver lights an on-screen pixel for `drawPixel(0,256,1)`; current tests reject off-canvas and extreme coordinates. |
| Non-overlapping FFT bin groups | Demonstrated improvement | The archived renderer reaches a negative bit shift at bin zero. Current groups keep shifts within 0–7 and destination indices within 0–15. |
| Dirty register tracking and one final flush | Demonstrated improvement | Original ordinary visible loops send two 224-byte frames; current unchanged loops send zero bytes. One changed register sends 28 bytes. This measures avoided work, not battery savings. |
| Feature guards | Demonstrated improvement | Current disabled sensors have zero reads; the original evaluated disabled inputs and retained microphone-related state. The current default ELF has no FFT object/buffer symbols. |
| Button pull-up and stable debounce | Demonstrated improvement in configuration and simulated behavior | Original setup selected OUTPUT and drove the button pin HIGH; current setup uses INPUT_PULLUP. Chatter, release, hold, wake, and startup-held cases pass. |
| Cooperative microphone acquisition | Useful improvement with a remaining regression | Removes the deliberate 64 × 250 µs sampling wait from ordinary servicing. Cadence/recovery tests pass; F1 should be fixed. Actual conversion and processing time need measurements. |
| Float FFT buffers and cached window | Demonstrated storage reduction; plausible speed benefit | Two 64-element double arrays use 1,024 B; two float arrays plus 32 cached coefficients use 640 B, saving 384 B in these arrays. The numerical reference test passes. Total CPU speedup was not measured. |
| DC removal and pause reset | Demonstrated signal-processing improvement in tests | Constant inputs at several biases produce zero non-DC magnitudes; changing bias after a pause does not falsely activate speech. |
| Sleep, visor debounce, boop hysteresis, shared drawing | Useful behavior additions | Startup, wake ordering, boundary, and combined-control probes pass. F2 needs a focused lifecycle fix. These additions intentionally change the original behavior. |
| Deferred SPI initialization and explicit transactions | Clearer initialization and bus contract | Current constructors perform no SPI I/O; command ordering and mode checks pass. Physical startup reliability remains untested. |
| Increase from 4 to 8 MHz | Benefit insufficiently established | Nominal wire time decreases. No user-visible improvement or assembled-chain measurement is available; consider reverting this setting pending validation. |
| Settings and test tooling | Useful maintainability improvement | Central settings and rejection checks reduce scattered constants. Eight feature builds and meaningful sanitized tests are reproducible. Address the evidence gaps in F3–F5. |
| Removed zero transition arrays and old wrappers | Reasonable cleanup | The removed arrays are unused zeros; all retained initializer bytes match. The reported old wrappers are already absent. |

The sketch's globals and small functions are manageable at this size. A large framework or rewrite would add risk without a demonstrated need. The useful simplification is to clarify boop lifetime versus rendering state, keep a single authoritative current overview, and preserve historical evidence with explicit identities.

## Artwork, layout, interfaces, and memory review

The archive comparison parsed **20 current array initializers**: 16 static artwork arrays and four initial FFT buffers. All retained bytes match the archive. Only the two unused 16-byte all-zero `buffer0L`/`buffer0R` arrays were removed. All eight pin assignments, six icon positions, and all 14 tile rotations match. This establishes source preservation; physical panel ordering and appearance require the helmet.

Current `Icons.h` SHA-256:
`b977dbde17338799f8cdf658e13e08c91e20d7a67e68f255ac0c24f60e5bb645`.
Evidence: [archive comparison with array hashes](../build/independent-audit/20261003-214127/archive-comparison.json).

The driver allocates one checked block of **140 B** for this layout, plus allocator overhead. Drawing clips signed logical coordinates before indexing; dimensions and tile setters are bounded; copies are prohibited; destruction frees storage. Current application FFT inputs are bounded by the ten-bit ADC range. Its rolling sum and rendered magnitude mapping fit the target's integer types. No memory violation was reproduced in current valid application paths.

The sketch ignores `matrix.begin()`'s failure return. Driver operations remain safe no-ops after allocation failure, so this is a low-priority fault-reporting opportunity: a future fix could make a failed display initialization visible to the operator. It is unlikely with the small allocation and available memory.

The driver interface has changed historically: callers must initialize it from `setup()`, and `begin(uint32_t)` returns a status. Its project-local README explains this. The BSD license is byte-identical to the archived driver's license, and attribution is retained.

Timing comparisons use explicit 32-bit elapsed arithmetic and constrained intervals. Existing tests plus independent probes cover millisecond/microsecond wrap, boop expiry, full blink completion, and pause/resume behavior. Normal animation counters remain bounded by their state transitions. Settings rejection checks cover the documented user-facing ranges; the extra probe checks SPI limits and the signed-half-range timing boundary.

The firmware has no application command parser, networking, stored credentials, or input-dependent runtime allocation. The relevant software safety surface is GPIO/ADC-driven state, buffers, arithmetic, and driver ownership. This review covers those paths and used dependency interfaces; it is not an exhaustive internal audit of the Teensy core or every external library.

## Fresh validation and its limits

Environment:

| Component | Verified version |
| --- | --- |
| Arduino CLI | 1.5.1 |
| Teensy platform | 1.62.0 |
| Target compiler | Arm GNU Toolchain 15.2.Rel1 / GCC 15.2.1 |
| arduinoFFT | 2.0.4 |
| Adafruit GFX | 1.12.6 |
| Adafruit BusIO | 1.17.4 |
| Host tools | Apple Clang 21.0.0; Python 3.13.5 |

All builds select `teensy:avr:teensy40:usb=serial,speed=600,opt=o2std`.

| Enabled sensors | Errors / warnings | Flash, including headers | RAM1 variables | RAM1 available for locals |
| --- | --- | ---: | ---: | ---: |
| None | 0 / 0 | 34,812 B | 7,456 B | 484,064 B |
| Microphone | 0 / 0 | 39,932 B | 9,280 B | 482,240 B |
| Boop | 0 / 0 | 34,812 B | 7,456 B | 484,064 B |
| Boop + Microphone | 0 / 0 | 39,932 B | 9,280 B | 482,240 B |
| Visor | 0 / 0 | 34,812 B | 7,456 B | 484,064 B |
| Visor + Microphone | 0 / 0 | 39,932 B | 9,280 B | 482,240 B |
| Visor + Boop | 0 / 0 | 34,812 B | 7,488 B | 484,032 B |
| All three | 0 / 0 | 39,932 B | 9,312 B | 482,208 B |

RAM2 static variables are **12,416 B** in every build; the driver allocation is additional. These are linker measurements, not runtime stack/heap high-water measurements. Total flash includes alignment/header effects; identical totals can conceal differences in code size.

Validation completed:

- The unchanged host suite passed with AddressSanitizer, UndefinedBehaviorSanitizer, `-Wall -Wextra -Werror`, and nonrecovering sanitizer failures. It includes 3,584 pixel/rotation cases, driver layouts, eight sensor masks, and seven Python test groups.
- All eight real target feature builds and a separate default export succeeded with zero warnings.
- Eleven independent behavior scenarios checked startup-held/hidden/obstructed inputs, early/late audio bursts, combined controls, boop/blink rollover, and nominal SPI costs. Two scenarios explicitly reproduced F1/F2.
- A separate direct mathematical DFT checked 15 synthetic ADC frames across all 32 rendered bins, with a largest magnitude difference of **0.00926174**. Thirty-two asymmetric bar spectra also matched the intended physical order and left/right mirror. This avoids relying solely on the existing float/double comparison within the same FFT library. Evidence: [independent DSP output](../build/independent-audit/20261003-214127/probes/independent-dsp.log).
- Four archived-code scenarios reproduced two heap overflows, wrapped off-screen drawing, and a negative FFT shift. The old driver source/header were extracted unchanged. A permissive SPI stub allowed its constructor I/O solely to reach those memory paths. The old FFT renderer was isolated without changing its body. These are targeted defect comparisons, not a historical whole-firmware benchmark.
- Twelve additional configuration boundary cases and build/report failure probes behaved as recorded.

The existing default HEX and isolated clean export both hash to
`180b747d706e348bdc0f1e999475297f698a83deaa18a2990b4b0e5c7e200503`
and are byte-identical. Evidence: [artifact comparison](../build/independent-audit/20261003-214127/artifact-comparison.json).

Test model limitations were examined against the installed dependency source. GFX bitmap bit order and transparency agree for the used primitives, and target builds exercise real headers/prototypes. The simplified model does not establish physical scan behavior, rendering speed, resistor/pull-up behavior, voltage, interrupt load, SPI clock quantization, or actual ADC/FFT duration. Host `long` is 64 bits whereas target `long` is 32; the bounded application mapping arithmetic fits both.

The nominal timing probes observed:

| Model over 400 ms | Microphone samples | Completed frames | Minimum / maximum / mean sample gap |
| --- | ---: | ---: | --- |
| SPI 1 µs/byte; ADC 10 µs/read | 1,596 | 24 | 250 / 352 / 250.666 µs |
| SPI 2 µs/byte; ADC 10 µs/read | 1,572 | 24 | 250 / 496 / 254.465 µs |

These figures describe the synthetic model only. FFT and graphics consume no simulated CPU time. Cooperative acquisition can discard partial frames after long stalls; predictable audio timing beyond this visual effect would require actual measurements before choosing timer-driven acquisition.

The historical optimization report's 41,980 B flash / 9,632 B RAM1 baseline is supported by retained logs, but its exact source and matching dependency state cannot be reconstructed. It is not an independently reproduced before/after benchmark. The directly established storage/work reductions above are the defensible improvements.

## Prioritized actions

1. **Keep** the driver safety fixes, FFT indexing/DC corrections, dirty updates, sensor guards, button configuration/debounce, shared drawing, and cooperative sampling.
2. **Fix** F1's per-frame activity detection and F2's reaction lifetime before relying on the optional microphone/boop features. Add the paired burst and hidden-timeout cases to permanent tests.
3. **Consider reverting** the helmet's SPI setting to 4 MHz until the full chain is measured. Retain 8 MHz only with recorded hardware evidence and test the actual selected nominal timing.
4. **Fix** report provenance and the incorrect current-state claims. Bind each run's results to its source and completion state; retain prior evidence under separate identities.
5. **Simplify** the maintained documentation and clarify boop lifetime state. Avoid a broad rewrite or unrelated micro-optimizations.
6. **Establish Git history** before subsequent edits, with a reviewed baseline and focused changes. This was not performed during the report-only audit.

## Focused helmet checks

The manufacturer's constraints support the existing wiring notes: Teensy GPIO and analog inputs accept 0–3.3 V and are not 5 V tolerant; GPIO output current is recommended at no more than 4 mA, and VIN/VUSB sharing needs care with simultaneous USB and external power. [PJRC Teensy 4.0 electrical and power reference](https://www.pjrc.com/store/teensy40.html).

MAX7219 input HIGH is specified at 3.5 V minimum with a 5 V supply, so verify suitable translation for DIN/CLK/LOAD. The final DOUT remains disconnected from Teensy. [MAX7219/MAX7221 electrical table](https://www.analog.com/media/en/technical-documentation/data-sheets/max7219-max7221.pdf#page=3).

1. Verify supply, common ground, signal levels, RGB current limiting, and the actual board pinout. Start with sensors disabled.
2. Check every panel and expression, including left/right orientation, blinking, angry glitch, short release, long hold, and wake to the same expression.
3. Check display signals across the assembled chain at the chosen clock. Compare 4 and 8 MHz if retaining the faster setting; record any artifacts and measured timing.
4. Enable the visor and check initial hidden/open states, chatter, expression changes while hidden, and sleep while opening the visor.
5. Enable boop and check startup obstruction, both threshold boundaries, ordinary timeout, and a hide/reveal spanning the timeout. Confirm the intended fix requires release before a new reaction.
6. Enable the microphone and check silence, gain/sensitivity, brief sounds, speech, changing input bias after a pause, and speech during blink/glitch/control changes. Measure sampling gaps and FFT/display work if response is inconsistent.
7. Exercise connected sensors together and verify the display supply and temperature in the assembled operating setup. Record results against the exact source/image hashes.

## Coverage and evidence inventory

| Subsystem | Review and verification |
| --- | --- |
| Main loop and startup | Read completely; normal, hidden, held-button, sensor startup, initialization and wake order checked |
| Controls and optional inputs | Debounce, release/hold, sleep/visor combinations, hysteresis, exact boundaries and timeout checked |
| Blink, glitch and expression drawing | Read completely; frame restoration, expression reset, speech priority, full blink and rollover checked |
| Microphone and FFT | Collection, frame discard, bias/envelope resets, gating, DC removal, window, float reference, bar indexing and contention checked |
| MAX7219 driver | Ownership, allocation failure, dimensions, coordinate/tile rotation, mapping, clipping, dirty register routing and transactions checked |
| Artwork and board configuration | Initializers, pins, positions, rotations, feature defaults and target guards compared |
| Build and test tooling | CLI lookup, feature rewriting, warning/failure propagation, temporary copies, generated artifact identity and model assumptions checked |
| Dependencies and instructions | Installed versions, used GFX/FFT/SPI/core interfaces, local license, source/report discrepancies and manufacturer requirements checked |

The archived software was used as a historical comparison. Head-model/fabrication archives and the historical Nano schematic were outside this active software review.

The baseline manifest covers **50 pre-existing project/evidence files**. The active firmware/tooling subset covers **24 files**, with fingerprint
`ea5aa2096462fc80368bcfdf37364fd64b86acf591ff2cc6ef3b716415489ee0`.
The fingerprint is SHA-256 of sorted relative path, space, file SHA-256, and newline. Full manifest SHA-256:
`fa2ee9ef61b7acf49f0c5b4ea195d750e9de61959788e491a0eceec0fc4b58b7`.

Fresh evidence:

- [original source/artifact manifest](../build/independent-audit/20261003-214127/source-manifest.json), [active fingerprint](../build/independent-audit/20261003-214127/active-source-fingerprint.json), and [final preservation check](../build/independent-audit/20261003-214127/preservation-check.json).
- [toolchain inventory](../build/independent-audit/20261003-214127/toolchain.txt), [dependency hashes](../build/independent-audit/20261003-214127/dependency-hashes.json), and [platform hashes](../build/independent-audit/20261003-214127/platform-hashes.json).
- [host suite output](../build/independent-audit/20261003-214127/host-tests.log), [all eight target build outputs](../build/independent-audit/20261003-214127/snapshot/build/feature-checks.log), [default build output](../build/independent-audit/20261003-214127/default-build-run.log), and [parsed memory measurements](../build/independent-audit/20261003-214127/build-metrics.json).
- [default image symbol inspection](../build/independent-audit/20261003-214127/default-symbol-check.txt), including the distinction between FFT buffers and an autogenerated matrix-initializer label.
- [diagnostic observations and exact compiler commands](../build/independent-audit/20261003-214127/diagnostic-results.json), [tool probe results](../build/independent-audit/20261003-214127/tool-probe-results.json), and [archive comparison](../build/independent-audit/20261003-214127/archive-comparison.json).

Preserve this evidence directory if cleaning generated build files.

To reproduce the audit checks on the retained isolated copy, run each block
from the repository root.

```sh
cd "build/independent-audit/20261003-214127/snapshot"
./scripts/test-firmware.sh
PYTHONDONTWRITEBYTECODE=1 python3 scripts/check-features.py
./scripts/build.sh
```

```sh
cd "build/independent-audit/20261003-214127"
PYTHONDONTWRITEBYTECODE=1 python3 probes/run.py
PYTHONDONTWRITEBYTECODE=1 python3 probes/tools.py
```

The diagnostic runner deliberately expects sanitizer failures from archived defective code and labels reproduced current defects in their case logs. Its successful exit means those observations were verified. The audit probes share the project's host stubs; their scenarios and expectations are new, while physical-model limits remain.

**Completion:** every active subsystem listed above was reviewed; material claims are backed by the linked evidence or explicitly marked unverified. Firmware changes and physical helmet validation remain separate follow-up work.

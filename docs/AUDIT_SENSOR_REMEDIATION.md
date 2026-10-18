# Sensor acquisition and visor connector remediation

The October 2026 audit's ADC-latching and Teensy pin-12 findings were independently confirmed. Existing physical pins, hardware pilot gating, the 4 kHz microphone timer, and optional-feature defaults are preserved.

## Confirmed findings and changes

| Finding | Evidence | Remediation |
| --- | --- | --- |
| ADC1 failure persists until reboot | Original `serviceSensorAcquisition()` immediately returned whenever `failed` was set. Timeout, out-of-range reading and failed-start fault injections all remained failed after the source recovered. | Stop the failed conversion, invalidate both ADC1 measurements, disarm physical boop, then perform one asynchronous probe after a bounded backoff. Every enabled channel must provide a new valid sample before clearing the acquisition fault. |
| A microphone fault disables its own recovery path | Original rail/no-progress handling set `microphoneEnabled=false`; main then never called `microphone()` again until an explicit enable command. | Keep the user's enabled preference, stop speech/timer on failure, and retry only while normal control policy permits microphone operation. Require 256 consecutive off-rail samples (64 ms at 4 kHz) before clearing a recovered fault; discard probe samples. Explicit disable continues to stop sampling and retries. |
| SPI initialization overwrites pilot GPIO | Pinned Teensy 1.62.0 `libraries/SPI/SPI.cpp`, `SPIClass::begin()`, writes both the pad control and mux for `hardware.miso_pin[miso_pin_index]`; the SPI0 table selects pin 12. `matrix.begin()` calls this on initial startup and every reconnect. | The MAX7219 driver is write-only. Immediately after every `SPI.begin()`, restore `Settings::DockPilotPin` as `INPUT_PULLUP` on Teensy 4.0. No receive data is used and no pin moves. |
| Previously valid rail voltage could qualify a later faulted connection | The old accessor returned an undated cached value; power cycling did not invalidate a conversion begun before that cycle. | Timestamp completed samples; readings older than 20 ms are invalid. Invalidate/cancel a pending voltage conversion before requesting new dock power. Qualification requires a fresh valid measurement taken in this power cycle. |
| Holding the signal buffer off throughout initialization would drop MAX7219 setup | A more realistic host model that ignores SPI transfers while the buffer is disabled reproduced a missing scan-limit register. This was caught during remediation review. | `Max72xxPanel::begin()` accepts an optional bus-ready callback after SPI/pilot restoration and before register writes. The connector callback rechecks the physical pilot, visibility and fresh voltage before enabling the buffer. The post-initialization interlock check remains. |
| Duplicated GPIO numbers | ADC1 completion used literal 21 and 15 separately from scheduling/setup. | Use existing `irPin` (A7), and centralized dock constants in `Settings.h`: power 6, buffer 4, pilot 12, sense 15. Physical wiring and geometry remain unchanged. |

Do not use `SPI.setMISO(255)` to disable MISO on this pinned Teensy library. Its 255 table entry is a sentinel; the Teensy 4 implementation does not supply a supported disconnected-MISO mode. Restoring the unused input pin to GPIO is the local write-only solution. The independent hardware pilot gate still governs power/buffer safety when software is executing SPI initialization, has not yet observed detachment, or cannot run.

## Recovery policy and diagnostics

Conversion timeout is 5 ms. Recoverable failures are timeouts/no progress, out-of-range samples and a failed acquisition start on a verified supported pin. Retry delays are 100, 200, 400, 800, 1600, 3200, then 5000 ms, capped thereafter. There is no retry loop inside a main-loop pass and no heap allocation during recovery. Persistent hardware faults remain invalid and get low-duty probes, allowing a repaired/transient input to recover without rebooting. Invalid configuration pins and a missing ADC allocation are permanent faults requiring configuration correction/restart; no repeated allocation is attempted.

ADC1 recovery calls its supported `stopContinuous()` (selects the disabled channel), `resetError()` and `startSingleRead()`. It does not reconstruct the shared `ADC` object, recalibrate, stop ADC0, or alter ADC0's timer. This avoids the library's calibration waits in the recovery path. Microphone restart uses the existing supported software-conversion, interrupt and 4 kHz timer setup; no processor-frequency change is introduced. The library's initial ADC_ETC software reset has an existing conditional 5 ms setup delay; no new delay is added by this remediation.

Both sensor groups expose failure, retry and successful-recovery counters plus last reason/state getters. Existing `sensor_faults` mask 1 is dock qualification, mask 2 ADC1 acquisition and mask 4 microphone acquisition. `microphone_enabled` represents the user's preference; the fault bit/recovery state indicates whether speech acquisition is usable. Enabling a microphone no longer clears a fault before a successful probe, and calibration refuses a faulted microphone. The USB `SENSORS` command reports the two groups' state, last reason and counters without expanding the radio/UART status payload. These are runtime diagnostics; no EEPROM or radio/UART packet layout changes are needed.

Recovery state codes are 0 running, 1 waiting for retry, 2 permanent failure; microphone state 3 is qualification after a retry. Last failure codes are 1 missing ADC allocation, 2 conversion timeout/no progress, 3 invalid/railed input, 4 failed start and 5 unsupported pin. A zero reason means no observed failure. Counters are diagnostic 32-bit counters and may eventually wrap.

## Executed verification

Tests compile the actual sketch and MAX7219 driver against the repository's hardware boundary stubs using Clang C++17, `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer with recovery disabled. Fault regression assertions were observed failing on the previous implementation before the fixes.

- `scripts/tests/sensor-recovery.cpp`: 12 scenarios passed: buffer-gated initialization and redocking at requested brightness 15 without exceeding the configured ceiling; detachment during SPI initialization; pilot restoration on startup/reconnect/direct reinitialization; timeout/backoff/recovery; invalid values and boop disarming; rejected start recovery; stale voltage cutoff and physical-boop disarming; no previous-cycle voltage reuse; bounded persistent failures; permanent unsupported pin; millis rollover and diagnostics.
- `scripts/tests/microphone-recovery.cpp`: 9 scenarios passed: sustained rail, invalid samples, failed start, stopped ISR, bounded persistent failure, explicit disable/re-enable, permanent unsupported pin, millis rollover, and ADC1 fault/recovery while ADC0 retains its 4 kHz timer. The timing model produced consecutive samples exactly 250 microseconds apart during the ADC1 fault/recovery case.
- Both runners are integrated in `scripts/test-firmware.sh`, so default sanitizer CI runs exercise them.
- All five real `scripts/check-extensions.py` Teensy variants compiled with zero warnings and `-Werror` in snapshot run `build/runs/20261017T191920Z-extensions-03f8886e`. The full-feature variant reported 90,108 bytes flash and 18,016 bytes RAM1 variables. The build runner correctly rejected that run as current-tree evidence because other audit work changed files while it ran; the root final validation must produce a fresh source-bound run.
- After correcting the old intensity-15 test and the old stale-held-boop wake expectation, the complete host runner passed every C++ suite (including all 21 new scenarios), all eight sensor feature masks, authenticated UART and all 18 `test-tools.py` checks. It then exposed a `host_support` import-path error in the independently modified PQ-worker test; the root agent corrected that import and separately passed the eight normal and eight optimized-Python PQ tests. The final consolidated whole-run results belong in the main report.
- The final stale-boop diagnostic edit (`boopReading=-1`) was also observed red before implementation; all 21 focused ASan/UBSan scenarios passed again afterward. `git diff --check` passed.


## Review locations

| File | Relevant lines / responsibility |
| --- | --- |
| `firmware/PROT_OS7_MAX7219/Settings.h` | Lines 7–15: existing dock pin constants and bounded timeout/freshness/retry settings; later assertions reject zero/half-range-overflow or inconsistent intervals. |
| `firmware/PROT_OS7_MAX7219/SensorHardware.ino` | Lines 3–65: ADC1 failure state/counters; 67–94 initialization; 96–120 invalid/stale accessors and power-cycle invalidation; 121–172 bounded scheduling/recovery. |
| `firmware/PROT_OS7_MAX7219/Microphone.ino` | Lines 1–51: fault state/counters; 55–92: sample qualification and checked acquisition start; `microphone()` applies backoff and recovery qualification; `calibrateMicrophone()` rejects failed input. |
| `firmware/PROT_OS7_MAX7219/VisorConnector.ino` | Lines 9–18: buffer qualifier/safe disable; 37–60: new-cycle voltage invalidation, guarded initialization and final interlock check. |
| `firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.{h,cpp}` | Optional bus-ready callback; `begin()` restores write-only SPI's unused MISO pin to pilot GPIO before display traffic. |
| `firmware/PROT_OS7_MAX7219/Control.ino` | Line 63: invalid/stale physical boop clears state/arming and reports reading -1. |
| `firmware/PROT_OS7_MAX7219/{UsbControl,PawLink}.ino` | Enable commands preserve fault status until acquisition actually recovers; root integration exposes USB diagnostics. |
| `scripts/tests/{sensor-recovery,microphone-recovery}.cpp` | Actual sketch/driver fault-injection scenarios, integrated into `scripts/test-firmware.sh`. |
| `scripts/tests/stubs/{ADC,SPI}.h` | ADC timeout/start/pin injection and counters; realistic SPI pin remux/buffer gating plus init-time detach hook. |

## Hardware-dependent qualification still required

No physical measurements were performed. Confirm actual pad mux/pull-up and pilot reads after initial setup, every reconnect and a deliberate SPI reinitialization; scope buffer/power gating during first-break contact detachment and power ramps. Confirm every MAX7219 receives its setup registers through the real buffer. Disconnect/stall ADC inputs and check invalid diagnostics, recovery cadence, and rail freshness while asleep and awake. Measure ADC conversion/interrupt timing, microphone noise/clipping, voltage-divider tolerance and real paw-to-visible latency under maximum qualified display load. Host timing and successful compilation do not establish those electrical or latency properties. The hardware gate, overvoltage protection and qualified power path remain necessary.

## Independent final review correction

The final reviewer reproduced an additional stale-completion bug using normal
`loop()` ordering and a hardware-like latched ADC result: a good conversion
left pending for 101 ms was consumed later and timestamped as fresh, allowing
the connector to remain ready after a rail change. The new `stalecompletion`
regression failed before the correction and passed afterward under ASan/UBSan.
Acquisition now checks the pending conversion age before `isComplete()` and
uses its start time for accepted boop/voltage timestamps. This is the 22nd
focused recovery scenario. The ADC stub now latches single-shot results while
retaining timed microphone sampling. Final full-suite results are in the parent
remediation report; the old snapshot counts above identify earlier executions.

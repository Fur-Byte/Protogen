# Efficiency implementation and physical qualification

The efficiency plan is approved with whole-system energy as the primary outcome,
physical button-to-visible-expression latency (including idle wakeup), unchanged
animations/audio, unchanged security, and preserved radio reliability. Each
optimization must pass independently before a combined candidate is qualified.
**Physical energy savings and latency are unmeasured. No candidate is qualified.**

All eight bench switches in `src/HelmetProtocol/Efficiency.h` default to zero.
Production retains 600 MHz, 16 ms frame eligibility, the 4 kHz audio timer,
existing FFT/attack/release/hold timing, random blink behavior, 160 ms default
transitions, button release navigation, radio/security constants, continuous
receiver availability and existing saved settings. Instrumentation and explicit
new commands are additive; nothing is uploaded by these scripts.

## Independently selectable candidates

| Optimization / flag bit | Change when explicitly enabled | Production baseline |
| --- | --- | --- |
| `cache_face` / 0 | Compare every pose field and active spectrum mouth bytes before composition; force composition when `frameValid` is false | Compose at the existing render opportunities |
| `case_2hz` / 1 | Calibrated case ADC/NTC reads every 500 ms | Every 20 ms background pass |
| `oled_1hz` / 2 | Normal status at most 1 Hz; faults, warning changes, face/link changes and link expiry update immediately when serviced | 200 ms screen interval |
| `oled_pages` / 3 | Compare eight 128-byte pages; record only successful sends; invalidate all pages after a failed transfer | Full 1024-byte screen sends |
| `checkpoint_5min` / 4 | Commit changed integer mWh checkpoints every 300 s; failures retry after the interval | Commit every 60 s when energy is valid |
| `wearer_deadlines` / 5 | Block on face/FULL notifications and the next sensor, conversion, screen, stale-link or retry deadline | 20 ms polling |
| `main_events` / 6 | GPIO, UART queue and radio RX/TX-completion notifications; explicit debounce, retry, expiry and liveness deadlines | 5 ms polling |
| `oled_protection` / 7 | Lower contrast, small horizontal/controller vertical offsets each minute, display off for fresh explicit face sleep; warnings and wake restore it | Existing low contrast and fixed position |

The foreground event candidate requests a 20 ms maximum fallback for the serial
console and 5 ms servicing for busy/queued radio, handshake, pairing, command work,
UART backlog and held controls. These requests are subject to RTOS tick granularity:
the pinned C3 configurations use `CONFIG_FREERTOS_HZ=100`. The original
`vTaskDelay(pdMS_TO_TICKS(5))` therefore yields with zero ticks. The event candidate
also consumes notifications and yields when the next deadline is less than one
tick away, preserving the original active polling behavior rather than sleeping
past a deadline. Longer waits round down to whole ticks and GPIO/UART/radio events
can end them early. The nominal 5 ms request is not a measured physical service
bound. Physical latency, hold-action, command-deadline and radio qualification
remain mandatory. Production retains its SDK tick configuration and original
delay call. This first candidate does not claim a fully
notification-only foreground or MCU sleep. Receive-budget leftovers continue
to be serviced. The wearer task keeps its 5 s I2C backoff and 10 ms SHT conversion
deadline; environment/INA reads stay at 1 Hz. A stale/missing link restores the
OLED warning display even if the last face status said asleep.

`PERF` on Teensy reports render attempts, compositions/skips, changed logical
frames, wakes, render time, and attempted/verified configuration writes. Existing
`STATUS` supplies audio frames, dropped samples and actual ADC timer frequency.
`PERF` on ESP supplies reset reason, heap/current minimum, task work/executions,
radio sends/completions/receives/drops, ADC/sensor reads, OLED pages/bytes,
checkpoint attempts/commits/failures, verified configuration writes and INA energy
deltas. Counters are since boot; renderer counters saturate, some byte/radio
counters wrap and must be differenced as unsigned values over bounded trials.
Neither task work time nor ACK time measures physical display latency or total
CPU utilization. Interrupt/driver work needs separate CPU profiling for headroom.

INA `measured_mwh` accumulates valid measured intervals independently of `FULL`
and checkpoint SOC. It excludes the boot/initial-read interval and invalid sensor
gaps. `used_mwh` is a charge estimate; after reset its old checkpoint never
qualifies SOC. Interrupted/failing checkpoint writes do not advance the remembered
saved value. Pairing/config saves remain immediate and now verify NVS readback;
no pairing format or cryptographic parameter changes. A five-minute policy reduces
scheduled commit opportunities by 80%; it does not establish flash lifespan.

## Build and software verification

Install prerequisites (Python 3 with venv, C++ compiler and CMake) and then:

```sh
python3 scripts/install-toolchains.py
source build/toolchains/environment.sh
scripts/test-firmware.sh
bash scripts/test-efficiency.sh
python3 -O scripts/tests/test-efficiency.py
python3 scripts/check-features.py
python3 scripts/check-extensions.py
python3 scripts/build.py
python3 scripts/build-esp32.py
CMAKE=cmake scripts/test-wireless.sh
```

The installer follows `toolchain.lock.json`, verifies the CLI against official
release checksums, uses isolated build directories, retains downloads/dependency
sources and records Python package versions. It installs development C3 targets;
it never changes SDK pins or selects S31. CI runs both sanitizers, normal and
optimized Python, sensor/extension builds, production builds and both radio roles.
On this ARM host GCC AddressSanitizer cannot reserve its shadow address region,
including outside the sandbox; local runs can explicitly select
`CXX=g++ PROTOGEN_HOST_SANITIZERS=undefined`. Such runs are **UBSan only**.

Build matched all-feature bench images without replacing production exports:

```sh
python3 scripts/build-efficiency.py baseline
python3 scripts/build-efficiency.py cache_face
python3 scripts/build-efficiency.py case_2hz
# Repeat separately for each row above.
python3 scripts/build-efficiency.py teensy_clock --teensy-mhz 528
```

Outputs, actual compiler flags/clock, hashes and logs live in `build/runs/`.
Each invocation changes only one optimization. All-feature images require the
corresponding connected/qualified sensors and dock. Clock trials reuse the matched
baseline radio images; they never select a production clock. Start with 528 MHz,
then test board-supported lower clocks. Select a clock only after measured system
energy improves, all workload checks pass, and steady-state CPU profiling shows
at least 50% headroom with interrupt work included. Clock alone proves no savings.

For ESP32 flag verification, run PlatformIO's `-t compiledb` separately for each
role and retain the project's `compile_commands.json` before exporting the other
role. The SDK's separate CMake database omits PlatformIO role/optimization defines.
Check the actual PlatformIO command and the board's `PERF` flag mask during every
physical capture; requested build flags alone are insufficient evidence.

## Physical measurement procedure and capture format

Measure helmet input energy with the existing calibrated INA228, and paw input
energy with an external meter (the paw voltage divider does not measure energy).
For whole-system qualification include boot/wake and invalid INA intervals with
external measurement; cross-check INA deltas. Verify the meter boundary includes
the display, MCU/radio, sensors and regulators. Use the same batteries/supplies,
temperature range, brightness, artwork, radio environment, firmware features and
security configuration. Preserve module IDs and calibration in
`hardware/calibration-records.csv`; use the single wiring reference
[HARDWARE_WIRING.md](HARDWARE_WIRING.md) and `hardware/connector-map.csv`.

Run at least three alternating matched baseline/candidate trials of at least ten
minutes each, plus sustained full-workload runs and a repeatable paw runtime test.
Replay identical physical button actions, microphone recordings, animation idle,
boops, sleep/wake, reconnection, pairing/renewal and loss conditions. Measure
renewal overhead within the energy workload. Benchmark the optimizations alone;
then repeat the complete qualification on the selected combination. Record meter
uncertainty and require reproducible energy reduction beyond that uncertainty.

Capture button GPIO/contact edges and the target expression's emitted light on a
common instrument timebase, using a photodiode with appropriate expression
discrimination or calibrated high-speed video. A UART ACK, SPI LOAD edge or first
eye-closing transition is not proof of the requested visible expression. Capture
at least 1,000 actions per condition (`online` and `idle_wake`) per trial. Retain
bounce, hold time, debounce, radio retries, display scan latency and wake latency.

Navigation controls activate on **button release** (25 ms debounce on helmet pin 2; 30 ms debounce on the wireless paw) to differentiate short taps from a long hold-to-sleep (1200 ms). Because of this release trigger and animation structure, latency must be analyzed and reported across three distinct intervals:
1. **Full physical press-to-expression latency:** Measured from initial physical contact depression to final visible expression; inherently includes the user's hold duration prior to release.
2. **Action-edge to first-visible-reaction latency:** Measured from physical contact release break to the first visual frame (eyelids starting to close on the old face). Includes release debounce (25 ms helmet / 30 ms paw), RF and UART bridge transit (for paw remote), loop dispatch, frame quantization (up to 16 ms), and SPI transfer.
3. **Action-edge to complete-expression latency:** Measured from physical contact release break to the fully settled target expression artwork. Includes release debounce, RF/UART latency, the full transition animation (default 160 ms; configurable 0–1000 ms), and MAX7219 scanout.

Long press-to-expression latency and transition delay must remain in the physical result. Do not reinterpret a transition frame as the target expression, subtract hold/debounce time, or change timing to make a result pass. A result above 100 ms is a failed target, requiring an explicit user-approved behavior/timing decision. Do not claim the 100 ms target is achieved without physical measurements on a calibrated instrument timebase (photodiode or high-speed video). The default 160 ms transition alone exceeds 100 ms complete-expression latency, and shorter transition candidates exclude RF, UART, render loop quantization, and display scanout latency. Furthermore, saved EEPROM settings persist across power cycles and override `Settings.h` defaults on boot; changing compiled firmware defaults does not alter saved helmet configurations without explicit `DEFAULTS` and `SAVE` commands. Report physical action-edge latency alongside full press latency for diagnosis.

Each trial has `energy.csv` with monotonically increasing cumulative readings:

```csv
time_s,helmet_j,paw_j
```

and `latency.csv` (times in microseconds on the same instrument clock):

```csv
condition,press_us,action_edge_us,visible_expression_us
```

Use an empty visible field for a missed response; never remove failed trials.
`action_edge_us` is press for a press-triggered action, release for navigation.
`run.json` contains `measurement: "physical"`, `optimization` (baseline or a
table name), integer `flags`, `teensy_mhz`, unique matched `trial`, relative
`energy_csv`/`latency_csv` paths, and matching `source_sha256`, `workload_sha256`,
`settings_sha256`, `security_sha256`, `rig_id`, `calibration_id`, `operating_range`
and `hardware`. Store instrument configuration, raw capture references, actual
firmware checksums and voltage/thermal conditions with the run. `checks` contains
explicit booleans for all requirements listed in `analyze-efficiency.py`, backed
by recorded evidence. Clock candidates add `steady_cpu_headroom_percent`.
Each run also supplies positive `energy_uncertainty_j` for both meters together
and matching `uncertainty_confidence` (at least 0.95 and below 1). The candidate's
upper wattage bound must be below the baseline's lower bound in every trial.

```sh
python3 scripts/analyze-efficiency.py \
  --baseline a1/run.json a2/run.json a3/run.json \
  --candidate b1/run.json b2/run.json b3/run.json \
  --output build/efficiency-measurements.json
```

The analyzer checks independent switches, matching metadata/durations, cumulative
energy validity, sample counts, missed responses, both physical p99 targets and
explicit validation checks. Its report hashes the raw CSVs and reports system
joules/watts and measured reductions. It cannot authenticate the instrument,
prove metadata/check assertions or estimate uncertainty from three trials;
review the raw evidence before activation. Missing data is not a measurement.

Accept only unchanged artwork/animation/microphone timing; p99 physical latency
under 100 ms in both conditions; no overruns, memory growth, watchdog resets,
missed command deadlines or reduced radio reliability/security; successful
rollover, reconnection, sleep/wake, packet loss and interrupted-storage tests;
and reproducible whole-system energy reduction. Record sustained-test duration,
reset/heap/audio/radio counters before/after, RF range and confidence intervals.

## Brightness and S31 work awaiting qualification

Indoor/Outdoor profiles differ from Everyday only in name/brightness (1 and 3).
Applying either is explicit; no default changes. Outdoor 3 is a candidate rather
than a calibrated outdoor requirement. `BOOST <0..15> <1..30000 milliseconds>`
sets a temporary session intensity, restores saved/runtime brightness on expiry,
and never writes settings. Applying configuration or reconnecting the visor
cancels a boost. This is a light-output convenience, not a claimed energy saving.

Automatic temperature/battery dimming is deferred until fitted battery limits,
thresholds, hysteresis and the brightness-vs-expression response are qualified.
Do not invent a charger control or infer charge/storage limits from cell voltage.
Temperature maximum and raw readings support trend capture without automatically
altering light output. Existing warnings remain available.

S31 DFS, power locks, automatic MCU sleep and ESP-NOW receive windows have no
active implementation here. They remain in the isolated adapter qualification
phase under [WIRELESS_V4_HARDWARE_GATES.md](WIRELESS_V4_HARDWARE_GATES.md).
The pinned legacy SDK is 6.1.0; Espressif lists 6.1.1 onward as the S31 supported
tag baseline ([official FAQ](https://esp32-s31.espressif.com/en/faq)). A version
number alone does not satisfy the project's security/API/board gates. Never port
by bypassing protection/calibration or changing radio/security parameters.

After those gates, benchmark DFS alone before MCU sleep, then paw receive windows
alone. Keep the helmet receiver continuously available initially. Acquire locks
only for measured peripheral/crypto work; keep the dedicated ML-KEM worker blocked
between jobs with its planned 48 KiB allocation. Wake on existing buttons and
scheduled radio events, retaining awake service through action/ACK, pairing and
renewal. Qualify entropy, timer/peripheral service, RF loss/recovery and physical
idle-wake latency under the full workload before any deployment. Espressif's
[power-management documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/system/power_management.html)
describes the APIs; it does not establish this hardware's energy or reliability.

## Recorded local software verification — 2026-10-09

The final runtime/tooling source identity is
`1bf6033ab0cc902b9f4d90bf4f9a7c316978d472eb903b291f8e4d24deae975e`.
Matching successful manifests cover the default Teensy image, both legacy C3
roles, all eight sensor feature builds, and the independent `main_events`
candidate with all Teensy features and both C3 roles. All compiled with zero
warnings. PlatformIO's actual compiler commands confirmed the role defines and
`PROTOGEN_MAIN_EVENTS=1` for both candidate images.

All five wireless/visor extension builds also passed with zero warnings. Their
whole-repository manifest predates the final ESP32-only deadline correction;
per-file hash comparison confirmed their Teensy firmware sources are identical
to the final sources. The final scheduler yields for sub-tick foreground
deadlines; its default production path retains the original delay call.

Local host checks used **UBSan only** because of the ARM AddressSanitizer mapping
limitation above. Firmware and cached-renderer regressions passed, including
3,584 mapping cases, microphone buffering/FFT, animation timing, wake restoration,
boost expiry, rollover and checkpoint failures. Comparison with the four archived
original faces found zero logical or physical pixel differences. Actual NVS save
code passed mocked set/commit/read failures and corrupted/short readback tests;
the separate atomic-storage model passed 135,983 fault cases. Wireless tests
passed one million authenticated records, one million protocol-event transitions,
pairing/replay/loss/recovery cases, and UART interoperability/malformed streams.
Normal and optimized Python tooling checks passed. These are software tests,
not RF, flash endurance or physical display measurements.

Recovery packaging was exercised with real matching build artifacts, complete ZIP
checksum verification and exact retention of a previous verification ZIP. No
installed configuration was available; its absence was recorded. No physical
USB boards or energy/optical captures were available. Whole-system savings, paw
runtime, physical online/idle-wake p99 latency, sustained hardware operation and
S31 qualification remain pending. No optimization has been promoted.

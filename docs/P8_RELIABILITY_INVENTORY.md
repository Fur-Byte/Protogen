# P8 reliability and recovery inventory

The inherited source baseline is P7 merge
`2741065bf2ff01a1ca25f9094bce54938a8acd01`. P8 release evidence must name its
own exact source, feature flags and build identity. Historical evidence
applies to the source and conditions on which it was collected.

## A. Already implemented

| Owner | State and recovery boundary | Existing infrastructure |
| --- | --- | --- |
| Teensy `setup()` and foreground loop | Setup loads validated settings, configures the MAX7219 chain and starts required services. The loop owns display shutdown, sensor/audio retries, command admission, rendering and local diagnostics. ADC ISR acquisition only supplies samples and counter updates. A powered external display can outlast a CPU fault. | [Sketch](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino), [microphone](../firmware/PROT_OS7_MAX7219/Microphone.ino), [sensors](../firmware/PROT_OS7_MAX7219/SensorHardware.ino), [P4 ownership](P4_BEHAVIOR_CORE.md). |
| Teensy `ConfigStore` | Validated v1/v2/v3 records across two EEPROM slots. CRC, sequence ordering and last-write commit marker select a recoverable record. Runtime brightness is bounded by request, approval and compiled ceiling. | [Config store](../firmware/PROT_OS7_MAX7219/ConfigStore.h), [brightness qualification](RELEASE_BRIGHTNESS_QUALIFICATION.md), [compatibility](../scripts/test-config-compatibility.sh) and [configuration safety](../scripts/test-configuration-safety.sh) probes. |
| C3 `app_main`, storage and workers | Each role owns link/session and NVS configuration on the main task. Existing callback ingress and wearer worker publish bounded observations. Storage failures disable wireless without silently erasing a pair. V3 authentication precedes replay commitment. New boots and sessions require current generation and fresh tickets before commands can execute. | [C3 startup](../firmware/esp32/src/main.cpp), [NVS policy](../firmware/esp32/src/SecureStorage.cpp), [session](../firmware/esp32/src/WirelessSession.cpp), [wireless recovery](WIRELESS_VALIDATION.md), [wireless tests](../scripts/test-wireless.sh), [UART admission tests](../scripts/test-uart-admission.sh). |
| P3 recorder | Single-writer volatile ring on each controller. Bounded local Teensy export and C3 debugger inspection at an owner-safe point use allowlisted observations. Boot resets logical ownership and cursors. | [P3 instrumentation](P3_INSTRUMENTATION.md), [recorder host parity](../scripts/test-flight-recorder.py), [target placement checks](../scripts/check-flight-builds.py). |
| P4–P7 behavior | Shared core owns expression, transition, sleep and boop state. P5 adds bounded opt-in animation tracks, P6 adds validated declarative timelines and P7 adds Audio V2 acquisition/detection handling and mouth behavior behind its existing flag. Hardware and security admission stay under the platform owners. | [P4 behavior](P4_BEHAVIOR_CORE.md), [P5 animation](P5_ANIMATION_V2.md), [P6 timelines](P6_DECLARATIVE_TIMELINES.md), [P7 audio](P7_AUDIO_BEHAVIOR.md). |
| Deterministic replay and mutations | P2 exercises the sketch adapters with ordered virtual events under repeatable stepping. The independent assertions compare modeled outputs with fixture expectations. The mutation runner isolates intentional changes to guarded invariants. | [Replay runner](../scripts/twin/replay.py), [test cases](../scripts/twin/test_replay.py), [P2 scope](P2_DIGITAL_TWIN.md), [mutation runner](../scripts/check-invariant-mutations.py). |
| Build and release tooling | Shared build manifests track source and pinned dependencies. The inherited packager verifies hashes, role/UART compatibility and an identified previous archive. The Git clean-source requirement is a separate provenance check from the source-content hash. Recovery uses existing USB/physical tools and matched artifacts. | [Build support](../scripts/build_support.py), [packager](../scripts/package-release.py), [matching and recovery](RELEASE_RECOVERY.md), [setup and pinout](README_RECOVERY.md). |
| Existing CI | PR runs target the fast regression path. Manual/full coverage extends the regression and build matrices. The source controls consolidation and duplicate-run avoidance. | [Workflow](../.github/workflows/firmware.yml), [CI efficiency](../AGENTS.md). |

## B. Independently verified

- The [October remediation review](RELEASE_READINESS_2026_10_17.md#fresh-independent-regressionsecurity-review)
  independently checked targeted sanitizer, timing and protocol regressions and
  reproduced the corrected stale-status and lost-ACK fence behavior at its
  historical source. Its scope excluded actual hardware and RF measurements.
- P1 uses existing visual oracle checkpoints. P2 independently asserts
  externally observable replay behavior and compares event-driven stepping
  with reference stepping. P4 captured its exact P3 reference separately,
  and P7 compared disabled Audio V2 against a separately built P6 reference.
  [P1 compatibility](P1_COMPATIBILITY.md), [P2 digital twin](P2_DIGITAL_TWIN.md),
  [P4 parity](P4_BEHAVIOR_CORE.md#verification-and-resource-evidence),
  [P7 local qualification](P7_AUDIO_BEHAVIOR.md#local-qualification).
- The P8 host journal uses independent binary fixtures and checks every
  modeled interrupted-write prefix under empty and previously committed
  conditions. Exact physical reset behaviors remain outside that model.
  [Reset-retention tests](../scripts/tests/test-reset-retention.py).

## C. Incompletely tested

- EEPROM interruption cases model indexed byte writes and CRC/marker
  validity. The core's flash sector erase and compaction, power-related
  preservation and worst-case save time need physical evidence. C3 storage
  initialization, commit and recovery have the same measurement boundary.
- P2 replay remains bounded by its scenario and peripherals model.
  P5/P6/P7 recovery interactions and any expanded P8 campaign require exact
  seed coverage, virtual duration and declared budgets. A fast suite or
  timeout is partial evidence for the 100-seed, one-day target.
- P7's held-out synthetic set contains an 848 ms missed-activity interval in
  speech plus noise. Audio V2 default approval requires representative real
  microphone evaluation. [P7 qualification](P7_AUDIO_BEHAVIOR.md#local-qualification).
- Build flags and ELF placement establish compiler/linker behavior. Report
  only compiled combinations and separately measure stack high-water and
  CPU/service timing. A deliberate compatible predecessor fixture can test
  rollback validation while a real rollback requires an identified verified
  archive and a board recovery demonstration.

## D. Dependent on physical hardware

| Gate | Outstanding evidence |
| --- | --- |
| Identity | Actual serials/revisions, installed options, LED chain and rotation, power/regulators, wiring, calibration and instruments. [Manifest template](../hardware/p8-physical-manifest.template.json), [wiring](HARDWARE_WIRING.md). |
| Timing and acoustics | Physical service gaps, ADC loss, FFT/interrupt costs, stack high-water, optical response timing, and the actual installed microphone across wearer conditions. [Latency](RELEASE_LATENCY_QUALIFICATION.md), [hardware qualification](P8_HARDWARE_QUALIFICATION.md). |
| Display and electrical | Readability, brightness, flicker, power/thermal/rail margins, reset blanking, and an uninterrupted one-hour mixed load. [Brightness](RELEASE_BRIGHTNESS_QUALIFICATION.md), [power qualification](RELEASE_POWER_QUALIFICATION.md). |
| Wireless and restore | Paired RF realization, stale/session recovery, reconnect/action delays, real stable settings, and restoration of matching firmware. [Wireless validation](WIRELESS_VALIDATION.md), [release recovery](RELEASE_RECOVERY.md). |
| Watchdog and retention | Reset causes and domain for each candidate mechanism, deadline margin, safe recovery or repeated-reset fallback, linker/startup survival and output behavior. [P8 feasibility](P8_WATCHDOG_RETENTION.md). |

## E. Unsupported reliability claims

| Claim | Evidence boundary |
| --- | --- |
| A qualified 250 ms watchdog is available now. | The deadline is experimental. Healthy startup/save bounds, recovery and stale light have open measurements. |
| Foreground observation or an ISR heartbeat guarantees independent stall detection. | Foreground observes missed intervals after it resumes. ISR/idle activity can continue while control work stops. |
| A reset immediately blanks the face or restarts the whole assembly. | Powered MAX7219 modules, the peer board and separately powered peripherals have their own state. |
| Uninitialized sections or valid CRC prove retention or trusted boot state. | Linker placement, reset class and RAM survival need actual evidence. Retained diagnostics supply no command authority. |
| Build hashes, previous archives, host stress or successful compilation on their own establish production readiness. | Provenance, a known-good verified rollback, measured F3 hardware evidence and separate approvals are required for the intended release. |

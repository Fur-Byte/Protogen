# Comprehensive audit remediation — 2026-10-17

> Historical report: the [October20 targeted follow-up](POSTMERGE_REMEDIATION_2026_10_20.md) supersedes reconnect admission and the single first-visible latency interpretation.

> Historical first-pass report for commit3275ff8. The follow-up
> [release-readiness report](RELEASE_READINESS_2026_10_17.md) supersedes current
> storage/latency/power/S31/CI status: EEPROM format3 is an explicit migration,
> and the final hosted check result is tracked on existing PR1.

Branch: `codex/comprehensive-audit-remediation`; baseline `1956268`.
The source of the requested findings is the supplied Sonnet 5.5 remediation
brief. The historical audits, S31 preflight and later v4 core were checked
against the current code; inherited claims were not treated as proof.

Implementation preserves Teensy 4.0, the original MAX7219 mapping/artwork,
C3/v3 roles, 4 kHz microphone timer, 600 MHz CPU baseline, UART/wireless packet
formats, EEPROM/NVS layouts and authentication/key hierarchy. Production v4
remains compile-time blocked. No firmware was uploaded, eFuse changed, or branch
merged. Compilation and host timing are not proof of physical safety or latency.

## Finding disposition

| Original finding | Verification and remediation | Evidence / limits |
| --- | --- | --- |
| MAX7219 brightness accepted up to15 without hardware ceiling | Confirmed. `Settings::MaxBrightness=1` initially; clamp at every driver intensity write and startup/reset/reconnect/SET/BOOST path. Persisted/requested0–15 values retained, effective/max reported separately; Face Studio displays all three. | Ceiling0/1/15 sanitizer tests, register peak tracking, EEPROM reload and BOOST expiry; no electrically safe level claimed. Qualify RSET/current/supply/wiring before raising ceiling. |
| COBS output overflow | Confirmed by ASan. Overflow-safe checks at every write/reservation; null/capacity/malformed decode arguments fail with0. Existing wire bytes unchanged. | Requested boundary lengths plus4096,10,000 random long roundtrips, capacity canaries and malformed decoder input; ASan/UBSan. |
| USB configuration snprintf unsigned-length overflow | Confirmed. Shared bounded formatter handles negative/truncated results; all neighboring CONFIG/STATUS/FRAME/PERF/error builders checked; partial JSON replaced by complete fixed error. | Exact-fit/capacity sweep, long names/value maxima/80fields, real negative formatting result, JSON parser verification. |
| Duplicate detection limited to32bits | Confirmed. Small compile-time-sized boolean tracker uses checked indices; syntax and transactional apply preserved. |65 unique indices then duplicates; each actual field duplicate rejected atomically; no bit shifting. |
| Visor pilot12 versus Teensy SPI MISO | Confirmed in pinned SPI source. Every driver begin restores pilot GPIO; guarded callback qualifies rail/pilot before enabling buffer for MAX7219 init; final qualification before ready. Existing hardware interlock retained. | Realistic buffer-gated SPI model, startup/redock/direct reinit, detach during begin; actual pad/buffer waveforms remain unmeasured. |
| Hidden/sleep CPU continuously spins | Confirmed. Shallow interrupt wait only with hidden display and drained serial queues; periodic1ms core SysTick retained; no CPU/peripheral clock changes. PERF records wait duration/count. | Host policy/backlog/wake tests; physical WFI/current/latency measurement required. No numeric power saving claimed. |
| Transient ADC acquisition failure requires reboot | Confirmed for ADC1 and microphone. Independent capped100ms–5s retries; invalidation, fresh-sample qualification, diagnostic reason/counters; permanent unsupported-pin/allocation faults stop retrying. | Fault injection for timeout/start/invalid/rail/stall, rollover, explicit disable and ADC0/ADC1 isolation. No repeated heap allocation or blocking recalibration. |
| Duplicated pins/panel constants | Confirmed. ADC dispatch uses existing IR constant; dock pins centralized in Settings. Matrix dimensions/rotation count and FRAME dimensions derive from Face geometry with compile assertion. | Original mapping/artwork tests and builds; physical wiring unchanged. Static artwork/UI geometry is intentionally the original helmet layout. |
| Replayed broadcast Hello repeatedly tears down established link | Specific claimed teardown not reproduced: Online Link already rejects broadcasts/control. Two related effects confirmed and fixed: cached handshake replies bypassed250ms pacing; unicast captured Hello consumed active-data throttle. Callback filters operational broadcast/control before shared queue. |1000-command injection/recovery, paced retransmissions, admission/budget/refill tests plus existing protocol/crypto suites. Sustained RF interference and valid-key abuse remain possible. |
| UART key stored in ordinary Teensy flash | Confirmed physical limitation. Private header ignored with explicit public-fixture exception; no secret logging introduced, bounded tracked-file signature scan clean; reporting and residual extraction risk documented. | Authentication and key separation retained. Software obfuscation is not used as a physical-extraction defense. |
| Incomplete ESP32-S31 migration | Confirmed SDK/board blockers. Official latest v6.1 remains S31 preview; supported minimumv6.1.1 tag404. Pinned memory-protection TODO unchanged. Added explicit unsupported-target guard with isolated research opt-in; preview role/worker/UART-source cross-build preparation retained. | Fresh official checks, isolated preview builds/static resource analysis, board guard tests. No approved S31 pins/module, stable hardened SDK, physical UART/radio/security/power qualification. |
| Existing P-256 + ML-KEM-1024 v4 core correctness | Targeted review completed; no independently confirmed additional core defect. Added exact fragment expiry/overflow boundaries; preserved combiner/transcript/state/replay/cancellation/wipe protections and production exclusion. | Independent vectors,145 NIST cases, unchanged 42 vendor files, upstream functional tests, core/state/fragment/storage sanitizers and8 module static analysis. This is not a formal crypto audit or S31 runtime qualification. |
| Licensing/attribution | Original sketch's license grant remains unverified; no blanket license invented. Existing third-party notices retained; head model credited to ZillionRoss with downloaded CC Attribution notice/version uncertainty. | Archive inspection; upstream model page403. Author permission/exact license confirmation remains an owner action. |
| Unused DTLS adapter/options/test | Not unreferenced: production excludes adapter, but retained DTLS regression and CI use it. Kept required historical tests/options together. | Fresh real DTLS handshake/replay/loss/wrong-key regression. |
| CI permissions/action tags | Confirmed missing explicit least privilege and mutable tags. Read-only contents, credential persistence off, verified full action SHAs; all suites retained and new regressions integrated. | YAML parse/config assertions, host programs; remote Actions run is distinct evidence. |
| Private headers/secrets | Ignore rule added; public placeholder fixture retained. No matching private-key block/token/access-ID signatures or tracked private UART header outside fixture. | Bounded signature scan, not a proof that every possible secret representation is absent. No actual keys committed. |
| SECURITY reporting process | Added minimal public channel-request guidance, conditional GitHub private reporting, no invented address/response promise. | Existing trust boundary and physical limitations preserved. |
| Stale8.0 references | Live firmware already reports8.2. Retained expressly historical8.0 reports rather than relabeling their evidence. | Current source/reference review. |
| Icons CRLF | Normalized line endings/trailing whitespace only. | Initializer/artwork comparison:0 changed logical/physical pixels for all four original faces. |
| ~260KB generated evidence | Retained261,964bytes of historical source-bound/resource evidence; not exactly reproducible from current state/toolchains/time. New transient output stays ignored. | No unnecessary historical provenance deleted. |

## Newly discovered issues resolved

1. **Stale completed conversion:** independent review reproduced a101ms-old
   latched ADC result being relabeled fresh by normal loop ordering. A failing
   actual-sketch regression now passes: overdue pending conversions expire
   before reading completion, and valid values are dated by acquisition start.
   The ADC boundary stub now models hardware result latching.
2. **Stale physical-boop state/diagnostics:** expired readings disarm held-mode
   reactions and report-1; a fresh release is required before rearming. This
   safety correction is intentional; timed/remote reaction lifetime remains.
3. **Probe initialization behind a disabled buffer:** strengthened SPI model
   exposed lost initialization commands in an intermediate fix; the guarded
   callback enables the qualified buffer before programming displays.
4. **Build fingerprints included MCP runtime logs:** a red/green regression
   proves generated workspace output no longer changes firmware identity;
   real source edits still do.
5. **Host preflight portability:** Linux-only non-PIE flags and a hard-coded
   analyzer path prevented macOS checks. Platform-aware flags and installed/
   explicit analyzer selection preserve ASan/UBSan and warning enforcement.

## Detailed reports and measurement limits

- [Configuration/brightness](AUDIT_CONFIG_REMEDIATION.md): electrical assumptions,
  serializer and duplicate tests, exact source responsibilities.
- [Sensors/connector](AUDIT_SENSOR_REMEDIATION.md): ADC/core source evidence,
  retry policy, diagnostic codes, interlock tests and hardware checklist.
- [Wireless/v4](AUDIT_WIRELESS_REMEDIATION.md): complete requested property review,
  confirmed effects, source maps and adversarial validation.
- [Platform/maintenance](AUDIT_PLATFORM_MAINTENANCE.md): idle timing argument,
  S31 official sources/board matrix/UART milestone and housekeeping decisions.

The under100ms action-to-first-visible-response target remains a requirement,
not a newly measured result. The original160ms expression transition remains;
completion and first visible reaction are different latency metrics. No measured
power/thermal, RF/CCMP, pad timing, ADC calibration or S31 security claim is made.


## Changed source locations

The full PR diff is authoritative; these are the first changed line anchors in
each modified source/test/tool file (new files start at1). No original artwork
initializer or hardware wiring was changed.

| File | First changed line |
| --- | ---: |
| `firmware/PROT_OS7_MAX7219/Configuration.h` | 85 |
| `firmware/PROT_OS7_MAX7219/Control.ino` | 63 |
| `firmware/PROT_OS7_MAX7219/ControllerIdle.h` | 1 |
| `firmware/PROT_OS7_MAX7219/Icons.h` | 1 |
| `firmware/PROT_OS7_MAX7219/Microphone.ino` | 1 |
| `firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino` | 19 |
| `firmware/PROT_OS7_MAX7219/PawLink.ino` | 98 |
| `firmware/PROT_OS7_MAX7219/SensorHardware.ino` | 1 |
| `firmware/PROT_OS7_MAX7219/Settings.h` | 7 |
| `firmware/PROT_OS7_MAX7219/UsbControl.ino` | 8 |
| `firmware/PROT_OS7_MAX7219/UsbReply.h` | 1 |
| `firmware/PROT_OS7_MAX7219/VisorConnector.ino` | 4 |
| `firmware/PROT_OS7_MAX7219/src/HelmetProtocol/HelmetProtocol.h` | 47 |
| `firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp` | 18 |
| `firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.h` | 53 |
| `firmware/esp32/src/DeviceConfig.h` | 2 |
| `firmware/esp32/src/EspNowTransport.cpp` | 149 |
| `firmware/esp32/src/EspNowTransport.h` | 10 |
| `firmware/esp32/src/SessionHandshake.cpp` | 156 |
| `firmware/esp32/src/WirelessLink.cpp` | 183 |
| `firmware/esp32/src/WirelessTransport.h` | 4 |
| `scripts/build_support.py` | 32 |
| `scripts/check-mlkem-preflight.py` | 19 |
| `scripts/check-pq-worker.py` | 20 |
| `scripts/check-s31-preflight.py` | 121 |
| `scripts/check-v4.py` | 15 |
| `scripts/host_support.py` | 1 |
| `scripts/test-configuration-safety.sh` | 1 |
| `scripts/test-firmware.sh` | 57 |
| `scripts/tests/configuration-safety.cpp` | 1 |
| `scripts/tests/configuration.cpp` | 25 |
| `scripts/tests/controller-idle.cpp` | 1 |
| `scripts/tests/efficiency.cpp` | 51 |
| `scripts/tests/extensions.cpp` | 43 |
| `scripts/tests/firmware.cpp` | 161 |
| `scripts/tests/helmet-protocol.cpp` | 7 |
| `scripts/tests/matrix.cpp` | 29 |
| `scripts/tests/microphone-recovery.cpp` | 1 |
| `scripts/tests/sensor-recovery.cpp` | 1 |
| `scripts/tests/sketch.h` | 19 |
| `scripts/tests/stubs/ADC.h` | 8 |
| `scripts/tests/stubs/SPI.h` | 16 |
| `scripts/tests/test-board-targets.py` | 1 |
| `scripts/tests/test-helmet-tool.py` | 39 |
| `scripts/tests/test-host-support.py` | 1 |
| `scripts/tests/test-pq-worker.py` | 11 |
| `scripts/tests/test-tools.py` | 23 |
| `scripts/tests/v4/Checks.cpp` | 120 |
| `scripts/tests/wireless.cpp` | 639 |
| `tools/helmet/index.html` | 24 |


## Validation executed on the final source

All commands below completed successfully. Compiler invocations enforce
warnings as errors for application code. The known arduinoFFT dependency
absolute-value warning remains separately visible under its existing scoped
policy; no first-party warning or sanitizer was disabled.

| Validation | Result |
| --- | --- |
| `scripts/test-firmware.sh` | PASS: full native sketch/matrix/controls/audio/configuration suite, all 8 feature masks, authenticated UART rendering, 22 ADC/mic fault scenarios including stale completion, idle/backlog/wake tests, tooling and three brightness-ceiling variants; ASan+UBSan. |
| `scripts/test-configuration-safety.sh` (included above) | PASS: ceiling0/1/15,35 valid JSON replies each, long names/80fields/exact fit/canaries/negative format returns,65-field duplicate tracker; invalid ceiling-1/16/256 rejected at compile time. |
| `scripts/test-efficiency.sh` | PASS: visual cache, unchanged rendering, audio/control cadence, real worker code with all existing isolated candidate flags and combined OLED/deadline cases, storage readback and tooling. |
| Nine `scripts/tests/test-*.py` programs, both normal and `-O` | PASS,18 program invocations: board guards, efficiency analysis, Face Studio13 tests, host-build isolation, host portability, pairing, PQ checker, key/security build checks and tools. |
| `scripts/test-wireless.sh` | PASS: 1,000,000 protocol transitions;1,000,000 real authenticated AES-GCM records;52,000 corruption/fuzz cases;135,983 NVS-model storage faults;100,000 malformed authenticated UART streams; new Hello pacing/admission/recovery cases. ASan+UBSan on host and crypto dependencies. |
| `scripts/test-v4.sh` | PASS: real primitive/project vectors, pairing/rekey,135,983 storage faults,500,000 fragment cases,1,000,000 protocol events; ASan+UBSan. |
| `scripts/check-v4.py --skip-host` | PASS: 145 NIST ML-KEM-1024 cases, unchanged 42 pinned vendor files/license, upstream functional suite, independent vectors,8 application modules under Clang static analysis, both negative activation compile tests. Full host execution above ran separately. |
| `scripts/test-dtls.sh` | PASS: real DTLS cookies/handshake/loss/nonblocking writes/full-tag/replay/wrong-key/new-session isolation under sanitizers; retained historical suite. |
| `scripts/check-mlkem-preflight.py` | PASS: 145 NIST cases,1000 upstream functional iterations, sanitizers and static/resource preflight. Existing direct-main-task no-go verdict retained; not a production port. |
| `scripts/build.py` | PASS: current-source default Teensy export, zero warnings. |
| `scripts/check-features.py` | PASS: all 8 Teensy sensor combinations, zero warnings. |
| `scripts/check-extensions.py` | PASS: all 5 supported radio/connector/sensor combinations, zero warnings. |
| `scripts/check-uart-auth.py` | PASS: 3 actual authenticated Teensy builds with unique temporary test key and linked HMAC symbol checks, zero warnings. No test key committed. |
| PlatformIO MCP `project_envs`, fresh `build_project` for helmet/paw and `size_report` | PASS: both C3 production roles inspected/compiled; actual ELF identity verified for final size reports. |
| `scripts/build-esp32.py`, both UART modes | PASS: both roles with optional UART authentication and both ordinary defaults restored; zero warnings; expected SDK settings/partitions checked. Every final build manifest matches current source identity. |
| `scripts/check-s31-preflight.py --preview` | PASS: isolated S31 helmet/paw v3-source compiler probes and separate48KiB/PCT worker relinks, zero compiler warnings;5 upstream CMake dependency warnings per role recorded. Never a production/upload approval. |
| YAML/permissions/action checks, `git diff --check`, artwork comparison and bounded tracked-secret scan | PASS. No physical measurements. |

EEPROM tests simulate interruption at every byte budget through the complete
record write plus commit margin, require either the old or complete new valid
configuration, reject corruption and retain unchanged-save wear behavior. NVS
fault tests model atomic storage and135,983 corruption/interruption cases; real
flash power cuts remain unperformed.

Final local evidence is under ignored `build/audit-*.log`, source-bound build
manifests and `build/{s31-probe,pq-preflight,v4-validation}`. Earlier concurrent
builds were correctly rejected when sources changed during execution; they are
not substituted for the final successful current-source records. The first
PlatformIO size request raced an artifact replacement and returned
`ANALYSIS_ELF_MISMATCH`; a later stable-artifact retry passed. No mismatch was
ignored. The initial S31 setup failures (missing dependencies, Python trust store,
Linux-only linker flag) were resolved using the system CA store, official pinned
inputs/constraints and portable warning-clean flags, then the checks reran.

## Static memory and performance evidence

- Teensy default:74,748 bytes flash,14,368 bytes RAM1 variables. Full sensors +
  radio + connector:90,108 bytes flash,18,016 bytes RAM1 variables. With UART
  auth that full variant uses97,276 bytes flash and18,432 bytes RAM1 variables.
- C3 default PlatformIO accounting: helmet755,408/1,536,000 flash and
  35,052/327,680 RAM; paw681,092/1,536,000 flash and33,124/327,680 RAM.
  SDK linker filler/alias symbols are not additional RAM; raw symbol estimates
  must not be summed as runtime usage.
- S31 preview baseline app images:827,312 bytes helmet /742,448 paw; static
  data+BSS50,036 /48,120 bytes. Linker tail before runtime allocation366,784 /
  383,296 bytes. Separate worker candidate stack remains49,152 bytes; known
  native frame estimates do not constitute measured high-water or free heap.
- No measured current, CPU utilization percentage, stack high-water, battery
  improvement or physical response latency. CPU frequency remains600MHz,
  microphone timer4kHz, SPI4MHz. WFI and reduced replay response traffic avoid
  unnecessary work; their electrical savings are unquantified.

## Independent review and release gates

A fresh reviewer examined every changed production file and supporting tests,
CI/tooling and documentation. The sole actionable finding was the stale
completed-ADC conversion; its regression was observed failing before the fix,
then passing, followed by the whole green suite. No second actionable regression
was identified. This is a focused engineering/security regression review, not a
formal proof or exhaustive independent audit of all vendor cryptography.

Remaining release gates: assembled-helmet RSET/current/supply/wire measurements;
pilot/buffer pad timing and power-cycle/fault waveforms; ADC calibration/noise and
actual4kHz/wake/under100ms action-to-visible timing under load; physical flash
power-cut tests and runtime stack/heap/watchdog/RF/CCMP qualification; official
supported stable S31 SDK with reviewed memory protection and an approved module/
board pin map; original-sketch license permission and exact head-model license
version. The experimental v4 core remains excluded from production until its
separate integration/provisioning/hardware gates are satisfied.

# Final remediation and release readiness — 2026-10-17

Continues PR #1 on `codex/comprehensive-audit-remediation` from remediation
commit `3275ff85783ddd242caf29b2ff96322af4edcf12`. The current user brief is the
implementation specification. This follow-up implements CI portability, brightness
qualification and update behavior, response/idle timing, and isolated S31/v4
preparation, with full validation and a fresh independent review. Production
v4 activation, firmware uploads, hardware changes and merges remain prohibited.

## CI failure reproduced

[Failed remote run 39062915777](https://github.com/Fur-Byte/Protogen/actions/runs/39062915777),
job117484406514, exited1 at `scripts/test-configuration-safety.sh:54` with
`rg: command not found` after its three sanitizer configurations had passed.
The real script was also executed locally with a restricted PATH containing
only its declared prerequisites; it failed at the same command before the fix.
The runner now uses portable `grep -F -q`. A regression executes the entire
configuration sanitizer suite without ripgrep in PATH; it passed after the fix
and is included in CI. No sanitizer/assertion/warning policy was weakened.

The script audit found no second mandatory `rg` invocation. Optional packaged
Clang fallback paths are preceded by installed/explicit compiler discovery;
Linux-only stack experiments remain explicitly labeled. Further CI results and
final source-bound validation are recorded when executed below.

## Final issue dispositions

| Issue | Final classification | Change/evidence |
| --- | --- | --- |
| Missing ripgrep in Ubuntu CI | Resolved and verified | Portable grep, real restricted-PATH regression and actual green hosted run39077608392 on6a9e9ba; the final expanded commit is checked separately. |
| Other host portability assumptions | Resolved and verified for supported hosts | macOS/Linux OS entropy providers for qualification, Linux-only affinity explicitly reported, POSIX stack allocations rounded/reported by actual page size; actual macOS full worker qualification and hosted Linux compilation/tests. Node is an explicit host-test prerequisite; the critical Face Studio event regression no longer silently skips. |
| Old saved brightness requests activating after a ceiling increase | Resolved and verified | Explicit EEPROM format3 stores request and approved base separately. Read-only format1/2 loads authorize at most historical1. Same image saved under ceiling1 stays1 when compiledceiling15; only explicit brightness SET/profile reauthorizes. Atomic/interrupted writes remain covered. |
| Unrelated Face Studio edits reauthorizing brightness | Resolved and verified | Actual inline event regression; Apply omits brightness unless explicitly edited/selected in a profile. |
| MAX7219 current/wire/thermal qualification | Implemented but requiring physical hardware validation | Ceiling remains1. RSET/Vf/PWM/scan/pulse current, supply/drop/I²R/thermal calculation and stepwise measurement procedure are in RELEASE_BRIGHTNESS_QUALIFICATION.md. No safe measured maximum is invented. |
| Optional upstream INA validity/SOC faults | Resolved and verified in software; physical connection/calibration required | Conversion-ready/trim/overflow/range/I²C checks, fault/counters/recovery, valid-only measurements and fault-generation-bound FULL requests. ENERGY labels the upstream PD rail; it cannot measure individual5V display rails. |
| First response/rapid action scheduling | Resolved and verified as host/model regressions | Immediate status refresh, post-ACK UART cache fence, prompt Teensy status and Paw ordering/expected-state checks. Exact-body review reproduced and resolved stale Next and lost-ACK deadlock. |
| First-feedback rendering and long transitions | Implemented but requiring physical hardware validation | Dense2ms sampling only before first changed frame;60ms closing/reopening cap with longer closed hold;160/1000ms total durations retained. Final actual-sketch model max98.370ms; only1630us model margin, no physical guarantee. |
| C3 five-ms poll becoming zero ticks | Resolved and verified | Supported1000Hz configuration, compile guard and build settings check. Two-ms active polling and bounded immediate bridge passes retain responsiveness; frequencies unchanged and physical energy effect unmeasured. |
| WFI entry/backpressure/diagnostics | Implemented but requiring physical hardware validation | Context/mask/sleep/tick guards and runnable-TX policy verified with actual-sketch tests and real ARM compile/disassembly; IDLE counters and paired meter/optical procedure. No measured current/residency/wake claim. |
| S31 production SDK hardening | Blocked by external dependency | Official latestv6.1 remains S31 preview; supportedminimumv6.1.1 ref404; tagged PMP/PMA gap persists. No master/preview substitution as production. |
| S31 board/UART preparation | Implemented but requiring physical hardware validation | Rejected-by-default explicit profiles, guarded uncalled UART adapter, authenticated bounded partial-I/O service, host suite and isolated target relink. No approved pins or actual board communication claimed. |
| V4 full integration preparation | Implemented but requiring physical hardware validation and external SDK | Real core→authenticated UART gate integration/rekey/failure/replay/epoch tests; unlinked S31 ABI/frame/memory analysis. Algorithms/key hierarchy unchanged; both production activation barriers remain. |
| Physical runtime/RF/flash power-cut/provisioning/update qualification | Implemented procedures; hardware/external dependency gates | See RELEASE_S31_V4_READINESS.md, PRODUCTION_PROVISIONING.md and existing hardware gate reports. No flash/eFuse/radio/hardware actions performed. |
| Original sketch permission/exact model license version | Blocked by external dependency | Existing attribution/notices retained. Author/source confirmation remains required; no permission invented. |
| Unresolved confirmed software defects | None identified after final fixes/review | This is not a formal proof or exhaustive cryptographic audit of every vendor line. Remaining hardware/SDK gates above remain release-blocking. |

## Fresh independent regression/security review

A fresh agent reviewed the entire PR1 diff and new/current source, including
memory/integer boundaries, interrupt/concurrency/state ordering, sensor faults,
storage migration, UART/radio authority, power/tick changes, isolated research
components and CI/tooling. It independently ran sanitizer callback reproductions,
command/status tests, both latency modes/durations, portability/board/tick and
actual Face Studio event tests. No hardware or repository edits were performed.

It found two P2 issues in intermediate latency work: cached pre-command status
repeated consecutive Next targets, and the first fence fix could wait forever
after a lost UART ACK. Both were reproduced with exact production function
bodies under ASan/UBSan. Identical retries now pass; the original300ms fence
expires without extension. The independent final reproduction confirmed two
retries forwarded and recovered cache, and the reviewer reported no further
confirmed actionable regression. Review limitations include physical timing,
electrical/RF behavior and vendor cryptographic proof.

## Additional confirmed issues resolved

- INA FULL could bypass an implausible accumulator delta or retain a request
  accepted from a stale published snapshot. Fault-generation binding and
  valid-only counter processing now require a new healthy owner request.
- Direct helper invocation during local final validation exposed a missing
  executable-mode assumption; new/config runners have executable
  modes and the real final hosted workflow verifies repository checkout modes.
- macOS qualification used Linux getrandom/affinity and4KiB-page assumptions.
  OS-backed randomness remains cryptographic; macOS is explicitly unpinned,
  and requested/allocated host stack sizes are separately reported. This does
  not qualify MCU scheduler service or lower the ML-KEM parameter set.
- New timing constants are range-checked before narrowing; existing settings
  mutation tests caught missing0/UINTMAX rejection and now pass.

## Reporting and measurement references

- RELEASE_BRIGHTNESS_QUALIFICATION.md: electrical math, actual unknowns,
  explicit format3 layout/rollback/authorization policy and energy API.
- RELEASE_LATENCY_QUALIFICATION.md: complete pipeline, modeled versus physical
  timings, source scheduling corrections and exact shared-timebase procedure.
- RELEASE_POWER_QUALIFICATION.md: interrupt guards, diagnostics and paired
  current/energy/optical capture matrix. No CPU frequency scaling activated.
- RELEASE_S31_V4_READINESS.md: official SDK evidence, new isolated implementation,
  ABI/static stack observations and both-role provisioning/hardware gates.

Final command exits/source identities, size comparison, hosted run links and
final commit are recorded in the validation section and PR1 body. The final
status refers to the exact published head, not the earlier green CI-fix commit.


The final pipeline audit also reproduced valid encrypted burst records being
removed during authentication cooldown. Retaining queued records before the
same5ms budget check fixed that loss without increasing cryptographic admission.
`ServicePollMs=2` and bounded immediate bridge/command passes avoid needless
scheduler periods. This refines the model's assumed upstream budget; it is not
an end-to-end hardware measurement. The final authenticated modeled maximum is
98.370ms with1630us margin; external optical timing remains mandatory.


## Validation record

The local final application/build manifests have source identity
`b2adab59dbcafc313d20f5e9a3e8031ad622dab74d45fc0a62b047276713f25b`.
This hashes firmware/scripts/tools/profiles/lock inputs, separately from Git
commit metadata. Ignored `build/release-final-*.log` files and standard build
manifests retain local evidence; hosted CI uploads its own build evidence.

| Validation | Result and scope |
| --- | --- |
| `test-firmware.sh`, `test-configuration-safety.sh`, `test-efficiency.sh` | PASS, strict warnings and ASan/UBSan; actual sketch, migration, ADC, INA, WFI guards and backpressure |
| `test-latency.sh` | PASS, CRC/authenticated UART, 160/1000ms transitions, normal/wake/sensors/heavy ingress; final modeled maxima 92.628/98.370ms |
| `test-s31-platform.sh` | PASS, host profile/authentication/partial-I/O/fault suite; no board I/O |
| `test-wireless.sh` | PASS, full default suite including million-record and million-event corpora, 52,000 corrupt/fuzz cases, 135,983 storage/schema/power-loss model cases and 100,000 malformed UART streams |
| `test-v4.sh`, `test-dtls.sh` | PASS, real cryptographic core plus authenticated-UART integration; real Mbed TLS DTLS and fault/replay/recovery suite |
| `check-v4.py`, `check-mlkem-preflight.py` | PASS, full host tests, 145 NIST vectors, 42 upstream functional outputs, independent vectors, static analysis and production negative-compilation guards; direct-main KEM remains NO_GO |
| `check-pq-worker.py` | PASS as isolated qualification tooling on macOS; actual OS entropy, 2571 operations, 1,000,000 worker transitions and 12,000 admission rejections; no single-core or target timing claim |
| All 11 top-level `scripts/tests/test-*.py` programs | PASS in normal and `python3 -O` modes, including mandatory actual Face Studio JS regression and PATH without ripgrep |
| `build.py`, `check-features.py`, `check-extensions.py`, `check-uart-auth.py` | PASS, pinned actual Teensy compiler, all requested combinations, zero warnings |
| `build-esp32.py` | PASS, both C3 roles, default and temporary-key authenticated variants, defaults restored; zero warnings and resolved SDK settings verified |
| PlatformIO MCP | Both default roles explicitly rebuilt successfully; environment configuration and PlatformIO memory accounting inspected |
| `check-s31-preflight.py --preview`, `check-s31-uart.py --preview`, `check-v4-resources.py` | PASS as isolated preview/compiler/resource checks; pinned SDK, unchanged production, default activation rejection, uncalled UART relink and unlinked ABI/frame estimates |
| GitHub Actions | Original failure39062915777 reproduced; interim portability run39077608392 passed. Final exact-head run and SHA are recorded in the existing PR1 validation record after publication. Local success is not the hosted completion gate. |

## Flash and static memory comparison

Comparison is against remediation commit `3275ff8`, using the same pinned
compiler/SDK and corresponding feature sets. Bytes below are linker/build
accounting, not measured peak runtime stack or heap. C3 flash figures are
PlatformIO program-size accounting rather than `.bin` file length.

| Build | Before flash | After flash | Delta | Before static RAM | After static RAM | Delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Teensy default | 74,748 | 76,796 | +2,048 | 14,368 | 15,456 | +1,088 |
| Teensy all sensors + wireless + connector | 90,108 | 92,156 | +2,048 | 18,016 | 18,080 | +64 |
| Same Teensy with authenticated UART | 97,276 | 100,348 | +3,072 | 18,432 | 19,520 | +1,088 |
| C3 helmet default | 755,408 | 757,262 | +1,854 | 35,052 | 35,132 | +80 |
| C3 paw default | 681,092 | 682,260 | +1,168 | 33,124 | 33,148 | +24 |

The Teensy default increase was inspected in actual ELF sections/symbols:
`.data` +1024B, `.bss` +64B, `.text.itcm` +960B and `.text.csf` +64B.
Named mutable additions include bounded response/idle counters and approval
state; no unbounded allocation was introduced. New LATENCY/IDLE format strings
were verified in mapped flash. Default RAM1 local-variable space remains
443,296B; the fullest authenticated build retains406,464B. C3 maximum static
RAM is10.7% of327,680B and maximum default program size49.3% of1,536,000B.
The bounded additions are acceptable for these builds; runtime S31 heap/stack
qualification remains outstanding.

## Source map for follow-up fixes

- `scripts/test-configuration-safety.sh:67` and
  `scripts/tests/test-runner-portability.py`: portable runner and restricted PATH.
- `Configuration.h:18`, `ConfigStore.h:51`/`:75`,
  `tools/helmet/index.html:65`: approved-base enforcement, explicit record3 and
  explicit-only user brightness reauthorization. The brightness report provides
  the complete MAX7219 call-path map; central `Settings.h:18` remains1.
- `WearerIO.cpp:287`/`:294`/`:411` and `WearerIO.h`: fresh-conversion diagnostics,
  valid-only energy and fault-generation binding.
- `ControllerIdle.h`, `ResponseTiming.h`, `PROT_OS7_MAX7219.ino`,
  `PawLink.ino` and `UsbControl.ino`: guarded WFI, runnable serial policy,
  first-visible tracking/early sampling and bounded IDLE/LATENCY diagnostics.
- `ApplicationStatus.h:20`, `WirelessLink.cpp`, `main.cpp:396` and
  `WirelessConfig.h:15`: post-ACK fence/retry expiry, retained RX during existing
  authentication cooldown, immediate status refresh and representable2ms polling.
- `firmware/esp32/platform_research/` and `scripts/tests/v4/UartIntegration.cpp`:
  rejected-by-default S31 preparation and full-core/UART integration regression.

These short source names refer to the Teensy sketch or ESP32 `src` directory
unless a repository-relative path is supplied. The qualification reports supply
full paths and the exact external measurement procedures. No physical power
saving is asserted: measured current, RF, electrical/thermal behavior, timing
and WFI residency remain unmeasured. CPU clocks remain600MHz/160MHz.

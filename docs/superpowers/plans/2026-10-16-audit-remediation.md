# Comprehensive audit remediation implementation plan

> **For agentic workers:** Use Superpowers debugging and test-driven development for each independently confirmed defect; parallel agents own disjoint subsystems. The user explicitly requests implementation through validation and a pull request without merging.

**Goal:** Address every finding in the supplied remediation brief while preserving Teensy 4.0, the original panel layout, C3/v3, 4 kHz audio, authenticated protocols, storage formats and the under-100-ms response target.

**Architecture:** Bound fixes at existing interfaces: MAX7219 register writes, checked USB response construction, ADC acquisition lifecycle, SPI/pilot restoration, and protocol admission. Keep S31 research and v4 separate from production. Hardware measurements and supported SDK gaps remain explicit gates.

**Tech stack:** Teensy Arduino 1.62, ESP-IDF 6.1.0 / PlatformIO 6.1.19, C++17, Python, Clang ASan/UBSan.

**Spec:** User-supplied Comprehensive Audit Remediation brief (2026-10-16); preserved requirements summarized above.

## Global constraints

No security downgrade, new hardware requirement, silent pin/protocol/storage change, production v4 activation, upload, eFuse action or automatic merge. Treat compilation and simulation as software evidence only.

## Review focus

Oversize COBS inputs and output capacity; serial truncation and duplicate fields beyond bit31; stale ADC samples after recovery; pilot input after SPI reinitialization; replay traffic during online and reconnect states. Tests belong to their owning task.

## Tasks

- [x] Configuration safety: reproduce unsafe snprintf accounting and bit shifts, add bounded serializer and scalable duplicate set; cap every intensity register write using Settings::MaxBrightness=1; preserve requested brightness and report effective ceiling. Test actual sketch, EEPROM reload, BOOST, reconnect, JSON truncation, duplicate keys and Face Studio.
- [x] Sensor/connector: reproduce ADC failure latch and SPI pin12 mux overwrite; bounded independent ADC retries/backoff and counters, invalidation/fresh voltage on dock stages; restore pilot after SPI.begin with buffers off. Fault injection, rollover, attach/detach and microphone timing regressions.
- [x] Wireless: reproduce COBS overflow with 254-byte run, then test all specified boundaries and randomized long roundtrips. Verify malicious Hello behavior in Link and Handshake; fix only confirmed interruptions. Review all v4 properties, keep production guard, run crypto/state/fragment tests.
- [x] Idle: prove hidden loops spin; add interrupt-aware wait only when hidden and serial queues drained, preserving core SysTick and peripherals. Test polling/wake, UART backlog and active audio; expose elapsed-idle diagnostics without fabricated power savings. Do not change CPU clock without physical evidence.
- [x] Maintenance: least-privilege pinned CI actions, ignore private UART key headers with fixture exception, report secret scan without values, preserve upstream licenses/attribution, normalize icon newlines only after artwork equivalence, retain historical evidence and DTLS tests if still referenced.
- [x] S31: verify official release/support and pinned memory-protection source, run isolated preview/preflight where dependencies allow, audit board-specific pins and document a safe build-only UART milestone without inventing wiring or activating preview production.
- [x] Integration: rerun full host/sanitizer/Python/build matrices, C3 roles via PlatformIO MCP, auth fixtures and interrupted storage simulations. Record exact commands/results and hardware/SDK blockers in final report. Review diff, commit dedicated branch, push and open/attach reviewable PR; never merge.


## Execution record

The dedicated branch was used in the existing clean checkout as explicitly
requested. Three independent implementation domains ran concurrently and one
fresh reviewer checked the complete combined patch. The reviewer confirmed one
additional stale-completed-ADC result bug; its actual-sketch regression failed
before the fix and passed afterward. Full suites and target builds were rerun.
Hardware power, pad/RF/timing, unsupported stable S31 integration and original
sketch license permission remain gates, documented rather than claimed complete.
No production v4 activation, protocol/storage-format change, upload or merge.

# ESP32-S31 SDK and ML-KEM-1024 preflight

Assessment date: 2026-10-06. Production source baseline:
`6e87f521958f4a6ba40a9a8218a9f152f7c4e34f`.

**Intended final controller: ESP32-S31, for both helmet and Paw.** Teensy 4.0
remains the rendering/behavior controller. C3 is retained for legacy v3
development/regression. The delivered firmware is still C3/v3. Neither an
active production retarget nor S31 platform activation is complete. The
isolated [v4 core](WIRELESS_V4_PROTOCOL.md) is now implemented and host-tested;
that work does not change this SDK gate.

**S31 PRODUCTION INTEGRATION STATUS: BLOCKED — supported-stable SDK and memory-protection gap.**
**HARDWARE QUALIFICATION STATUS: PENDING PHYSICAL ESP32-S31 HARDWARE.**

The software blocker is independent of missing boards and independent of the
previous C3 stack gate. S31 has promising static memory margin for ML-KEM-1024.
Do not interpret the C3 qualification verdict as an S31 resource rejection.

## Why the production migration stops here

The [official S31 FAQ](https://esp32-s31.espressif.com/en/faq) identifies
v6.1.1 onward as the officially supported release tags. The current
[v6.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1) explicitly
offers S31 **preview** support. GitHub's release and tag APIs were checked:
v6.1 is the latest release and `refs/tags/v6.1.1` returns HTTP 404. This does not
mean S31 is nonexistent or cannot compile.

More specifically, the pinned public v6.1 implementation of
[`esp_cpu_configure_region_protection()`](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_hw_support/port/esp32s31/cpu_region_protect.c)
has the unresolved IDF-15238 TODO. Its body configures a permissive external-RAM
PMA window; it does not implement the expected PMP memory-region policy.
The generated configurations nevertheless enable `ESP_SYSTEM_MEMPROT`,
`ESP_SYSTEM_MEMPROT_PMP`, and `BOOTLOADER_REGION_PROTECTION_ENABLE`.
The [bootloader calls this routine](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/bootloader_support/src/bootloader_mem.c).
That source also temporarily opens HP/LP peripheral permission registers for
all security modes under IDF-14620. `SOC_APM_SUPPORTED` is commented out.
Configuration flags and successful linking therefore cannot establish that the
existing platform hardening has been preserved. This is a source-level SDK
gap, not a demonstrated radio exploit or a claim that every hardware protection
is absent. ROM behavior has not been physically tested.

Public master at `4d59230ddff16327812782151ef0afef202dc6d7` contains a much fuller
[S31 PMP implementation](https://github.com/espressif/esp-idf/blob/4d59230ddff16327812782151ef0afef202dc6d7/components/esp_hw_support/port/esp32s31/cpu_region_protect.c),
including instruction/data splits. The inspected file's SHA-256 is
`92f4643c96c7dc34a8f05af16ff3a4eb07f39f8e988181f0bd0971a1e7030b59`.
It was read for comparison, not copied into the SDK or selected as a production
dependency. Switching to an unreleased master snapshot or adding a local PMP
implementation would not satisfy the requested supported-stable baseline.

Resume the migration when an official supported stable tag is published and
the security-relevant implementations have been reviewed. A relaxed preview
SDK policy alone would not resolve the pinned v6.1 hardening gap.

## Completed build-only work

`python3 scripts/check-s31-preflight.py --preview` creates an isolated CMake
project under ignored `build/s31-probe/`. It compiles the **unchanged production
v3 sources** for `esp32s31`, once per role, then links separate uncalled PQ
qualification workers. It does not edit production firmware, toolchain locks,
PlatformIO environments, storage schemas, or protocol behavior. It contains no
upload/eFuse commands. All ELF/bin/map/object/log output stays outside Git.

This is compiler/resource research, not a board port: existing C3 pin constants
remain in those images. They must never be flashed as S31 board definitions.
Paw's isolated research configuration uses the shared UART console defaults;
its future board-specific USB selection remains unimplemented.

The current PlatformIO 7.1.3 platform's RISC-V builder selects only C3/C6. It
cannot select S31 by changing a board name. The research tool uses the official
ESP-IDF CMake build directly; it does not patch PlatformIO or depend on private
components. A future production S31 build should use that official build path
with a supported SDK and a reviewed board definition, while legacy C3 builds
continue separately through the existing PlatformIO tool.

| Build assumption | Evaluated value |
| --- | --- |
| ESP-IDF | 6.1.0 / official tag v6.1, S31 preview |
| Upstream release commit | `fff9895c82d744c7237be8847347bdd1b07c6643` |
| Installed framework package | `framework-espidf` 4.60100.0 |
| Compiler | GCC 15.2.0, `esp-15.2.0_20251204` |
| Target | `esp32s31` |
| ABI / ISA | `ilp32f`; `rv32imafc_zicsr_zifencei_zaamo_zalrsc_xesploop_xespv` |
| CPU configuration | 320 MHz, dual HP cores; no measured operation timing |
| External RAM | Disabled; no PSRAM used in these static results |
| Flash assumption | 4 MiB, existing 1,536,000-byte factory app partition |
| Main task | Existing configured 12,288 bytes, unchanged |
| Development security | Recoverable; ordinary NVS; boot/flash encryption disabled |

Seven critical SDK source files were compared byte-for-byte with the official
release commit and pinned in `scripts/tests/s31-preflight-pins.json`. This is
not a claim that every SDK file or proprietary Wi-Fi archive has been compared
with a Git blob. The report hashes built artifacts and every tracked firmware
input; the script checks firmware hashes before/after its run.

Both role builds and worker relinks passed with **zero compiler warnings** and
application `-Wall -Wextra -Werror`. Each fresh configuration emitted **five
upstream CMake dependency-validation warnings** about private Wi-Fi/WPA include
directories. These are recorded, not suppressed. Consequently there is no
claim of a zero-warning, supported S31 v3 production checkpoint.

## S31 stack qualification

Backend: **mlkem-native v2.0.0**, final FIPS 203 ML-KEM-1024, portable C/FIPS 202,
no native assembly, pairwise consistency testing (PCT) enabled. Upstream commit
`d1b2fe782888bdb761a50336012923180be7f502`; MIT license selected from the
upstream offered licenses. Archive SHA-256:
`7c7a10464ba3c62d5657a70da495539ab7f28e464cff80eb9d8173e2bc91c4d3`.
The existing pin manifest verifies 41 source files. No cryptographic internals
were changed. No ML-KEM-768 substitution was made.

The actual S31 compiler/ABI flags and `-Og -fno-shrink-wrap` were used with GCC
`-fstack-usage -fcallgraph-info=su`. These are **compiler estimates**, not
runtime stack high-water measurements.

| Path / frame | Bytes |
| --- | ---: |
| Largest individual frame: `pqqual_indcpa_enc` (`mlk_indcpa_enc` upstream) | 19,056 |
| KeyGen including PCT: known nested native frames | 26,320 |
| Encaps: known nested native frames | 22,992 |
| Decaps: known nested native frames | 24,496 |
| Worker loop frame | 80 |
| Backend adapter frame | 16 |
| Known worker + adapter + maximum native path | 26,416 |
| Recommended **static candidate** stack | **49,152 (48 KiB)** |
| Space above known maximum | 22,736 |
| Planning allowance for unaccounted platform/spills | 6,144 |
| Remaining planned margin after that allowance | 16,592 |

The allowance is a conservative engineering reserve, not a measured upper bound
on RTOS/interrupt/RNG frames. Call graphs omit external/platform frames; other
branches such as entropy acquisition must be qualified too. A 24 KiB worker
fails the known KeyGen path. A 32 KiB worker leaves only 6,352 bytes above the
known worker path before platform allowance. The larger 48 KiB candidate is
cheap enough in the evaluated S31 link budget to prefer comfortable headroom.
No unrelated task stack was enlarged. The normal ONLINE path would retain only
the bounded worker allocation and perform no KEM operation per button press.

The largest frame is dominated by an 8,192-byte public matrix, four polynomial
vectors totalling 8,192 bytes (including secret encryption/noise intermediates),
three 512-byte polynomials, a 1,024-byte secret multiplication cache and seed
storage. See the prior [worker analysis](WIRELESS_PQ_WORKER_QUALIFICATION.md)
for exact ownership, lower-stack allocator API and alternative-backend analysis.
The supported allocator interface is experimental in this version; it was not
selected for this S31 default-stack qualification. Smaller stack alone does not
justify changing the reviewed backend or introducing GPL code.

## Linked memory evidence

Physical HP SRAM is 524,288 bytes; LP SRAM is a separate 32,768-byte region.
The actual evaluated application SRAM region is
`0x2f000000..0x2f07afc0`, **503,744 bytes**. IRAM and data share this region;
do not count the remaining physical SRAM, LP memory, or instruction/data aliases
twice. No external-memory capacity is included.

| Static compiler/linker observation | Helmet | Paw |
| --- | ---: | ---: |
| v3 baseline application image bytes | 823,776 | 739,824 |
| Factory partition headroom bytes | 712,224 | 796,176 |
| Baseline `.dram0.data` bytes | 19,332 | 19,000 |
| Baseline `.dram0.bss` bytes | 30,576 | 28,992 |
| Baseline data + BSS bytes | 49,908 | 47,992 |
| Baseline `.iram0.text` bytes | 86,794 | 72,298 |
| Contiguous linker tail before runtime allocations | 366,912 | 383,424 |
| Data + BSS with 48 KiB worker and ML-KEM relink | 105,876 | 103,960 |
| Net linked static RAM increase | 55,968 | 55,968 |
| Linker tail with worker, before runtime allocations | **310,944** | **327,456** |
| Increment in selected loadable code/data sections | 24,444 | 24,420 |

The last row sums `.flash.text`, `.flash.rodata`, `.iram0.text` and `.dram0.data`
changes. It is not a generated v4 binary-size increase: these are separate
uncalled research ELF relinks. Baseline files were not overwritten. Runtime
radio/driver/RTOS/libc allocations and future v4 objects are **not** included in
the linker-tail number; it is not free heap after boot. Existing global wireless
objects are included in baseline BSS. The v3 Link object is 2,664 bytes on this
ABI. A complete v4 Link/Handshake size has not been measured because v4 does
not exist yet.

Fixed worker input/output payload is 6,336 bytes (pk 1,568 + sk 3,168 + ct 1,568
+ shared secret 32). The linked Worker object is 6,400 bytes. Qualification task
TCB is 100 bytes, queue and mutex control blocks are 84 bytes each, queue storage
is one 16-byte token, and numeric-only measurement storage is 96 bytes. Alignment,
initializer placement and linker retention are reflected in the actual net
delta; summing object-file BSS/data independently need not reproduce it.

Existing configured stacks: main 12,288, WearerIO 6,144, event 2,304, timer 3,584,
IPC 1,024, each idle 1,536, each ISR 1,536, FreeRTOS timer 2,048, configured lwIP
3,072 bytes. These settings are not measurements of allocated/live tasks.
The project creates no IP service; driver task/buffer allocations, main-stack
platform additions and all startup allocations still require an actual boot.

## Worker architecture and secret lifetime

The research relink reuses the isolated qualification worker, not a new radio
implementation. One fixed slot serves requests/results; one static queue entry
carries an internal operation token. Only KeyGen, Encaps and Decaps are legal.
Exact fixed lengths are checked before copying; no allocation size comes from
radio input. A completed unclaimed result occupies the slot and expires.
Separate operation budgets refill automatically. The adapter executes outside
the mutex and critical sections, with a blocked tick between operations.

Cancellation advances a 64-bit generation immediately. A running operation is
retired without concurrently wiping memory the primitive is still using;
completion discards and wipes it. A stale generation/ticket cannot install
anything into a later candidate. No callback dispatches application commands.
Future production ingress must supply structural, state, authentication and
rate-budget admission; the model's admission boolean alone is not authentication.

On failure, timeout, abandoned result, queued cancellation or stale completion,
the whole slot is securely wiped. Success retains only required outputs until
bounded consumption, then wipes the slot. The decapsulation request's private
key is wiped on completion. The consuming handshake must independently destroy
its transferred private/shared outputs; that protocol integration is pending.
The default upstream stack cleanup is reused. Volatile zeroing and compiler
barriers do not prove erasure of registers, spills, caches or all physical copies.
Do not delete a task while the primitive uses its stack. No secret logs exist.

S31's dual HP cores permit a future worker pinned away from the wireless/UART
service task, but no core-placement/scheduler claim has been measured or
implemented in production. The current uncalled probe uses the FreeRTOS static
task API at priority 1 without pinning. No yield is inserted inside ML-KEM.

## Validation actually performed

Fresh results at this assessment:

- Both unchanged v3 roles compiled/linked for the public S31 preview target;
  both separate 48 KiB/PCT-enabled worker ELF relinks passed.
- Legacy C3 helmet and Paw builds passed through the existing pinned production
  build tool, with zero warnings. Active v3 sources/settings remain unchanged.
- All 145 authoritative NIST ML-KEM-1024 cases passed under ASan/UBSan, including
  the PCT-enabled host build. Vector provenance remains ACVP-Server
  v1.1.0.43, commit `975de31eb83d87039ec88934fdc47d8c312b892d`.
- Upstream functional test: 1,000 iterations. SHA-256 verification of the backend
  archive and 41 sources passed; unchanged MIT selection.
- Existing v3 host regression: 1,000,000 protocol transitions, 1,000,000 real
  PSA AES-GCM records, 52,000 packet corruption cases, 135,983 storage faults,
  and 100,000 malformed UART streams, with ASan/UBSan and warnings as errors.
- Clang 19.1.7 Static Analyzer: 10 wireless/storage/UART source modules, no
  diagnostics; ML-KEM portable implementation analyzer, no diagnostics.
- Existing tooling tests passed (12 test methods, including their subcases).
- Re-executed the unchanged sanitizer-instrumented worker qualification binary:
  2,571 real KEM operations, 1,000,000 worker-model transitions, 12,000 admission
  rejects and 20 single-CPU threaded operations. Simulated UART service maximum
  gap was 3.525 ms on Linux, not S31. The allocator test passed 77 injected
  allocation failures and 1,000 complete/modified-ciphertext round trips under
  ASan/UBSan. That experimental allocator remains comparison-only.

Full production S31/UART-auth/Teensy build matrix, v4 cryptographic vectors,
hybrid-combiner tests, fragment fuzzing, v4 state fuzzing, v4 storage faults and
the final v4 adversarial audit are **not completed**. Existing historical C3,
Teensy, worker and alternative-backend evidence is retained in the earlier
reports; no past result is labelled as a fresh S31 measurement. There is no
v4 implementation to audit, no selected production combiner, no v4 packet
format and no v4 persistent record. These remain the requested next phase once
the SDK blocker is resolved.

The fresh host-only non-PCT portable-C benchmark averaged 0.094 ms KeyGen,
0.099 ms Encaps and 0.123 ms Decaps, across 64 iterations, including FFI.
This is **not S31 timing**, does not estimate 320 MHz execution, and omits the
PCT cost included in the target configuration. Real worker scheduling, idle
watchdogs, radio, UART and local-control latency have not run on S31.

## Future production and board gates

The [preliminary datasheet](https://documentation.espressif.com/esp32-s31_datasheet_en.html)
and [official feature status](https://developer.espressif.com/hardware/esp32s31/)
describe useful security hardware. The pinned SDK capability headers advertise
AES-128/256 (including GCM), SHA, HMAC, ECC including constant-time multiplication
and P-384, ECDSA/RSA signature peripherals, RNG, HUK/Key Manager, flash encryption
and PSRAM encryption. Secure Boot v2 **RSA** is enabled as a capability while
ECC boot is not. Hardware capabilities are not proof of configured protection.
Secure Boot stays classical; future hybrid radio protection does not make the
firmware trust chain or physical Teensy security post-quantum.

Secure debug, power-glitch mitigation, peripheral/memory isolation, encrypted
storage, anti-rollback and signed-update policy require supported SDK APIs and
review against the actual module/revision. The current feature-status snapshot
also lists ADC calibration as unsupported (IDF-14742); the existing ADC path
fails safely without calibration. Never replace calibration with guessed
voltage conversions to force an apparent port.

The future board definition must centralize module, antenna, flash size/mode,
PSRAM presence/encryption, UART/USB/pairing LED/sensor/button GPIOs, power modes,
and strapping restrictions. No S31 GPIO assignments are approved by this report.
Pairing storage must keep the existing fail-closed protocol-facing interface,
with recoverable development storage and separately provisioned production
encrypted/hardware-backed storage. No automatic production-key creation, eFuse
burn, debug lockdown, or release flash-encryption transition is authorized.

After resolving the software SDK blocker, implement and validate v4 before
hardware purchase. A sound, fully tested v4 may then be labelled **SOFTWARE
COMPLETE / HARDWARE QUALIFICATION PENDING**. Missing physical measurements alone
must not stop that software phase. Before wearing or production use, obtain:

- Per-operation actual stack high-water, pre/post-creation internal free heap,
  minimum heap and largest free block under complete wireless load.
- KeyGen/PCT, Encaps, Decaps and total reconnect CPU/elapsed times, queue latency,
  watchdog/idle progress, UART and radio service latency under attack and rekey.
- Real ESP-NOW CCMP, temporary MAC changes, channel recovery, packet loss/reboots,
  storage power cuts, provisioning/secure boot/updates/rollback behavior.
- RF/antenna behavior near the wearer, current draw, thermal and power behavior.

## Evidence and changed-file manifest

Static artifacts and hashes are captured in
[`validation/wireless-s31-preflight.json`](validation/wireless-s31-preflight.json).
Reproduction requires the pinned installed SDK/toolchain, its core Python
dependencies constrained by Espressif's `espidf.constraints.v6.1.txt`, and the
existing hash-verified ML-KEM preflight downloads. Run the host preflight first,
then `python3 scripts/check-s31-preflight.py --preview`. The latter verifies
source pins and does not download or silently select another SDK. Generated
images are research artifacts only, never upload candidates.

This qualification change creates this report, that JSON evidence snapshot,
`scripts/check-s31-preflight.py`, and `scripts/tests/s31-preflight-pins.json`.
It adds status references to README, SECURITY, ENGINEERING_PLAN,
WIRELESS_PROTOCOL, WIRELESS_IMPLEMENTATION, WIRELESS_VALIDATION,
PRODUCTION_PROVISIONING, WIRELESS_V4_PREFLIGHT,
WIRELESS_PQ_WORKER_QUALIFICATION and firmware/esp32/README.
No production source, active sdkconfig, partition table, toolchain lock, Teensy
code, protocol test scale, dependency license, or pairing record changed.

The two requested migration/v4 milestones cannot honestly be committed as
completed work. Only this completed research/qualification is eligible for a
research commit; no incomplete Protocol v4 implementation is included.

# Experimental v4 software validation and final review

**EXPERIMENTAL / HARDWARE-INDEPENDENT / NOT ACTIVE PRODUCTION FIRMWARE.**
Assessment date: 2026-10-06. Unchanged production baseline:
`8e6c0301b1bdbb231c3fb11083d976818fd3ecf0`.
Full changed/created file manifest and source hashes are in
[wireless-v4-core.json](validation/wireless-v4-core.json).

## Software result

The real `Core`, traffic codec, transcript/KDF, storage codec, worker and pinned
ML-KEM run together as Paw and Helmet in the host simulator. Tests do not replace
the protocol with a simpler state model. Synthetic deterministic entropy is
confined to tests; the reference host adapter uses PSA secure randomness.

| Validation | Result / retained scale |
|---|---|
| Exact vendored ML-KEM-1024, PCT enabled | **145** NIST cases: 25 KeyGen, 50 Encaps, 30 Decaps, 20 public-key checks, 20 private-key checks |
| Upstream functional harness | **1,000** iterations; authoritative outputs unchanged |
| Existing worker qualification rerun | 2,571 real operations, 1,000,000 lifecycle transitions, 20 threaded operations, 12,000 admission rejects |
| Existing upstream arena failure research | **77** allocation failures; wiped LIFO cleanup; 1,000 round trips + 1,000 implicit rejection cases |
| New v4 fragment suite | **500,000** cases; deterministic seed `a43099813579` |
| New v4 interrupted-write/corruption suite | **135,983** cases, including role mismatch |
| New v4 protocol suite | **1,000,000** event transitions; seed `713eddcca019`; 148 distinct installations including targeted setup |
| Release-mode core, assertions disabled | same full v4 suites pass with test assertions enabled |
| ASan + UBSan | no diagnostic in new core/native implementation and test harness |
| Clang Static Analyzer | no diagnostic in 8 new translation units, including full native SCU; existing v3 10-module analyzer also passes |
| Warnings as errors | new host/static-target sources and existing firmware builds pass |
| Legacy v3 event corpus | **1,000,000** transitions, four retained seeds |
| Legacy AES-256-GCM limit suite | **1,000,000** authenticated records; same-key reset rejected |
| Legacy radio corruption suite | **52,000** cases |
| Legacy storage fault suite | **135,983** cases |
| Legacy malformed UART suite | **100,000** streams; actual Teensy malformed UART/local fallback tests retained |
| C3 firmware | Helmet + Paw, default + authenticated UART, zero warnings |
| S31 preview qualification | unchanged v3 Helmet + Paw and uncalled worker relinks pass; no production activation |
| Teensy firmware | default, all 8 feature combinations, 5 extension combinations, 3 UART-auth combinations: **17** successful builds, zero warnings |
| Existing host/tooling | full firmware sanitizer tests, pixel/archive checks and tooling tests pass |
| Secret scan | detect-secrets 1.5.0: 318 candidates reviewed; public hashes, labels/paths and synthetic vectors only; no credentials or production private keys |
| Vendor/hash/license/isolation | 42 unchanged upstream files verified; MIT selected; production firmware/lock diff empty; both activation negative-compile tests pass |

The 145 vectors come from NIST ACVP-Server **v1.1.0.43**, commit
`975de31eb83d87039ec88934fdc47d8c312b892d`, directories
`ML-KEM-keyGen-FIPS203`, `ML-KEM-encapDecap-FIPS203`, and
`ML-KEM-encapDecap-FIPS203-tr1`. All six prompt/expected JSON source hashes remain
pinned in `scripts/tests/mlkem-preflight-pins.json`. No final-FIPS validation
case is skipped. Invalid lengths are checked by the authoritative adapter and
the fixed-length worker boundary. Modified ciphertext exercises FIPS implicit
rejection; it must not be described as guaranteed explicit decapsulation failure.

The allocation research is **not** the selected production allocation mode:
native/default-stack ML-KEM uses no heap. The experimental arena and GPL-licensed
wolfCrypt comparison remain research only and are not linked/vendored in v4.

Project deterministic fixtures are in `scripts/tests/v4/vectors.json`, independently
computed with Python hashlib/hmac and checked against C++. They cover exact T,
SHA3 hybrid root, all HKDF outputs, direction bits, Finished, Commit and object
commitments. Inputs are explicitly public synthetic test values. Primitive checks
retain RFC 5869 HKDF and AES-GCM vectors, add RFC 4231 HMAC, SHA3's published empty
value and P-256's standard generator/d=1 agreement. They are not production keys.

Sanitizers instrument the new core, native ML-KEM and test code. The reused
PSA/Mbed TLS dependency retains the existing pinned host configuration; this is
not a claim that every third-party library path has been sanitizer-fuzzed.

## Adversarial scenarios

The host radio has fixed eight-frame per-peer receive queues and a bounded
32-entry transport queue. It supports loss, duplication, corruption, delay,
reordering, disabled channels and withheld completion callbacks. Worker results
can be delayed, failed or stale. Storage can cut writes at byte boundaries.
Core reset/reboot, identity rotation, channel loss, rekey, rate exhaustion and
sequence/record thresholds run through actual implementation methods.

Additional targeted checks include:

* Actual hybrid physical pairing/SAS, both physical confirmations, storage
  readback, fresh session, authenticated command, replay rejection and rekey.
* Modified offer/P-256 public material, ML-KEM public key and ciphertext during
  pairing, with an attacker **recomputing** untrusted commitments. Establishment
  fails or SAS differs; no secret is persisted or command dispatched.
* Wrong persistent PSK rejects Hello before expensive KEM work; reboot uses the
  valid v4 record to obtain unrelated new session material.
* Both complete offers, both nonce objects, KEM public key and ciphertext change
  on reconnect. Every installed traffic-key fingerprint is checked against all
  previous installations in the bounded test history.
* Using only the known PSK in place of ephemeral secrets with the recorded public
  transcript produces different keys. This is a regression check, not a proof
  of cryptographic hardness; the forward-secrecy claim remains conditional.
* Failure/reset at KeyGen, Encaps and Decaps, stale results, cancellation during
  running work, queue saturation and no result reuse after generation changes.
* Corrupt Finished/Commit at each direction wipes all candidate/Finished/workspace
  material. Old encrypted data and old captured control/fragments cannot regain
  authority under a new transcript/generation.
* Conflicting and identical fragment duplicates, cross-object/generation/source
  confusion, exact length/count/index boundaries, padding/reserved bits, timeout,
  random bytes and cancellation/rotation clearing.
* Missing transmit completions force recovery after **500 ms**, including ONLINE
  traffic; candidate material cannot be installed from an uncompleted send.
* Safe local service advances throughout wireless failures in the simulator.
  Actual UART/rendering regression tests remain independent and unchanged.

## Fresh manual security review

Review covered every new runtime interface/implementation and test source,
wire encoding, all upstream vendored files by source hash/provenance and the
unchanged supporting PSA/codec boundary. Checked paths included combiner/context,
SAS commit/reveal ordering, downgrade, dual-contribution installation, fragment
mixing/length arithmetic, stale worker and TX tickets, cancellation ownership,
implicit rejection, nonce/sequence limits, storage provenance, logging and DoS.

Legitimate findings fixed before final validation:

1. Completed-object duplicates previously bypassed conflict checks. They now
   compare canonical/authenticated contents and cannot reschedule KEM work.
2. Storage failure recovery status was overwritten by generic reset.
3. Finished completion ordering could delay installation despite all proofs.
4. Helmet could wait for Finished without retransmitting its necessary nonce.
5. Sender pacing and authentication refill mismatch could repeatedly drop a
   particular fragment; pacing/refill are now compatible and separately bounded.
6. Storage reload and expired recovery state could retain old secret material.
7. A lost transport completion could stall ONLINE TX indefinitely despite valid
   inbound keepalives. The explicit transmit deadline now forces fresh recovery.
8. Zero root/pairing material and partially failed key derivation cleanup were
   hardened; authentication failures have their own replenishing budget.
9. Native radio installation preceded traffic-material validation. Invalid/reused
   traffic material is now rejected before altering native radio state; native
   installation failure clears candidate keys and requires fresh recovery.

All affected suites were rerun after fixes. No known unresolved core defect is
reported by this review. It is a development review, not independent cryptographic
certification. The security-diff-scan skill's referenced preflight resource/tools
were unavailable; no service-generated scan report or independent multi-agent
audit is claimed. Manual review and Clang analysis were completed instead.

## Static resources versus measurements

Official S31 compiler: GCC **15.2.0**, Espressif `esp-15.2.0_20251204`, ILP32F,
from the existing pinned ESP-IDF **6.1.0** preview research environment.
This table uses unlinked `-Og` S31 compiler objects, not a production S31 image.

| Item | Static/compiler evidence |
|---|---:|
| examined physical HP SRAM | 524,288 B |
| application linker SRAM region | 503,744 B |
| largest individual native frame | 19,056 B, `pqv4_indcpa_enc` |
| KeyGen/PCT known nested sum | 26,320 B |
| Encaps known nested sum | 22,992 B |
| Decaps known nested sum | 24,496 B |
| native dispatch maximum including wrapper | 26,336 B |
| future worker stack candidate | 49,152 B |
| separate platform/spill/interrupt allowance | 6,144 B, estimate |
| remaining stack margin after allowance | 16,672 B |
| `Core` (S31 ABI, contains candidate/workspace/result) | 30,640 B |
| `SlotWorker` (S31 ABI) | 6,392 B |
| `Exchange`, contained in Core | 9,440 B |
| `Result`, contained in Core | 6,360 B |
| `Material`, contained in Core | 340 B |
| `Reassembly`, contained in Core | 1,744 B |
| PK / SK / CT / SS worker buffers | 1,568 / 3,168 / 1,568 / 32 B |
| core+worker+stack reservation | 86,184 B |
| sum of unlinked core/native text/rodata estimates | 44,704 B |

| Budget | Helmet | Paw |
|---|---:|---:|
| unchanged v3 baseline `.data` | 19,332 B | 19,000 B |
| unchanged v3 baseline `.bss` | 30,576 B | 28,992 B |
| baseline contiguous linker tail before runtime allocation | 366,912 B | 383,424 B |
| conservative unlinked tail after core/worker/stack, all extra text in SRAM and extra 6 KiB reserve | 229,880 B | 246,392 B |

The estimate deliberately retains the whole v3 baseline while adding v4; it is
not an actual linked dual-protocol configuration. Future linked placement,
crypto adapters and runtime allocation must be measured again. It excludes
unverified LP/RTC/PSRAM capacity and does not count host RSS as MCU memory.
Main stack remains 12,288 B; other existing task settings are unchanged and
recorded in the S31 baseline report. No worker stack is allocated in production.

The actual production firmware-size increase is **zero**: no production source
or selection changed. There is no final v4 firmware-size or flash-margin measurement
because the platform integration is intentionally absent. Unlinked 44,704 B is
an estimate of added core/native code, not a claimed final image delta. Existing
S31 v3 baseline images remain 823,776 B Helmet / 739,824 B Paw.

Host qualification reports 32 worker iterations and one real-core reconnect
CPU sample. Mean sanitizer-host timings were KeyGen **1.554 ms**, Encaps
**0.502 ms**, Decaps **0.613 ms**; the one reconnect sample used **74.336 ms**
host CPU and **3,195 ms / 425 frames** in the simulated radio. These are sample
measurements, not worst-case bounds or S31 performance estimates. The retained
JSON labels host timing and simulated radio time/frame count explicitly. The
protocol minimum is 58 session frames / 7,424 radio payload bytes, while retries,
scan timing and budgets add more. Physical airtime, worst-case CPU time and
reconnect latency remain unmeasured.

**HARDWARE MEASUREMENT REQUIRED:** actual worker high-water, heap before/after
worker and normal objects, minimum/largest free heap block, fragmentation, KEM
durations, task priorities/interrupt overhead, watchdog health, UART/radio latency,
RF, power, thermal, real flash cuts and provisioning. The model cannot establish
real CCMP, memory protection or side-channel behavior. See the release checklist
in [WIRELESS_V4_HARDWARE_GATES.md](WIRELESS_V4_HARDWARE_GATES.md).

## Reproduce

```sh
python3 scripts/check-v4.py
python3 scripts/check-v4-resources.py
bash scripts/test-v4.sh --qualification
bash scripts/test-wireless.sh
python3 scripts/check-pq-worker.py
python3 scripts/check-s31-preflight.py --preview
python3 scripts/build-esp32.py
```

Use the existing documented per-helmet UART include mechanism for UART-enabled
C3 builds, and the pinned Arduino CLI/libraries for the unchanged Teensy matrix.
Host firmware/desktop tests need the existing Arduino math compatibility include
(`-include math.h`, or `cmath` with `using std::abs`) when using Clang 19 with
ArduinoFFT; no production source or upstream FFT routine was patched and no
warning was suppressed. Logs/artifacts stay under ignored `build/`.

**PROTOCOL V4 CORE SOFTWARE STATUS: COMPLETE.**
**S31 PRODUCTION INTEGRATION STATUS: BLOCKED ON OFFICIALLY SUPPORTED SDK.**
**PHYSICAL HARDWARE QUALIFICATION STATUS: PENDING.**

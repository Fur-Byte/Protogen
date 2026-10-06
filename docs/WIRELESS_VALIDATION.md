# Wireless validation and release gates

> **Target plan (2026-10-06):** ESP32-S31 is the intended final wireless MCU
> for helmet and Paw; C3/v3 is retained as legacy development/regression.
> Current production sources remain v3. Public S31 preview builds and a 48 KiB
> ML-KEM-1024 worker have static resource evidence, but the supported-stable SDK
> and its memory-protection implementation block the production migration/v4
> phase. Physical S31 qualification is separately pending. See the
> [S31 preflight](WIRELESS_S31_PREFLIGHT.md) for evidence and remaining work.

## Final workspace results — 2026-10-06

All target build records match source SHA-256
`bc72e25ddd4934f0ef803d9c20d73b1e3915262dd2418eba17c8dc0f263ac161`.

| Validation | Final result |
| --- | --- |
| ESP32 helmet/paw, default and authenticated UART | 4 successful builds; 0 warnings |
| Teensy sensor matrix / extension matrix / authenticated UART / default | 8 / 5 / 3 / 1 successful builds; 0 warnings; actual compiler commands contain required flags |
| Authenticated Teensy firmware | Linked HMAC/adapter symbols verified; 83,964–94,204 flash bytes, 15,424–18,304 RAM1 variable bytes |
| Wireless ASan/UBSan suite | PASS: 1,000,000 protocol events, 1,000,000 GCM records, named regressions and 52,000 corruption cases |
| Storage fault suite | PASS: 135,983 cases in the documented atomic NVS model |
| UART suite | PASS: interoperability and 100,000 malformed streams |
| Teensy/tool suite | PASS: actual rendering/command gate, 1,000 malformed UART streams, eight sensor masks, 25 Python tests, retained face equivalence |
| Retained historical DTLS suite | PASS; this transport is not added to v3 firmware |
| Clang Static Analyzer | PASS: 10 translation units; no diagnostics |
| Source checks | Complete 79-file manifest, pinned upstream hashes, private-key/token scan, Python syntax, and `git diff --check` pass |

The 64-session host reconnect benchmark measured 80.7079 ms mean, 135.244 ms
maximum, and 5,163.69 ms total process CPU time. Each completed exchange uses
five frames / 640 control bytes. Host objects: Handshake 1,048 bytes, Link 2,712
bytes; process peak RSS 417,056 KiB includes sanitizer overhead. These are not
ESP32 heap, complete stack high-water, latency or battery measurements.

## Automated checks

`bash scripts/test-wireless.sh` builds the actual v3 pairing, session, replay,
recovery and link modules against ESP-IDF's bundled Mbed TLS 4.1 PSA software
backend. Test code has address/undefined-behavior sanitizers, assertions and
`-Wall -Wextra -Werror`; there is no fake cipher. The transport is an explicit
in-memory two-radio model (role/MAC/channel/native-encryption boundaries), not a
claim to execute Espressif's Wi-Fi driver or CCMP implementation on the host.
The small compatibility headers alias ESP-IDF's old bignum/ECP include names to
its real private software headers, without replacing cryptographic operations.

Current automated coverage includes the retained RFC 5869 HKDF and NIST AES-GCM
vectors, pairing/interruption/confirmation, invalid committed curve-point recovery, first-use MITM with mismatched SAS
under the documented human comparison policy, replacement timeout preserving the
old pair, resets of either/both devices, stale sessions/Finished, corrupt
Finished/ciphertext/tag, wrong PSK/known MAC, channel recovery, idle timing,
queue saturation, rapid actions, battery/ACK/status and sequence/replay edges.

| Check | Cases / scope |
| --- | --- |
| Packet corruption/fuzz | 50,000 random packets + 2,000 valid-header corruptions |
| Authenticated-record limit | 1,000,000 records sealed and opened with actual PSA AES-GCM; refuses further same-key use/reset |
| Protocol-event corpus | 1,000,000 event steps; seeds `0x50524f33`, `0x46494e49`, `0x52535433`, `0xdeadbeef` |
| Storage faults | 135,983 atomic-write, torn-read, every-byte truncation, bit-corruption, unknown-schema and randomized-record cases |
| UART MAC/parser | PSA/Mbed TLS interoperability, reset/session/epoch/replay/CRC-only rejection and 100,000 malformed streams |
| Actual Teensy UART dispatch | Valid MAC execution; forged MAC with valid CRC, replay, legacy and unsupported action rejection; local buttons/rendering after 1,000 malformed streams |
| Host reconnect benchmark | 64 completed ephemeral exchanges, five frames each; wall/CPU time, object sizes, process RSS |

The event corpus drives the real Pairing/Handshake/Session/Link modules over an
explicit bounded radio model. It injects loss, duplicate/corrupt/stale control and
DATA packets, bursts, paired replacement attempts, single/both resets, wrong PSK,
storage unavailable/recovered, channel failures, renewal and clock wrap. Every
command dispatch checks ONLINE, recorded legitimate origin, current receiver
identity and uniqueness of execution; queues and lifecycle invariants are checked
continuously. Identities are checked for reuse at each completed connection. Seeds
fix event choices; cryptographic randomness deliberately uses PSA's secure host
RNG rather than an insecure deterministic production replacement. An exact
cryptographic byte trace is therefore not fixed by the event seed. Failures
introduced while developing the explicit Commit label were corrected and retained
in the dedicated fresh-handshake/Finished regression path.

The storage model covers pre-write, pre-commit, post-durable/pre-ack and completed
writes for first pair, replacement, FORGET and configuration changes. Interrupted
reads test every byte boundary and require old exact, new exact, or rejected/
unpaired state. Unknown future schemas are rejected even with valid CRC; no
unreviewed migration occurs. It tests the NVS atomic-blob API contract rather than
executing Espressif's flash garbage collection or physical power loss. CRC is not
a cryptographic MAC. A malicious writer with unlocked physical flash access is
outside the development storage guarantee.

```sh
bash scripts/test-wireless.sh
bash scripts/test-firmware.sh
bash scripts/test-dtls.sh
python3 scripts/check-features.py       # all eight sensor combinations
python3 scripts/check-extensions.py     # all five existing extension combinations
python3 scripts/check-uart-auth.py      # three additional authenticated UART variants
python3 scripts/build.py                # selected/default Teensy
python3 scripts/build-esp32.py          # helmet and paw
CXX=clang++ python3 scripts/analyze-wireless.py
git diff --check
```

The firmware build helper now uses `-Werror` for all Teensy configurations;
it preserves the board's original `build.flags.defs`, appends security options to
that consumed property, and verifies the actual compiler-command database.
The authenticated variants additionally verify linked HMAC/adapter symbols;
an ignored feature flag cannot count as a successful authenticated build.
ESP32 project source and host targets use `-Wall -Wextra -Werror`. ESP-IDF's own
build policy retains vendor exception flags; build manifests reject any warnings.
Sanitizers instrument application modules and the vendored Teensy MAC library;
ESP-IDF's host PSA backend is the real software library, not a fake cipher, but
is not itself fully sanitizer-instrumented by this harness. Clang Static Analyzer
covers seven portable radio modules plus UART authentication, its Mbed adapter,
and storage codec through their actual host call sites (10 unique translation
units). Closed Wi-Fi/CCMP driver and hardware code are outside host analysis.

Results, compilation logs, source identities, artifact hashes, benchmark JSON and
static stack-frame estimates are under ignored `build/`. Authenticated UART
firmware artifacts contain their unique provisioned development key and must not
be treated as public artifacts. Public fixtures are explicitly labelled and
excluded from firmware include paths. No upload, eFuse programming or production
lockdown is performed by these checks.

### Performance interpretation

The benchmark measures host software P-256 work for both peers, not ESP32 radio
or energy use. The loss-free protocol transfers five fixed 128-byte controls,
performs one key generation and one agreement per peer and performs **no ECC on
operational commands**. The initial measured mean was about 81–84 ms for both
peers together; final measurements are recorded in `build/wireless-benchmark.json`.
Handshake/Link object sizes are roughly 1.05/2.71 KiB on the host. Sanitizer process
RSS includes quarantine/shadow allocations and is not MCU heap usage. `.su` files
record host static stack frames, not a complete crypto-call-chain high-water mark.
Actual MCU reconnect p50/p95, CPU/task time, PSA heap allocation/high-water,
stack high-water and measured energy/current per reconnect are hardware release
gates. Time-based rekey adds approximately four session establishments/hour;
its actual battery impact is not measured here. Idle STA reception remains
power-save-disabled for reliability, as before.

## Physical checks still required (not executed in this workspace)

Use two actual configured C3 boards with safe isolated supplies and the mandatory
LEDs. No RF adapter/attached serial devices are available here.

| Bench test | Acceptance |
| --- | --- |
| Phone/laptop Wi-Fi scans, both roles: unpaired, normal, pairing, recovering | No AP/SSID from either device; this cannot be proven by a host test |
| Monitor-mode capture | Fixed 128-byte operational ESP-NOW payloads; no operational broadcast, secrets or factory source address |
| Compare/confirm matching and deliberately mismatching LED codes | Both local gestures and second confirmations required; mismatch never accepted |
| Timeouts/interruption and partial NVS commit | Old pair preserved on timeout; partial replacement disconnected; new physical attempt restores operation |
| Inject random lengths/roles/versions, known-MAC/wrong-key frames, captured duplicates/old sessions | No command, crash, deadlock or excessive rendering delay |
| Reset each board separately, reset both, lost finish/response/control frames and rapid reconnects | Fresh nonces/keys/MACs, automatic recovery, no held-button command |
| Intentional power/channel loss, single-channel interference and changed channel | Authenticated recovery without a message on the failed channel; latest-session then persistent fallback schedule |
| Broadband interference | Local controls/rendering continue; no promise of radio operation |
| Burst button presses, delayed/lost UART execution ACK, full TX/RX queues | Bounded queues, stale intent/ticket expiry, no duplicate execution |
| First button after long idle and active gestures | Fresh ticket round-trip; record actual p50/p95 latency and misses |
| Idle, active, degraded link and battery telemetry capture | 3–5 s quiet keepalives, 100 ms active/degraded polls, 12 s radio loss threshold, 20 s optional battery report |
| At least 17 minutes idle, high-traffic record limit, and repeated ECDH renewal | Encrypted renewal, fresh identities/session, brief safe reconnect |
| Repeated stop/start and MAC changes during traffic | No callback races, stale peer entries, CCMP rejection loops or transmit-completion wedge |
| Paired/unpaired resets and external LED driver | Correct C3 strap boot levels; SAS readable through complete six-digit cycles |
| UART enabled on both controllers; wrong key, reset, injection and mixed builds | Authenticated commands only; local rendering survives every failure; measure parser CPU and queue behavior |
| Physical power cuts across NVS writes/erase/replacement/configuration/GC | Exactly old valid pair, new valid pair or unpaired/recovery required; no mixed authority |
| Reconnect CPU/heap/stack/current measurements on actual C3 | Record p50/p95 duration, task time, allocation/high-water and joules; no ECC during commands |
| Production NVS/key/security boot paths on sacrificial release boards | Missing pre-provisioned key fails without any eFuse write; signed boot/update/rollback policy verified by factory process |

Record actual RF latency, disconnect/recovery distributions, dropped actions,
range/body attenuation, current draw, antenna/channel behavior, temperature and
LED ergonomics before release. Unlocked development flash can reveal secrets;
production hardware protection has not been bench-validated here. Do not execute
irreversible provision/burn/upload commands as part of these host checks.

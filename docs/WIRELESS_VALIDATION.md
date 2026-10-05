# Wireless validation and release gates

## Automated checks

`bash scripts/test-wireless.sh` builds the actual v2 pairing, session, replay,
recovery and link modules against ESP-IDF's bundled Mbed TLS 4.1 PSA software
backend. Test code has address/undefined-behavior sanitizers, assertions and
`-Wall -Wextra -Werror`; there is no fake cipher. The transport is an explicit
in-memory two-radio model (role/MAC/channel/native-encryption boundaries), not a
claim to execute Espressif's Wi-Fi driver or CCMP implementation on the host.
The small compatibility headers alias ESP-IDF's old bignum/ECP include names to
its real private software headers, without replacing cryptographic operations.

Covered: independent RFC 5869 HKDF-SHA256 and NIST AES-256-GCM vectors; first pairing and matching SAS; no peer; wrong commitment; one missing
physical confirmation; interrupted/reset pairing; storage failure; legitimate
reconnect; reset paw, helmet, both; key/session/MAC renewal; duplicate/old-session
records; corrupted ciphertext/tag; truncated/unknown-version/unsupported-command
packets; 50,000 random frames and 2,000 valid-header tag/ciphertext corruptions;
replay-window boundaries and zero/max/overflow sequence values; known identity
with wrong keys; quiet idle keepalives; link loss; changed-channel recovery;
driver-completion failure; fixed-queue saturation; four rapid intents, intent
expiry and clock wrap; status/ACK and battery envelopes; LED waveform; dropped session finish and current-channel interference; offline rendezvous identity renewal.

`bash scripts/test-firmware.sh` runs the existing COBS/CRC16/freshness/dedup/rate
and actual Teensy command-path suite under sanitizers, along with configuration,
face/sensor and Python-tool tests. `bash scripts/test-dtls.sh` retains historical
library regression coverage. `python3 scripts/check-extensions.py` compiles five
real Teensy feature combinations. `python3 scripts/build-esp32.py` builds helmet
and paw and enforces dependency pins, SDK choices, no SoftAP, warnings-as-errors
in project source, and source/artifact evidence. `CXX=clang++ python3 scripts/analyze-wireless.py` runs Clang Static Analyzer on the seven portable protocol source modules, using the actual host compile commands. `git diff --check` is a text
hygiene check. Results and compiler output are under ignored `build/`.

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
| At least 17 minutes idle per session | Encrypted renewal, fresh identities/session, brief safe reconnect |
| Repeated stop/start and MAC changes during traffic | No callback races, stale peer entries, CCMP rejection loops or transmit-completion wedge |
| Paired/unpaired resets and external LED driver | Correct C3 strap boot levels; SAS readable through complete six-digit cycles |
| Production NVS/key/security boot paths on sacrificial release boards | Missing pre-provisioned key fails without any eFuse write; signed boot/update/rollback policy verified by factory process |

Record actual RF latency, disconnect/recovery distributions, dropped actions,
range/body attenuation, current draw, antenna/channel behavior, temperature and
LED ergonomics before release. Unlocked development flash can reveal secrets;
production hardware protection has not been bench-validated here. Do not execute
irreversible provision/burn/upload commands as part of these host checks.

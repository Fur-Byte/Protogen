# Implementation review record

The v2 implementation preserves the Teensy controller and UART contract and
replaces the active USB-provisioned DTLS/factory-MAC radio path with deliberate
wireless pairing, PSA application encryption and rotating identities.

Pair: gestures on both devices → 60 s LED window → committed ephemeral P-256
ECDH → compare six LED digits → second local confirmation on both → atomic
per-device NVS write → authenticated reconnect. Erase: hold the local gesture
12 s; replace peers by re-pairing both units. No USB pairing or remote entry.

Session: paw and helmet contribute fresh random challenges, prove possession
of the pairing secret using transcript-bound HMAC, then use labelled
HKDF-SHA-256 for directional AES-256-GCM keys, ESP-NOW LMK/PMK, session ID,
identity MACs and channel recovery material. 128-byte operational unicast
packets have a 64-record replay window and cannot dispatch before full validation.

Identity/recovery: random local rendezvous addresses; transcript-derived local
session addresses; disjoint role bits; session renewal at 15–17 idle minutes;
disconnected address rotation around 30 s. Stop/reinitialize the native radio
when changing MAC/peer keys. Recovery uses keyed channel order and asymmetric
400/2400 ms dwell, session material initially and pairing-secret fallback after
15 s. This works without synchronized boot clocks or failed-channel messages.

Timing: idle requests every random 3–5 s; immediate gesture/status requests;
100 ms active and degraded polling; 1.5 s active tail; 12 s RF disconnect, with
Teensy's existing local 2 s UART timeout clearing its paw-online indication
within another 2 s. Rendering and local controls continue throughout.

## Every changed/created source or documentation file

Paths below are relative to the repository root. Paired header/source names
list both files explicitly; the header defines the bounded API and the source
implements it. Existing DTLS sources/tests and WearerIO remain unchanged.

| File(s) | Change |
| --- | --- |
| `README.md` | Points to v2 firmware and new test runner |
| `SECURITY.md` | Threat boundary, protections, limits and production requirements |
| `docs/ENGINEERING_PLAN.md` | Replaces obsolete USB/DTLS/fixed-channel plan with implemented design |
| `docs/HARDWARE_WIRING.md` | Mandatory buffered pairing LED and C3 strap qualification |
| `docs/WIRELESS_PROTOCOL.md` | Full wire layouts, pairing/session/identity/recovery/timing design and SDK limits |
| `docs/PRODUCTION_PROVISIONING.md` | Separate reviewed irreversible factory steps, manual SDK selections and update blocker |
| `docs/WIRELESS_VALIDATION.md` | Automated coverage and explicit physical test matrix |
| `docs/WIRELESS_IMPLEMENTATION.md` | This review record and complete file manifest |
| `firmware/esp32/README.md` | User pairing/erase/build/operation/migration instructions |
| `firmware/esp32/sdkconfig.defaults` | PSA algorithms, no SoftAP and recoverable security defaults |
| `firmware/esp32/src/CMakeLists.txt` | Builds v2 modules and documented SDK dependencies, warnings as errors |
| `firmware/esp32/src/Kconfig.projbuild` | Explicit production-security guard selection |
| `firmware/esp32/src/DeviceConfig.h` | Version-2 atomic secret record, strict decoding and LED wiring pin |
| `firmware/esp32/src/main.cpp` | Integrates link/physical gestures, bounded intents, wireless telemetry and unchanged UART/Teensy handoff |
| `firmware/esp32/src/WirelessConfig.h` | Central version, packet sizes, timings, replay/channel/retry and UI constants |
| `firmware/esp32/src/WirelessCrypto.h`, `WirelessCrypto.cpp` | PSA HKDF/HMAC/SHA256/GCM/P-256 wrappers, RNG and secret wiping |
| `firmware/esp32/src/ReplayWindow.h` | Bounded 64-record authenticated replay state |
| `firmware/esp32/src/WirelessSession.h`, `WirelessSession.cpp` | Domain-separated material, fixed packet codec, AEAD and directional command validation |
| `firmware/esp32/src/WirelessPairing.h`, `WirelessPairing.cpp` | Commit/reveal agreement, SAS, independent local confirmations, timeout and bounded candidate |
| `firmware/esp32/src/SessionHandshake.h`, `SessionHandshake.cpp` | Fresh mutual challenge authentication and reconnect retries |
| `firmware/esp32/src/IdentityRecovery.h`, `IdentityRecovery.cpp` | Local identities and keyed asymmetric channel rendezvous |
| `firmware/esp32/src/WirelessTransport.h` | Fixed-frame transport abstraction used by real radio and host model |
| `firmware/esp32/src/EspNowTransport.h`, `EspNowTransport.cpp` | STA-only documented APIs, pre-radio entropy, native encrypted peers and fixed callback queue |
| `firmware/esp32/src/WirelessLink.h`, `WirelessLink.cpp` | Pair/session lifecycle, bounded TX/RX, keepalives, renewal, identity changes and failure recovery |
| `firmware/esp32/src/SecureStorage.h`, `SecureStorage.cpp` | Atomic storage, production guards and read-only hardware-key initialization |
| `firmware/esp32/src/PairingIndicator.h`, `PairingIndicator.cpp` | Six-digit LED waveform and pairing indication |
| `firmware/esp32/src/ActionQueue.h` | Four expiring paw intents; no offline carryover |
| `scripts/build-esp32.py` | Enforces no SoftAP, enabled algorithms and recoverable development SDK settings |
| `scripts/pair-paw.py` | Retires plaintext USB key provisioning; migration guidance only |
| `scripts/test-wireless.sh` | Builds/runs real PSA protocol tests with sanitizer instrumentation |
| `scripts/analyze-wireless.py` | Clang Static Analyzer using actual module compile commands |
| `scripts/tests/wireless.cpp` | Primitive vectors, pairing, AEAD/replay/fuzz, reset/renewal/MAC/channel/loss/queue tests |
| `scripts/tests/wireless-config.h` | Crypto-only host library configuration |
| `scripts/tests/wireless-crypto-config.h` | Host PSA algorithm/curve/entropy selections |
| `scripts/tests/wireless/CMakeLists.txt` | Links exact project modules and SDK library; sanitizer/test flags |
| `scripts/tests/wireless/compat/mbedtls/bignum.h` | Host alias for SDK software backend include-name compatibility |
| `scripts/tests/wireless/compat/mbedtls/ecp.h` | Host alias for SDK software backend include-name compatibility |
| `scripts/tests/helmet-protocol.cpp` | Updates secure-record regression checks to schema 2 |
| `scripts/tests/test-pair-paw.py` | Verifies retired provisioner gives guidance without USB access/key generation |

## Validation and practical limits

Builds: both ESP32 firmware targets and all five Teensy extension combinations.
Checks: wireless PSA/sanitizer suite, existing Teensy/packet/tool suite, retained
DTLS regression, Clang Static Analyzer on seven portable source modules,
`git diff --check`. Build evidence, artifact hashes and compiler output are
under `build/`. No upload, eFuse write or production lockdown is performed.

Not safely verifiable here: ordinary Wi-Fi scan visibility, real ESP-NOW/CCMP
behavior during stop/start/MAC changes, range, RF replay/injection, battery
endurance, physical LED comparison/strap circuitry, and production hardware
security. See the physical matrix before release. The single-factory partition
and prototype flashing workflow are not a field OTA/update system; qualify a
signed update/recovery design before factory lockdown. Development NVS is not
hardware protected. Interrupted pair commits can require re-pairing both devices.
No forward secrecy is claimed for PSK-only reconnect after later secret theft.

Manual production options and ESP-IDF limitations are listed in
[PRODUCTION_PROVISIONING.md](PRODUCTION_PROVISIONING.md). In particular, this
SDK exposes HKDF through PSA, not a `CONFIG_MBEDTLS_HKDF_C` switch, and encrypted
NVS's normal initializer can generate/burn a missing HMAC eFuse: this design uses
a read-only key lookup instead. Buffered pairing LEDs are a required hardware
addition on these board maps. RF fingerprinting, timing/transition correlation
and broadband jamming remain limitations; the system is not invisible or
unjammable.

Final workspace checks (2026-10-05): helmet binary 745,440 bytes; paw binary
672,112 bytes; each has a 1,536,000-byte application partition. Both target builds
succeeded with zero warnings and matching final source identity. Wireless
sanitizer tests (52,000 random/corrupt frames plus named scenarios), the existing
Teensy/tool suite, historical DTLS tests and all five Teensy extension builds
passed. Clang Static Analyzer reported zero diagnostics across seven modules;
`git diff --check` and Python syntax compilation passed. Physical tests remain
unexecuted as specified above.

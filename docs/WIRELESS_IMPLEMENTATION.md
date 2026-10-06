# Focused v3 wireless hardening implementation

This pass extends the existing v2 architecture. Teensy 4.0 still renders and owns
behavior; helmet ESP32 validates wireless commands before forwarding them.
ESP-NOW native AES-128-CCMP encrypted unicast, PSA AES-256-GCM, HKDF/HMAC-SHA-256,
P-256, 128-byte operational envelopes, local MAC identities, replay window,
physical/SAS pairing, channel rendezvous and bounded queues remain intact.
No SoftAP/SSID/scan, extra application cipher, negotiation or raw-frame disguise.

Every reconnect uses new P-256 keys and random nonces. Paw Hello authenticates
its contribution; helmet Response authenticates the complete ordered transcript;
ECDH plus its digest derives traffic, identity, CCMP, recovery and confirmation
keys. Private keys/raw ECDH are destroyed immediately. Paw Finished → Helmet
Finished → separately keyed Paw Commit precede ONLINE and MAC/radio installation.
[WIRELESS_PROTOCOL.md](WIRELESS_PROTOCOL.md) contains the exact layouts, key labels
and state diagram, including the unavoidable lost-final-ack readiness limitation.

Rekey by randomized 15–17 minutes, 1,000,000 authenticated records, or near
sequence exhaustion. The codec stops at the record limit, and fresh keys are
required for any production sequence reset. Crypto budgets survive handshake
resets: four ECC credits refilling every 500 ms; two creation credits every 1 s;
eight verification credits every 50 ms. Eight invalid tags produce bounded
250 ms backoff. Existing ONLINE data is independent of these unauthenticated
budgets. Duplicate in-flight handshakes use cached replies; four recent authenticated
Hello digests suppress short-lived replay across attempts. No permanent lockout.

Link, Handshake, Pairing and Session now use explicit phase enums. Production
ONLINE/dispatch checks and transition checks enforce paired/active/confirmed
state; debug assertions check impossible internal combinations. Timeouts and
failed confirmation wipe candidates before retry. Invalid pairing curve points
return immediately to recovery; both directional traffic keys must change before
a session counter can reset. Dedicated regressions cover these failure paths. Pairing never overlaps an
operational session. Storage decoding clears invalid destinations; configuration
copies wipe on destruction. Atomic whole-blob NVS storage remains schema 2, with
no unreviewed future migration. Failed read/save/security-config paths wipe
buffers and fail closed. Production hardware safeguards remain mandatory.

`SECURITY STATUS` reports only phase, age, authenticated record count, renewal
interval, channel and saturating non-secret counters. It never exposes secret
keys, tags, nonces, SAS or factory MAC/history and never changes authentication.
A SAS mismatch requires comparing two devices and cannot be diagnosed locally;
no misleading mismatch counter is fabricated.

`PROTOGEN_UART_AUTH` is an explicit optional build feature, with one generated
secret per helmet/Teensy pair and no universal key. Authenticated COBS/CRC16
UART envelopes bind version, role, boot epoch, new receiver session ID, sequence,
command and complete payload. PSA and a small unmodified upstream Mbed TLS 3.6.5
MAC subset interoperate. The enabled parser rejects CRC-only commands. A replayed
request cannot replace an active UART session without new authenticated confirmation.
The normal Teensy keeps its secret in extractable firmware flash; the feature
is not secure boot or physical tamper resistance. Disabled development builds
retain the explicitly trusted wire boundary. [PRODUCTION_PROVISIONING.md](PRODUCTION_PROVISIONING.md)
describes safe private provisioning and required manual production decisions.

The wipe audit covers volatile PSA private handles, raw shared secrets, temporary
HKDF outputs, superseded session/Finished/CCMP/recovery material, decrypted temporary
buffers, pairing candidates, Config copies and raw console/storage records.
ESP-NOW application peer structs are wiped; peer removal, PMK replacement and
driver deinitialization retire native keys. Proprietary driver-internal physical
zeroization cannot be inspected or promised from these APIs. No eFuses are
programmed, no firmware uploaded, and development remains recoverable.

## Validation evidence and remaining limits

[WIRELESS_VALIDATION.md](WIRELESS_VALIDATION.md) enumerates automated counts,
reproducible event seeds, storage-fault assumptions and hardware qualification.
The suite includes one million state-event steps, one million real AES-GCM
records, 135,983 storage faults, 52,000 radio corruption cases, 100,000 UART
streams, and actual Teensy behavior tests with malformed authenticated input.
Test fixtures are public material, never firmware defaults. Clang Static Analyzer
covers ten portable protocol/storage/UART translation units; warnings are errors.
Target validation includes four ESP32 and seventeen Teensy builds. The Teensy
helper preserves the board definitions, uses the consumed `build.flags.defs`
property, checks actual compiler commands, and verifies linked UART HMAC/adapter
symbols. A regression rejects ignored security flags. All final builds have zero
warnings and match the final source digest recorded in WIRELESS_VALIDATION.md.

A 64-session host benchmark records mean/max wall time, process CPU time, five
control frames/640 bytes, object sizes and process RSS in the ignored build tree.
Host `.su` stack frames and sanitizer RSS are not MCU stack/heap/energy data.
Target ECDH/reconnect timing, heap/stack high-water, current draw, SSID visibility,
RF injection/CCMP/MAC races, actual NVS power cuts, readable SAS LEDs and protected
production hardware still require bench qualification. No invisibility, broadband
jamming immunity, perfect physical zeroization or physical Teensy key protection
is claimed. No field OTA/rollback implementation or irreversible factory station
was added; those remain explicit production blockers.

## Complete modified/created file manifest

Paths are relative to the repository root. Build outputs and private provisioning
headers are ignored, not source changes. Upstream hashes/license are recorded
in `src/UartCrypto/UPSTREAM.json`; upstream `.inc` files and public headers are
unmodified, with separate compile wrappers and symbol-isolation macros.

Total: **79 files**.

| File | Change |
| --- | --- |
| `README.md` | v3 validation entry point |
| `SECURITY.md` | Threat boundary, forward secrecy, UART and production limitations |
| `docs/ENGINEERING_PLAN.md` | Align active design with v3 primitives and lifecycle |
| `docs/PRODUCTION_PROVISIONING.md` | Unique UART key build/provisioning boundary and ordinary Teensy limitation |
| `docs/WIRELESS_IMPLEMENTATION.md` | Implementation decisions, wipe audit, limits and this exhaustive manifest |
| `docs/WIRELESS_PROTOCOL.md` | Exact handshake, KDF, Finished, state/DoS/UART/storage/telemetry specification |
| `docs/WIRELESS_VALIDATION.md` | Test counts, seeds, fault model, benchmark and hardware gates |
| `firmware/PROT_OS7_MAX7219/PawLink.ino` | Compile-time authenticated UART adapter before the existing command gate |
| `firmware/PROT_OS7_MAX7219/UartCryptoConfig.h` | Minimal upstream SHA256/MD configuration |
| `firmware/PROT_OS7_MAX7219/UartCryptoSymbols.h` | Isolate upstream library symbols without changing algorithms |
| `firmware/PROT_OS7_MAX7219/mbedtls/build_info.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/check_config.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/config_adjust_legacy_crypto.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/config_adjust_ssl.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/config_adjust_x509.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/error.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/md.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/md5.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/platform.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/platform_util.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/private_access.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/ripemd160.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/sha1.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/sha256.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/sha3.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/sha512.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/mbedtls/threading.h` | Unmodified pinned upstream public header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/src/HelmetProtocol/SecureZero.h` | Portable reviewed volatile secure-zero helper |
| `firmware/PROT_OS7_MAX7219/src/HelmetProtocol/UartAuthentication.h` | Bounded authenticated UART handshake/codec, replay/session/epoch checks |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/LICENSE` | Upstream license |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/UPSTREAM.json` | Pinned version, archive and per-file upstream SHA256 evidence |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/UartMac.h` | Reusable upstream Mbed TLS HMAC context with per-helmet private header |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/alignment.h` | Unmodified upstream internal header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/common.h` | Unmodified upstream internal header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/md.c` | Compile-time wrapper for the unmodified upstream md implementation |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/md.inc` | Unmodified upstream md implementation (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/md_wrap.h` | Unmodified upstream internal header (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/platform_util.c` | Compile-time wrapper for the unmodified upstream platform_util implementation |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/platform_util.inc` | Unmodified upstream platform_util implementation (hash in UPSTREAM.json) |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/sha256.c` | Compile-time wrapper for the unmodified upstream sha256 implementation |
| `firmware/PROT_OS7_MAX7219/src/UartCrypto/sha256.inc` | Unmodified upstream sha256 implementation (hash in UPSTREAM.json) |
| `firmware/esp32/README.md` | v3 usage, security status and optional UART build documentation |
| `firmware/esp32/platformio.ini` | Opt-in private UART provisioning build hook |
| `firmware/esp32/src/DeviceConfig.h` | Securely erased Config copies and fail-closed decode destination |
| `firmware/esp32/src/EspNowTransport.cpp` | Retire CCMP peers/PMK and wipe temporary native peer key copies |
| `firmware/esp32/src/IdentityRecovery.cpp` | v3 domain context and superseded schedule wiping |
| `firmware/esp32/src/IdentityRecovery.h` | v3 domain context and superseded schedule wiping |
| `firmware/esp32/src/SecureStorage.cpp` | Validate saves, clear failed loads and wipe partial security-config buffers |
| `firmware/esp32/src/SecurityState.h` | Explicit states, saturating diagnostics, wrap-safe token buckets and backoff |
| `firmware/esp32/src/SessionHandshake.cpp` | Fresh authenticated ephemeral P-256, canonical transcript and new-key Finished/Commit |
| `firmware/esp32/src/SessionHandshake.h` | Fresh authenticated ephemeral P-256, canonical transcript and new-key Finished/Commit |
| `firmware/esp32/src/WirelessConfig.h` | Wire version 3, Commit type, centralized rekey/rate/backoff limits |
| `firmware/esp32/src/WirelessCrypto.cpp` | PSA volatile lifetime, secure failure outputs, non-copyable private handles |
| `firmware/esp32/src/WirelessCrypto.h` | PSA volatile lifetime, secure failure outputs, non-copyable private handles |
| `firmware/esp32/src/WirelessLink.cpp` | Explicit production-gated security states, confirmation/recovery/rekey/telemetry integration |
| `firmware/esp32/src/WirelessLink.h` | Explicit production-gated security states, confirmation/recovery/rekey/telemetry integration |
| `firmware/esp32/src/WirelessPairing.cpp` | Explicit confirmation phases, shared crypto budgets and distinct confirmation subkey |
| `firmware/esp32/src/WirelessPairing.h` | Explicit confirmation phases, shared crypto budgets and distinct confirmation subkey |
| `firmware/esp32/src/WirelessSession.cpp` | ECDH-only labelled material, record budget, replay diagnostics and same-key reset prevention |
| `firmware/esp32/src/WirelessSession.h` | ECDH-only labelled material, record budget, replay diagnostics and same-key reset prevention |
| `firmware/esp32/src/main.cpp` | Security status console, UART adapter and temporary-secret wiping |
| `scripts/analyze-wireless.py` | Deduplicate and expand analyzer coverage to ten radio/UART/storage translation units |
| `scripts/build-esp32.py` | Record and select optional UART build configuration |
| `scripts/build_support.py` | Warnings-as-errors and per-run optional compiler flags |
| `scripts/check-uart-auth.py` | Three actual authenticated Teensy builds with temporary private keys |
| `scripts/esp32-uart-build.py` | Append authenticated UART flags while preserving board roles |
| `scripts/provision-uart-auth.py` | Exclusive 0600 unique per-helmet development provisioning header generator |
| `scripts/test-firmware.sh` | Add actual authenticated Teensy dispatcher/local-fallback sanitizer test |
| `scripts/test-wireless.sh` | Run expanded sanitized radio/storage/UART suites and benchmark |
| `scripts/tests/helmet-protocol.cpp` | Update fail-closed persistent-record regression expectations |
| `scripts/tests/test-tools.py` | Reject ignored compiler security flags and preserve pinned toolchain definitions |
| `scripts/tests/storage-faults.cpp` | Whole-record power-loss/storage/schema fault injection model |
| `scripts/tests/uart-auth.cpp` | Real PSA/upstream HMAC interoperability and 100000 malformed/replay cases |
| `scripts/tests/uart-fixture/ProtogenUartKey.h` | Explicitly PUBLIC fixture, excluded from firmware builds |
| `scripts/tests/uart-mbed-adapter.cpp` | Separate upstream MAC translation unit for host interoperability |
| `scripts/tests/uart-rendering.cpp` | Actual Teensy authenticated dispatch and local control/rendering failure tests |
| `scripts/tests/wireless-benchmark.cpp` | 64 loss-free ephemeral reconnects with host performance/resource measurements |
| `scripts/tests/wireless.cpp` | Named v3 regressions, record quota, authenticated command oracle and million-event corpus |
| `scripts/tests/wireless/CMakeLists.txt` | Warnings/sanitizers, independent upstream MAC target, storage/UART/benchmark targets |

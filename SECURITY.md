# Wireless security boundary

> **Target plan (2026-10-06):** ESP32-S31 is the intended final wireless MCU
> for helmet and Paw; C3/v3 is retained as legacy development/regression.
> Current production sources remain v3. Public S31 preview builds and a 48 KiB
> ML-KEM-1024 worker have static resource evidence, but the supported-stable SDK
> and its memory-protection implementation block production S31 integration.
> The isolated [experimental v4 core](docs/WIRELESS_V4_PROTOCOL.md) implements
> host-tested hybrid P-256 + ML-KEM-1024 establishment and is never selected by
> production builds. Physical S31 qualification is separately pending. See the
> [S31 preflight](docs/WIRELESS_S31_PREFLIGHT.md) for evidence and remaining work.

## Reporting a vulnerability

Use **Security → Advisories → Report a vulnerability** on GitHub if the button
is available. Otherwise, contact the maintainer through an existing private
channel. If you have no private channel and can open a
[repository issue](https://github.com/Fur-Byte/Protogen/issues/new), use it only
to ask for a private reporting channel. Share vulnerability details, reproduction
steps and sanitized diagnostics privately. Keep keys, credentials, key-bearing
images, personal data, raw device captures and sensitive diagnostics out of
GitHub issues, pull requests and Actions evidence.

## Wireless trust boundary

The experimental v4 library is outside this active v3 trust boundary. Its
[protocol specification](docs/WIRELESS_V4_PROTOCOL.md) documents conditional
hybrid forward secrecy, SAS limitations and the standards-based combiner.
[Hardware gates](docs/WIRELESS_V4_HARDWARE_GATES.md) prohibit platform activation
until supported SDK hardening, storage, provisioning and real-board checks pass.
Review v4 changes for dual-contribution ONLINE gates, canonical transcript
coverage, stale worker/cancellation isolation, fragment bounds, replay/nonce
safety and secret cleanup. The storage self-HMAC is corruption detection; it
does not authenticate storage against an attacker able to replace its PSK.

The Teensy 4.0 owns rendering and behavior. The helmet ESP32 authenticates and
validates wireless input before forwarding bounded COBS/CRC16 frames over the
Serial1 link (pins 0/1, 115200 8N1). The explicit `PROTOGEN_UART_AUTH` build
feature adds per-helmet HMAC-SHA-256 authentication in both UART directions;
disabled development builds retain a physically trusted wire boundary. Local controls and rendering
continue when the ESP32, paw, radio, or session fails.

The v3 wireless design mitigates passive packet sniffing and reading command or
battery/status contents, unauthenticated command injection, replay, trivial
factory-MAC-based long-term tracking, ordinary Wi-Fi discovery, and malicious
malformed packets. Knowing that ESP-NOW is used grants no command authority.
Normal traffic is native encrypted ESP-NOW unicast plus independent AES-256-GCM,
with directional HKDF-SHA-256 keys, fresh authenticated sessions, and a 64-record
replay window. Every operational radio payload is 128 bytes. No AP, SSID,
network scans, IP interface, or raw-frame disguise is used.

Pairing requires a local gesture on **both** devices and comparison of all six
LED authentication digits, followed by a second local confirmation on both.
Ephemeral P-256 ECDH and commit/reveal bind random contributions before disclosure.
A mismatch, absent confirmation, or timeout must never be overridden. A nearby
active attacker can disrupt pairing and has a roughly one-in-a-million chance
of an undetected numeric match per properly compared attempt. Rate limiting is
not a replacement for checking the code. The pairing secret is never sent in
plaintext. Replacing a lost device requires deliberate re-pairing of both units.

This system does **not** prevent detecting 2.4 GHz RF energy, sophisticated RF
fingerprinting, traffic timing analysis, correlation during a rendezvous/session
transition, powerful broadband jamming, physical compromise of unlocked
development hardware, or attacks against undiscovered ESP-IDF/hardware
vulnerabilities. Temporary identities reduce stable address exposure; they do
not guarantee unlinkability. Every reconnect uses fresh authenticated ephemeral
P-256 ECDH and mutual new-key Finished confirmation. Later extraction of the PSK
alone cannot derive past completed-session traffic keys, assuming secure entropy,
correct primitive implementations and effective ephemeral-key destruction. This
is not protection against endpoint compromise during a session, side channels or
RAM remanence. An extracted PSK can authorize future impersonation until re-pairing. Historical v2 recordings
retain their original PSK-extraction exposure; v3 cannot retroactively protect them.

Sessions rekey at 15–17 minutes, 1,000,000 authenticated records or near sequence
exhaustion; numbers never reset under deliberately reused GCM keys. Explicit
security states forbid command dispatch before ONLINE. Bounded shared crypto
budgets and 250 ms temporary backoff mitigate CPU exhaustion without permanent
lockout; sustained RF flooding can still deny service. Saturating non-secret
`SECURITY STATUS` diagnostics do not alter authentication. Atomic storage fails
closed on invalid records; CRC is corruption detection, not malicious-write
protection. Physical power-cut qualification of ESP-IDF NVS remains outstanding.

## UART keys and production provisioning

The normal Teensy 4.0 cannot provide ESP32-style hardware-backed secret protection.
The optional UART secret resides in each controller's firmware (encrypted ESP32
flash in qualified production, ordinary Teensy flash). It protects unintended or
injected serial traffic under the assumed secret-protection model; extraction or
reflash of the ordinary Teensy defeats it. Never use a universal UART secret.

Private per-helmet `ProtogenUartKey.h` files are ignored by Git; the explicitly
public placeholder fixture remains tracked for regressions. Diagnostics must
report only non-secret counters/status. Ordinary Teensy flash remains extractable
with physical access: obfuscation or storing another software wrapping key in the
same image would not remedy that risk. Re-pair/re-provision after a suspected
extraction; keep UART keys distinct from wireless pairing keys and from other helmets.

Development uses recoverable ordinary NVS and intentionally lacks irreversible
lockdown. Production requires pre-provisioned hardware-backed encrypted NVS,
Secure Boot v2, flash encryption in release mode, signed firmware, anti-rollback,
and a qualified update/recovery process. See
[production provisioning](docs/PRODUCTION_PROVISIONING.md). Firmware never burns
security eFuses or generates missing NVS hardware keys. No cryptographic keys,
full nonces, pairing secrets, factory MACs, or authentication codes are logged.

[Protocol details](docs/WIRELESS_PROTOCOL.md) and
[release validation](docs/WIRELESS_VALIDATION.md) describe the implementation
and remaining physical release gates. This is not a claim of invisibility,
unjammability, or immunity to compromise.

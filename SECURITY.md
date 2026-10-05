# Wireless security boundary

The Teensy 4.0 owns rendering and behavior. The helmet ESP32 authenticates and
validates wireless input before forwarding bounded COBS/CRC16 frames over the
physically trusted Serial1 link (pins 0/1, 115200 8N1). Local controls and rendering
continue when the ESP32, paw, radio, or session fails.

The v2 wireless design mitigates passive packet sniffing and reading command or
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
not guarantee unlinkability. Session PSK authentication does not provide forward
secrecy against later extraction of the pairing secret and recorded reconnects.

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

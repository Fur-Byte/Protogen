# Experimental hardware-independent Protocol v4 core

This directory is **not** an ESP-IDF component and is never selected by the
production v3 build. Compilation with `ESP_PLATFORM` is explicitly denied.

```sh
bash scripts/test-v4.sh
python3 scripts/check-v4.py
bash scripts/test-v4.sh --qualification
python3 scripts/check-v4-resources.py
```

The host build uses pinned ESP-IDF Mbed TLS/PSA and vendored mlkem-native v2.0.0,
with ML-KEM-1024 and KeyGen PCT. Deterministic entropy exists only in the test
simulator; `HostCrypto` uses PSA secure randomness. No upload command exists.

See the [protocol](../../../docs/WIRELESS_V4_PROTOCOL.md),
[implementation](../../../docs/WIRELESS_V4_IMPLEMENTATION.md),
[validation](../../../docs/WIRELESS_V4_VALIDATION.md), and
[future hardware gates](../../../docs/WIRELESS_V4_HARDWARE_GATES.md).

S31 activation is blocked on an officially supported SDK with reviewed hardening.
No production FreeRTOS task, ESP-NOW driver, secure-storage backend, GPIO/eFuse
code or Secure Boot/flash-encryption activation is implemented here.

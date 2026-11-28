# ESP32-C3 helmet and paw firmware

> **Target plan (2026-10-06):** ESP32-S31 is the intended final wireless MCU
> for helmet and Paw; C3/v3 is retained as legacy development/regression.
> The active C3 development firmware uses v3. Public S31 preview builds and a 48 KiB
> ML-KEM-1024 worker have static resource evidence, but the supported-stable SDK
> and its memory-protection implementation block production S31 integration.
> The isolated [experimental v4 core](../../docs/WIRELESS_V4_PROTOCOL.md) is host-tested and is never
> selected by production builds. Physical qualification is separately pending. See the
> [S31 preflight](../../docs/WIRELESS_S31_PREFLIGHT.md) for evidence and remaining work.

Helmet: ESP32-C3-DevKitM-1. Paw: Seeed XIAO ESP32-C3. Teensy 4.0 remains the
behavior/rendering controller; the helmet ESP32 authenticates wireless commands
before the existing 115200 8N1 COBS/CRC16 Serial1 bridge (Teensy pins 0/1).

Normal radio mode is encrypted ESP-NOW unicast plus independent AES-256-GCM,
128-byte application envelopes, fresh ephemeral P-256 ECDH and HKDF-SHA-256 session keys, mutual Finished, replay checks,
and locally administered rotating identities. No SoftAP, SSID, scan, Wi-Fi
connection, IP interface or custom raw-frame disguise. See
[protocol](../../docs/WIRELESS_PROTOCOL.md) and [security](../../SECURITY.md).

## Build and tests

Pinned: PlatformIO 6.2.0, espressif32@7.1.3, ESP-IDF 6.1.0, Mbed TLS 4.1.0.

```sh
python3 scripts/install-toolchains.py
source build/toolchains/environment.sh
python3 scripts/build-esp32.py
bash scripts/test-wireless.sh
```

`PIO` can select another PlatformIO executable. Generated configurations and
artifacts live under `build/`; archive generated sdkconfigs before regenerating
changed defaults. The wrapper records artifact hashes, sources and resolved
security settings. DTLS tests cover only the retained historical adapter.
[Validation](../../docs/WIRELESS_VALIDATION.md) distinguishes host evidence from
real-radio qualification. Builds/tests never upload firmware or burn eFuses.
The [developer guide](../../docs/DEVELOPMENT.md) lists cross-target and focused checks.

## Pair without USB

Fit the mandatory buffered pairing LEDs described in
[hardware wiring](../../docs/HARDWARE_WIRING.md). Compare them with the devices
near each other, preferably off-body during first pairing.

1. After boot, hold helmet BOOT for 2 s and all three paw buttons for 3 s. Release
   both. LEDs rapidly blink during the 60 s pairing window.
2. Each LED shows the same six decimal digits: one long synchronization pulse,
   then six groups of short flashes; zero is ten flashes. Compare all groups,
   including leading zeros. Allow a complete cycle on each device.
3. If all digits match, hold each device's pairing gesture again for 1 s. Both
   physical confirmations are required. If any digit differs, do not confirm;
   wait for expiry and start a new attempt.
4. Devices exchange authenticated confirmation, store the unique pairing secret,
   automatically leave pairing, and reconnect. Future boots/reconnects need no
   pairing gesture. Release all paw buttons before using a reconnected link.

Never hold helmet BOOT while resetting; that selects the C3 bootloader. USB-only
provisioning and the console `PAIR` command were removed. The old `pair-paw.py`
entry point only prints these instructions and never accesses USB or keys.

To erase a pair without USB, hold the local pairing gesture continuously for
**12 s**. The unit clears its record and restarts. Replacing/retiring a peer
requires physical pairing on both devices. Interrupted storage may leave one
side paired and the other unpaired; start a fresh attempt on both. Old v1 USB
records are rejected; pair again and reapply any peripheral calibration.

## Paw operation and failures

Release either navigation button to cycle through the six original expressions.
Hold both navigation buttons for 1.2 s to toggle sleep. The boop button requests
the original 1200 ms expression.

The paw can queue four recent intents. An intent expires after 300 ms. Commands
use fresh Teensy tickets. Only a Teensy execution ACK confirms execution. A radio
ACK confirms delivery at the radio layer.
Gestures request fresh status immediately, with 100 ms active polling and a
1.5 s tail. Idle keepalives are jittered 3–5 s, not 500 ms. Link loss is detected
at 12 s; Teensy paw-online clears within another 2 s. Local buttons, rendering
and safe current state continue. Radios try keyed channel recovery and fresh
sessions; no usable spectrum means local-control fallback.

### Peripheral status and calibration

Optional peripheral mask starts at zero: helmet SHT40=1, INA228=2, SSD1306=4,
case NTC=8; paw protected cell divider=2. Calibration is required for usable
energy estimates. INA228 uses the existing 15 mΩ shunt configuration. Cell
voltage is not a promised percentage; energy percentage can be unknown after
reset/gaps/sensor faults. Wireless battery/status packets use the same encrypted
128-byte envelope as commands.

### Local console commands

`SECURITY STATUS` reports non-secret security state, counters,
session age, record count, renewal timing and channel. `INFO` reports role/pair/connection/features/faults, without a MAC
or keys. `CONFIG mask usable_energy_mwh`, `FULL` and `FORGET` require the local
physical unlock. CONFIG and FORGET restart. FULL requires a valid, calibrated
energy monitor and an actually full bank. No console command enters pairing or
confirms the authentication code remotely.

## Storage and release

Development is recoverable and uses ordinary NVS. Production needs explicit
hardware-backed NVS, Secure Boot, release flash encryption, signed firmware and
anti-rollback provisioning; see
[production procedure](../../docs/PRODUCTION_PROVISIONING.md). No automatic eFuse
burns. Hardware security, RF discoverability, MAC-change races, antenna behavior,
power, range and LED readability remain bench release gates. Do not treat a
successful compilation or host test as their verification.

## v3 hardening and authenticated UART

Every new radio session performs fresh PSK-authenticated ephemeral P-256 ECDH,
then directional new-key Finished and a paw Commit acknowledgement. Only a
confirmed ONLINE session forwards actions. Operational packets remain 128 bytes.
Version 2 radio traffic is rejected without downgrade; intentional upgrades of
both peers can retain a valid schema-2 pairing record.

Rekey after randomized 15–17 minutes, 1,000,000 authenticated records, or sequence
safety limit. Token buckets limit ECC, attempt creation and handshake verification;
invalid authentication backoff lasts 250 ms and clears automatically. Idle timing,
MAC rotation, keyed channel recovery and local Teensy behavior remain intact.

The optional `PROTOGEN_UART_AUTH` build feature authenticates both Serial1
directions with per-helmet HMAC-SHA-256 while retaining COBS/CRC16 and the Teensy
execution gate. A private provisioning header is required on both controllers:
see [provisioning](../../docs/PRODUCTION_PROVISIONING.md). Ordinary Teensy firmware
flash cannot protect this secret against physical extraction/reflash. Disabled
development builds retain the trusted-wires boundary; enabled builds cannot fall
back to CRC-only commands. No universal secret is provided.

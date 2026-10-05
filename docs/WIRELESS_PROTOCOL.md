# Paw / helmet protocol, version 1

Implementation: `firmware/esp32/src/{main.cpp,SecureSession.cpp}` and `firmware/PROT_OS7_MAX7219/src/HelmetProtocol/HelmetProtocol.h`. This is a bounded application protocol using a standard cryptographic transport. UART CRC is for corruption detection; it provides no authentication by itself. The internal UART is a physically trusted, isolated 3.3 V connection.

## Provisioning and transport

Two board roles: helmet=1, paw=2. Pair through physically unlocked USB consoles using `scripts/pair-paw.py`; no keys are hardcoded. Each pair has an independently generated DTLS PSK (32 bytes), ESP-NOW LMK (16 bytes) and PMK (16 bytes). A peer MAC and channel 1–11 are stored in the same versioned CRC-protected NVS blob. Random generation and the entire DTLS cryptographic handshake/key derivation are provided by existing OS/PSA/Mbed TLS facilities.

ESP-NOW uses encrypted unicast only, fixed peer whitelist, same channel, maximum DTLS datagram 240 bytes. Receive/send callbacks do only fixed queue copies and completion flags. Eight receive slots bound radio memory. DTLS BIO callbacks never wait for RF; send backpressure returns WANT_WRITE and receive starvation returns WANT_READ. Main service performs at most four handshake/read steps per 5 ms turn. An active `ssl_write` retains the exact same bytes and length until the library completes it. Application TX has four fixed slots.

Only `TLS_PSK_WITH_AES_128_CCM` over DTLS 1.2 is permitted. Authentication uses the full CCM tag. Server cookies bind to the configured peer identity; anti-replay is required at compile time and enabled through the library API. TLS 1.3, early data, resumption tickets and renegotiation are not used. A handshake attempts for at most five seconds; errors/timeouts clear commands, reset the library session and back off 500 ms. Authenticated application traffic must recur within two seconds. Old-session records and duplicate DTLS record sequence numbers are handled by Mbed TLS, not project cryptography.

PSK-only DTLS has no forward secrecy, and keys are in ordinary local NVS. This protects against nearby unauthorized transmitters/replays, not extraction from a physically stolen MCU or deliberate RF jamming. Reprovision both boards after loss. MCU secure boot/flash encryption are outside the current prototype.

## Binary layout

All multibyte application fields are explicit **little-endian**. No struct padding is transmitted.

| Offset | Length | Field |
| --- | ---: | --- |
| 0 | 1 | Magic 0x50 |
| 1 | 1 | Version 1 |
| 2 | 1 | Kind: Command=1, Status=2, Ack=3, Hello=4, Heartbeat=5 |
| 3 | 1 | Payload length, 0–72 |
| 4 | 4 | Application sequence |
| 8 | 4 | Teensy boot epoch, nonzero hardware-random value |
| 12 | 4 | Teensy freshness ticket |
| 16 | N | Payload |
| 16+N | 2 | CRC16-CCITT-FALSE; initial 0xffff, polynomial 0x1021 |

Maximum raw frame is 90 bytes. Over UART, COBS encode it and append a zero delimiter; maximum buffer is 92 bytes. Partial UART frames time out at 100 ms and are discarded through a delimiter. Over DTLS, carry the raw frame as one application record. Exact length/version/kind/CRC checks precede field processing. No network frame is interpreted as USB text or MAX7219 data.

## Payloads and authority

**Status — 20 bytes, Teensy → helmet → paw, approximately every 100 ms.**

| Offset | Length | Meaning |
| --- | ---: | --- |
| 0 | 4 | Current bridge session ID (zero until Hello accepted) |
| 4 | 1 | Selected expression: happy=0, surprised=1, angry=2, vwv=3, sleepy=4, wink=5 |
| 5 | 1 | Flags: sleep=1, visor visible=2, booping=4, speaking=8, mic enabled=16, display ready=32, paw online=64 |
| 6 | 1 | Features: Hall=1, physical boop=2, microphone=4, wireless=8, protected dock=16 |
| 7 | 1 | Physical boop mode: held=0, timed=1 |
| 8 | 2 | Latest physical IR ADC reading (0xffff may denote unavailable when source changes) |
| 10 | 2 | Faults: dock voltage/initialization=1, ADC1 failure=2, microphone failure=4 |
| 12 | 4 | Microphone sample drops |
| 16 | 2 | Remaining boop time limit, milliseconds |
| 18 | 1 | Configuration schema (2) |
| 19 | 1 | Reserved, zero |

**Hello — 4 bytes, helmet → Teensy only.** Payload is the new nonzero hardware-random bridge ID. It references a recent status epoch/ticket. Each new successful DTLS connection generates a new bridge ID; Teensy resets application dedup/rate state only when accepting a different ID. Repeated Hellos for the same bridge do not reset an earlier command result. Paw cannot forward a Hello to change this identity.

**Command — 8 bytes, authenticated paw → helmet → Teensy.**

| Offset | Length | Meaning |
| --- | ---: | --- |
| 0 | 4 | Current bridge ID |
| 4 | 1 | Action: expression=1, boop=2, sleep=3, mic enable=4 |
| 5 | 1 | Value: expression 0–5; sleep/mic 0–1; boop zero |
| 6 | 2 | Boop duration 100–6000 ms; zero for all other actions |

Command sequences start at 1 per bridge and strictly increase; sequence zero is rejected. The three-button controller exposes expression next/previous, sleep/wake combo and 1,200 ms original-art boop. Mic enable is reserved in the implemented command interface; the paw UI does not add a fourth button.

**Ack — 5 bytes, Teensy → helmet → paw.** Bridge ID plus result: Accepted=0, Duplicate=1, BadSession=2, Expired=3, Invalid=4, RateLimited=5, Unavailable=6. Sequence identifies the original command. The radio's MAC send callback is not this ACK. The helmet never acknowledges an action as executed before Teensy reports its result.

**Heartbeat — 4 bytes, paw → helmet → Teensy, about every 500 ms.** Current bridge ID, current epoch and recent ticket required. A heartbeat is sent even when buttons are held during reconnect. Link timeout changes status; it does not blank or reset an otherwise working face.

## Freshness, retry and reconnect

The Teensy keeps four recent status tickets and their issue times. It accepts a new action only within **300 ms of that ticket's issue in Teensy time**, with correct epoch/bridge and a valid payload. This rejects delayed commands even if RF or UART queues were congested. Paw only forms commands from recently received status, keeps one unacknowledged action, retries the same command/ticket about every 100 ms, and stops after 300 ms. No application command is buffered across disconnect.

The exact last accepted command can return its cached ACK after the expiry window without executing again. A reused sequence with changed ticket/payload is invalid; older sequences are expired. Failed execution results, such as boop while hidden/asleep or mic unavailable, are cached too, so retry cannot change their result into a new action. Twenty accepted commands per one-second window is the upper rate bound.

On timeout/reset, DTLS and application queues are cleared, a fresh handshake is required, and a fresh bridge is established through UART. Paw must observe all buttons released before control resumes. A new epoch/status after a Teensy reboot discards an old pending command and sequence state. The outward face retains the current selection during radio loss; a Teensy reboot uses its configured startup expression as usual.

## Evidence and tests

`scripts/test-dtls.sh` compiles the same SecureSession adapter against the ESP-IDF-bundled Mbed TLS 4 library on the host, using portable software PSA entropy/crypto. It tests cookie handshake, a lost ClientHello, nonblocking write retry, valid payload decryption, tampered tags, record replay, old-session ciphertext, and wrong PSK rejection. This is real library execution, not a fake encryption stub. The host configuration intentionally has the same DTLS version/suite/key/tag/replay requirements; ESP builds separately verify the embedded configuration.

`scripts/test-firmware.sh` adds 20,000 framed packet round trips, truncation/corruption/overflow, ticket rollover/expiry, epoch/session mismatch, payload dedup, rate limiting, schema migration and actual Teensy command-path behavior. `scripts/check-extensions.py` compiles five real Teensy feature combinations, including all optional hardware together. Both C3 targets compile separately.

Physical release still requires two actual radios: wrong-pair attempts, captured/replayed RF, deliberate disconnects/reboots, no held-button action on reconnect, lost ACKs, congested UART, channel interference and latency/energy measurements. Host tests do not establish RF range, CCMP implementation behavior in every failure, antenna performance, or on-body endurance.

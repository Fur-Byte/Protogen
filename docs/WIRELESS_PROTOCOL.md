# Low-profile wireless protocol v2

Both ESP32-C3 targets use ESP-IDF 6.1.0 / Mbed TLS 4.1.0 PSA primitives. The old
DTLS adapter remains only for historical regression tests; it is not linked into
firmware. Teensy behavior, command IDs, Serial1 pins 0/1, 115200 8N1, COBS, CRC16,
boot epochs, freshness tickets, deduplication and execution ACKs are preserved.

## Radio boundary

Only Wi-Fi STA mode is enabled; SoftAP is excluded from the compiled driver.
There is no default STA/AP network interface, DHCP/IP service, Wi-Fi connect,
credential configuration, SSID advertising, scan, or BLE service. The first MAC
is assigned **before** Wi-Fi starts, using the documented bootloader entropy
source before Wi-Fi or ADC initialization; that source is disabled before Wi-Fi
starts. Subsequent RNG calls use active Wi-Fi hardware entropy. ESP-NOW normal operation adds exactly one
peer with `encrypt=true`, a derived 16-byte LMK, and a derived 16-byte PMK.
Broadcast peer entries exist only in bounded physical pairing and reconnect
rendezvous states. They carry public pairing commitments/reveals or authenticated
session control, never commands, telemetry, or a secret.

ESP-NOW's CCMP protection is independent of application encryption. Normal Data
frames are accepted only as unicast from the current peer address and after all
application authentication, replay, codec and directional allowlist checks.
A known/spoofed MAC confers no authority. Native send completion is never an
execution ACK. No custom raw 802.11 frame construction is used.

## Physical wireless pairing

After boot: hold helmet BOOT for 2 s and all three paw buttons for 3 s. Do not
hold BOOT across reset. Release both gestures. Each enters a 60 s window with
rapid LED blinking, suspending wireless actions but preserving local rendering.
Unpaired devices otherwise transmit nothing; they do not advertise permanently.

Each creates a fresh locally administered unicast address, ephemeral PSA P-256
keypair, and 16 random bytes. First broadcast a SHA-256 commitment to protocol
version, role, temporary address, public key and random contribution. Lock the
first opposite-role commitment for this attempt; send public-key/random reveals
only to that candidate. Verify the reveal against the locked commitment, then
perform PSA ECDH. The canonical transcript is paw material then helmet material.
HKDF-SHA-256 with its SHA-256 digest as salt and separately labelled information
derives a 32-byte pairing secret and the short authentication code (SAS).

Compare **all six decimal digits** on both LEDs. One long pulse marks a repeat;
each of six groups has short flashes for its digit, zero represented by ten
flashes, with a longer inter-digit gap. Leading zeros matter. Start pairing with
the devices together and observe a complete repeat on each. Codes are never
printed. If codes differ, let the window expire and start again; do not confirm.
The commitments prevent choosing a random contribution after seeing the other's
reveal; checking the SAS provides the missing first-use identity authentication.

If codes match, hold each gesture again for 1 s after release. Local confirmation
is independent on each unit and cannot be sent by radio or console. Exchange
transcript-bound HMAC-SHA-256 confirmations with the pending secret. Neither unit
stores until **both** confirmations are present. NVS uses one atomic versioned
blob; no partial multi-key record. Repeat the confirmation for 2 s after saving,
then leave pairing and automatically establish a session. Ephemeral private keys
are destroyed after agreement; the stored secret identifies the paired opposite-role peer by possession rather than a permanent MAC. Temporary secrets are wiped on timeout/completion.

Pairing is deliberately not a distributed atomic storage transaction. Power loss
or a storage error can leave only one unit committed; this fails disconnected,
never trusted. Restart physical pairing on both units to replace the secret.
A window timeout before committing preserves the previous pair. On a mismatched
SAS, neither unit should be confirmed. A first-commit attacker can cause denial
of this attempt, but cannot cause a persistent lockout or pairing acceptance.

To erase without USB, hold the local pairing gesture continuously for **12 s**;
that unit clears its pair and restarts. Erase/re-pair both units when retiring a
peer. Optional physically unlocked `FORGET` does the same locally. `PAIR` USB
key injection was removed and `pair-paw.py` only prints migration guidance.
Old v1 factory-address/USB records are intentionally not accepted; pair again.

## Authenticated reconnect and session keys

Only the paw initiates. HKDF derives a dedicated rendezvous HMAC key from the
pairing secret, using `protogen/v2/rendezvous/authentication` as context.

1. Paw broadcasts a fresh 32-byte challenge and its fresh temporary address,
   authenticated by HMAC-SHA-256. No stable pair identifier is included.
2. Helmet validates the HMAC, binds the observed source to the supplied address,
   creates its own fresh 32-byte challenge and responds unicast. The response
   authenticates both challenges and both temporary addresses.
3. Paw verifies its current challenge and both addresses, derives material, and
   sends a unicast HMAC finish bound to the same transcript. Helmet accepts only
   the current outstanding response, not a previously seen finish.
4. Both stop ESP-NOW/Wi-Fi, install derived session MACs, restart STA and ESP-NOW,
   and add only the encrypted session peer. Operations require valid AES-GCM
   data from that session. Paw transmission of the finish is not proof of peer
   readiness; if it is lost, the link times out and retries a fresh handshake.

Canonical HKDF salt: SHA-256(version, reserved zero, paw challenge, helmet
challenge, paw temporary MAC, helmet temporary MAC). All keys use distinct
`protogen/v2/session/...` labels: `paw-to-helmet/aes-gcm`,
`helmet-to-paw/aes-gcm`, `id`, `recovery`, `esp-now/lmk`, `esp-now/pmk`,
`identity/paw`, `identity/helmet`. Direction keys are 32 bytes, ID 8 bytes,
recovery key 32 bytes, LMK/PMK 16 bytes, each MAC 6 bytes. Challenge equality alone
never authenticates a peer. Invalid traffic never updates liveness.

No session is resumed. Boot, reset, loss or renewal generates fresh challenges,
keys, replay state, MACs, and a fresh helmet-to-Teensy bridge ID. Paw initiates
renewal while idle after 15–17 minutes with an encrypted rekey request; both
enter recovery. Lost rekey requests fall back to timeout recovery. Thus idle
sessions do not leave a MAC indefinitely unchanged. Later pairing-secret
extraction can expose captured reconnects; reconnect does not claim forward
secrecy. Nonce safety relies on hardware randomness and no rollback of live
session counters; sessions are never persisted.

## Fixed encrypted data envelope

All multibyte values are little endian; no packed C struct is sent.

| Offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 1 | Magic 0x70 |
| 1 | 1 | Protocol 2 |
| 2 | 1 | Data type 7 |
| 3 | 1 | Sender role (helmet 1 / paw 2) |
| 4 | 8 | Session ID |
| 12 | 8 | Directional monotonically increasing sequence |
| 20 | 92 | AES-256-GCM ciphertext |
| 112 | 16 | Full GCM authentication tag |

The full 20-byte header is AAD. Plaintext: two-byte length followed by the existing
raw HelmetLink v1 frame (18–90 bytes, including CRC16); unused space is filled
with hardware randomness **before encryption**. Native CCMP overhead is outside
this 128-byte application payload. BOOP, navigation, mic/sleep, ACK, heartbeat,
battery, status request and response all have this same radio payload length.

GCM nonce = `0x70, version, sender-role, 0, sequence-as-8-bytes`. Direction keys
are separate and unique per fresh session. Sequence starts at one and stops at
2^63−1; no wrap. A retry at the transport layer reuses the exact ciphertext;
a new application retry receives a new GCM sequence. Replay prefilter rejects
zero, >2^63−1, old session IDs, duplicates, and age >=64. Decryption, CRC/length
and command-field checks precede marking the replay bitmap and dispatch. The
Teensy separately rejects old command sequences, modified retries and stale
epoch/tickets; authenticated out-of-order commands cannot execute backwards.

Control frames are also 128 bytes: magic/version/type/role, bounded control body
at offset 4, zero reserved padding, HMAC at offset 96 (32 bytes) for reconnect
and confirmation. Types: Hello 1, Response 2, Finish 3, PairCommit 4, PairReveal 5,
PairConfirm 6. Commitment: 32 bytes. Reveal: public key 65, random 16, source MAC
6. Confirmation: transcript hash 32, physically confirmed flag 0/1. Hello:
challenge 32, source MAC 6. Response/Finish: paw challenge 32, helmet challenge
32, paw MAC 6, helmet MAC 6. No operational parser handles control frames.

Wireless-only Status body variants: paw length 0 requests fresh status; length 1
with byte 1 requests rekey; length 4 reports cell millivolts (0 unknown, max 6000)
and sensor faults. Helmet length 24 extends the unchanged 20-byte Teensy status
with remaining battery percent (0xffff unknown) and sensor faults. The original
20-byte status is also accepted. These variants never pass through UART as
commands. Unsupported kinds/actions/values are rejected before forwarding.

## Identity, channel recovery and timing

Session MACs are derived from the authenticated transcript. Set bit 1 of the
first octet (locally administered), clear bit 0 (unicast). Reserve opposite last
address bits for the two roles so they cannot collide with one another. There
are 45 random address bits per role: collision with unrelated equipment remains
possible, not detectable with certainty without extra RF discovery. Pair-specific
keys and randomness make accidental collision unlikely; a new handshake changes
addresses on recovery. Factory MACs are neither required nor stored nor logged.

Recovery starts with random addresses and a keyed permutation of channels
1/6/11. Derive the permutation with HKDF label
`protogen/v2/recovery/channel-order`. After link loss, use the last authenticated
session's recovery key for 15 s, then the pairing secret for reboot-compatible
fallback. Boots start with the pairing secret. During a prolonged outage the temporary
rendezvous MAC rotates after 30 s (up to 4 s grace for a pending handshake),
without resetting the channel schedule. An absent peer therefore does not leave
a disconnected transmitter indefinitely on one address. The paw sweeps every 400 ms while
the helmet dwells 2400 ms. Asymmetric dwell guarantees channel overlap without
shared boot time or an RTC, provided at least one configured channel is usable;
repeated pending handshakes freeze the channel for at most 4 s. The handshake
pins the successful channel. Rates and dwell times are centralized in
`WirelessConfig.h`; no currently failing channel message is needed to coordinate
fallback. Some permutations necessarily resemble 1/6/11: this is a keyed order,
not an anti-jamming or secret-frequency claim. Native driver channel changes and
radio stop/start races require physical qualification.

Idle paw keepalives are encrypted status requests every random 3–5 s, with one
fresh status response. Helmet status changes may also be pushed (selection,
flags, features and faults); changing ADC readings alone do not cause idle RF.
Optional paw battery telemetry occurs every 20 s. Button gestures request status
immediately; poll every 100 ms during use and for 1.5 s afterwards. Pending
commands use unchanged ticket/sequence retries at 100 ms within the 300 ms
Teensy freshness limit. A four-intent queue bounds rapid presses; aged intents
expire instead of executing late. A cold first action may wait one status
round-trip. No stale held-button action executes after reconnect: release all
buttons to arm.

After 6 s without authenticated receive, use 100 ms probes until the 12 s
link-loss threshold. The helmet's **wired** 500 ms heartbeat preserves the
Teensy's existing 2 s UART timeout without RF traffic. On radio loss the helmet
stops that heartbeat; Teensy paw-online indication clears within another 2 s.
Rendering and local buttons continue in their current safe state throughout.
Antenna conditions, driver ACKs and latency affect recovery; 12 s sacrifices the
old 2 s disconnect display for quieter idle traffic. Usable spectrum loss causes
continued bounded recovery attempts and local-control fallback, not repeated
commands or frozen rendering. This does not defeat broadband jamming.

## Parser, resource and SDK boundaries

Wi-Fi callbacks only length-check and copy fixed frames/source/destination to an
8-slot queue. Main processing budgets 4 frames per 5 ms turn, receive AEAD at most
200/s, handshake HMAC at most 20/s and pairing ECDH at most once/s per candidate.
One candidate is pinned until the 60 s attempt expires. There is no persistent
attacker-triggered lockout. Four outgoing slots and one ciphertext retry slot
bound memory. PSA internals may allocate; application hot paths allocate no
variable-size memory. Length/version/role/session/sequence checks precede copies
or expensive operations. Sustained RF flooding can deny radio service but cannot
bypass authentication; Teensy remains independent.

ESP-IDF supports `esp_wifi_set_mac(WIFI_IF_STA, ...)` and STA ESP-NOW on these C3
boards. MAC rotation is done with Wi-Fi stopped; ESP-NOW is deinitialized and
peers/queued frames are rebuilt. This avoids changing live peer addresses and
CCMP counters in place. A send-completion failure triggers a radio rebuild after
4 s. STA power save remains off for reception reliability; battery savings from
RF silence are not a validated endurance claim.

[Security scope](../SECURITY.md), [production steps](PRODUCTION_PROVISIONING.md),
[tests and physical qualification](WIRELESS_VALIDATION.md).

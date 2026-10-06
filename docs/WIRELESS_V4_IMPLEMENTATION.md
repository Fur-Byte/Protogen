# Experimental v4 core implementation

**EXPERIMENTAL / HARDWARE-INDEPENDENT / NOT ACTIVE PRODUCTION FIRMWARE.**
Production C3/v3, Teensy rendering, UART behavior and all production build
selection remain unchanged. This core is an independent host library under
`firmware/esp32/protocol_v4/`, built only by `scripts/tests/v4/CMakeLists.txt`.
No active ESP-IDF component discovers it. `Interfaces.hpp` rejects compilation
with `ESP_PLATFORM`; `S31Adapter.hpp` also rejects explicit S31 activation.
Removing these guards requires deliberate future reviewed platform work.

The byte-level protocol, roles, KDF and state diagram are specified in
[WIRELESS_V4_PROTOCOL.md](WIRELESS_V4_PROTOCOL.md). Test results and the file
manifest are in [WIRELESS_V4_VALIDATION.md](WIRELESS_V4_VALIDATION.md).

## Components and ownership

| Component | Responsibility |
|---|---|
| `Core` | one candidate, explicit state, local pairing confirmation, authenticated session, Finished/Commit, renewal, dispatch gates |
| `Reassembly` | one fixed object, strict metadata, authentication/commitment, duplicates and short expiry |
| `Derivation` | explicit canonical transcript, approved one-step SHA3 KDF, separated HKDF outputs |
| `Traffic` | 128-byte AES-256-GCM, separate directions, 64-record replay window, conservative limits and packet allowlist |
| `Storage` codec | fixed v4 hybrid-provenance record, corruption rejection and explicit legacy repair |
| `SlotWorker` | one bounded KEM job/result, lifecycle and cancellation generations |
| `NativeKem` | exact pinned ML-KEM-1024 operations; no custom cryptographic internals |
| `HostCrypto` | real PSA/Mbed TLS primitives and upstream SHA3 reference adapter, host only |

Abstract interfaces cover crypto, randomness, time, transport, persistence,
worker execution, secure wipe and local hooks. Transport must purge old peer
keys and pending handshake frames on identity changes/installation and attest
native encrypted unicast on operational reception. A future adapter must not
assert CCMP merely because application GCM verified. Core never creates tasks,
touches pins/eFuses or dispatches from a worker callback. Normal user commands
use only the established GCM session.

`HostCrypto` compiles the unchanged v3 `WirelessCrypto.cpp` strictly as a primitive
provider; it does not call v3 pairing, handshake, storage or wire code. P-256
handles are PSA volatile keys with checked export/agreement and destruction.
The shared HelmetLink codec and directional packet allowlist preserve the Teensy
interface. The core itself has no dependency on v3 constants.

## Pinned ML-KEM provenance

* Upstream: [pq-code-package/mlkem-native](https://github.com/pq-code-package/mlkem-native).
* Version **v2.0.0**, commit `d1b2fe782888bdb761a50336012923180be7f502`.
* License: **MIT** selected from upstream's Apache-2.0 OR ISC OR MIT offer.
  No GPL implementation is linked or vendored into this core.
* Archive SHA-256:
  `7c7a10464ba3c62d5657a70da495539ab7f28e464cff80eb9d8173e2bc91c4d3`.
* Portable C arithmetic/FIPS202, parameter set 1024, default upstream stack
  allocation, key-generation pairwise consistency test **enabled**.
* Namespace `pqv4`, isolated C11 thread-local RNG adapter. The adapter refuses
  reentrant calls; production RTOS entropy/context will require its own reviewed
  implementation. TLS is a host mechanism, not proof of an S31 port.
* All **42** upstream source/license files are unmodified and individually
  SHA-256 pinned in `vendor/mlkem-native/PROVENANCE.json`, including `zetas.inc`.
  That source include was absent from the earlier 41-file measurement manifest;
  vendoring adds and pins the exact upstream include, without changing its contents.

Upstream's fixed-buffer APIs have no attacker-supplied length argument. The
worker checks exact lengths before invoking them. Decapsulation follows FIPS
implicit rejection: corrupted well-sized ciphertext can return success with a
different secret. Transcript/SAS/Finished catches disagreement. There is no
distinguishable ciphertext-validity response and no detailed radio failure log.
Upstream validation errors yield bounded generic candidate failure; non-sensitive
counters do not print keys, tags, nonces or ciphertext histories.

The backend's provenance, maintenance and security design take priority over a
smaller stack. The earlier worker report compared wolfCrypt, including its GPL
licensing consequences, and upstream's experimental arena configuration. Neither
is selected here. No lower parameter set or pre-standard Kyber is substituted.

## Worker contract and cancellation

The worker has fixed 6,336-byte payload storage: PK[1568], SK[3168], CT[1568],
SS[32]. Requests refer only to fixed buffers and are copied before submit returns.
One slot is the bounded queue; a second request is rejected while queued,
running, retiring or holding an unread result. Only KEYGEN, ENCAPS and DECAPS are
valid. Each request/result carries local generation and monotonic serial IDs.
No callback from a received radio frame directly executes a KEM routine.

Lifecycle:

```text
EMPTY → QUEUED → RUNNING → READY → EMPTY
                   └ cancel → RETIRING → EMPTY
QUEUED/READY cancellation → wipe → EMPTY
serial exhaustion → EXHAUSTED (fail closed)
```

Cancellation immediately invalidates the core's generation and candidate.
An already-running primitive is allowed to finish; its result is wiped and never
delivered. The old worker slot cannot be reused until that primitive returns.
A future RTOS adapter must serialize submit/claim/finish/poll/cancel, execute
outside its mutex, and join/retire the worker before destroying its storage.
It must never clear a buffer concurrently with a primitive writing it. The
portable `SlotWorker` is intentionally single-threaded; it is not a ready-made
thread-safe FreeRTOS task. The host simulator uses it outside `Core::tick()`;
the test worker injects delayed, failed and stale results into the real core.

The core compares the complete ticket and expected operation before copying any
result. Every stale result is wiped. Reset/reconnect invalidates transport
completion tickets as well. Only a bounded number of frames is serviced per
tick (four), and only one transmission is pending. ONLINE bypasses all PQ
reassembly/KEM processing and takes precedence over attacker handshake traffic.

## Protocol budgets and recovery

Budgets refill automatically with monotonic time; they are never reset by a
radio attacker or candidate restart. Initial capacities/refill periods are:

| Work | Capacity | One token per |
|---|---:|---:|
| candidate creation | 2 | 1,000 ms |
| KeyGen / Encaps / Decaps (separate) | 1 each | 1,000 ms |
| reassembly creation | 4 | 1,000 ms |
| structural-valid authentication work | 8 | 5 ms |
| authentication failures | 8 | 50 ms |
| P-256 work | 4 | 500 ms |
| hybrid derivation | 2 | 1,000 ms |

Frames are structurally screened before HMAC, and reconnect objects are
authenticated before KEM. Sender pacing is at least 10 ms between handshake
frames. Budgets are per endpoint, not proportional to an untrusted source count;
there is one expected peer/candidate. First pairing is deliberately unauthenticated
until SAS, so a nearby attacker can consume the bounded window. Session failure
clears the candidate and retries with 250-ms backoff; there are no multi-minute
punishments or permanent unauthenticated lockouts.

## Persistence and migration

Storage is an interface: the host uses a bounded recoverable store; a future
production adapter must enforce hardware-backed/encrypted storage policy before
starting the core. No production roots are generated or burned by this library.
Write success is followed by exact readback. A read/write failure destroys the
candidate, removes live authority for that failed candidate and enters recovery.
The interface must define real atomic write/rollback behavior during bring-up.

The 112-byte record is explicitly serialized:

| Offset | Field |
|---:|---|
| 0–3 | ASCII `PV4` then zero |
| 4 | schema 1 |
| 5 | protocol 4 |
| 6 | hybrid-established provenance 1 |
| 7 | role |
| 8–9 | LE16 parameter 1024 |
| 10–15 | reserved zero |
| 16–47 | pairing secret |
| 48–79 | pairing transcript hash |
| 80–111 | HMAC-SHA256(pairing secret, first 80 B) |

This HMAC detects accidental/torn corruption, but is **not** protection against
an attacker who can rewrite storage and replace both the PSK and tag. Endpoint
storage trust depends on the future encrypted/provisioned platform. A valid v3
record receives V3_REPAIR_REQUIRED and no imported secret; invalid records enter
recovery. No ML-KEM private key, raw shared secret or session key is persistent.

## Secret-lifetime audit

* P-256 handle: destroyed immediately after ECDH, or on generation failure,
  timeout, cancellation, reset and destruction.
* ML-KEM SK: worker-owned KeyGen output copied into one core candidate; original
  worker output wiped on poll. Core SK is wiped immediately after successful
  Decaps submission, then worker SK is wiped when Decaps completes/cancels.
* Raw ECDH/ML-KEM secrets: retained only until the complete transcript enables
  hybrid derivation, then wiped regardless of derivation success.
* Combiner/HKDF temporary buffers: securely wiped on success/error. The fixed
  derivation workspace is cleared on every candidate abandonment.
* Pairing candidate/SAS: wiped on failure/reset and after confirmed persistence;
  pairing confirmation keys exist only during the bounded grace interval.
* Session candidate/Finished/Commit/pre-auth keys: wiped after installation and
  every failure. Operational keys are wiped on renewal/reset/destruction.
* Reassembly and abandoned public objects: wiped on completion, expiry, conflict,
  restart, rotation and destruction, even though PK/CT are public material.
* Worker queued/ready/stale/failed outputs: wiped; running cancellation defers
  physical wiping until safe retirement, while authority is invalid immediately.

Host adapter wipes use volatile stores; upstream ML-KEM uses its reviewed
compiler-barrier zeroization. The wipe-observing tests exercise success, failed
jobs, timeout, malformed input, cancellation, stale completion and storage failure.
Neither the application nor the library can prove erasure of all compiler
registers/spills, allocator internals, caches or physical remanence. No claim of
perfect physical zeroization or MCU power/EM resistance is made.

## Telemetry and future platform

Saturating uint64 counters cover attempts/failures/timeouts, fragment rejection
and conflict, KEM operation failures, hybrid/Finished/Commit failures, replay,
downgrade attempts, stale results, cancellation, rate drops, renewals and recovery.
Counters expose no secret state. Storage provenance is a distinct safe status.

`S31Adapter.hpp` defines unsupported integration status and a metrics structure
whose validity mask separates missing hardware measurements from measured zero.
The future adapter must supply encrypted ESP-NOW transport, trusted entropy,
monotonic timers, temporary MAC/channel transitions, secure persistence and a
bounded worker task. It must verify supported SDK hardening and provisioning
before activation. Secure Boot, flash encryption, eFuse/HMAC roots, anti-rollback
and secure debug remain future platform responsibilities. Teensy continues all
behavior/rendering and local fallback; no PQ operation runs on Teensy.

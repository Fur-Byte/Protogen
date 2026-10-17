# COBS, wireless admission and experimental v4 review

Assessment: 2026-10-17. Scope: remediation brief sections 3, 10 and 13.
Production remains protocol v3 on the existing C3 builds. No packet format,
persistent record, crypto algorithm, key hierarchy or production-v4 guard changes.
This is a source review and host validation, not hardware or cryptographic certification.

## Confirmed defects and reproductions

**COBS output bounds: confirmed.** The original `capacity < n + 2` check
ignored code-byte reservations after each full 254-byte nonzero run, and its
addition could overflow `size_t`. Re-running the baseline encoder with 508
nonzero input bytes and a 510-byte stack output produced an ASan
`stack-buffer-overflow`, a one-byte write in `cobsEncode`. A test-first
insufficient-capacity assertion also failed at the 254-byte boundary. The fix
checks the overflow-safe minimum before reading input and checks every data,
code reservation and delimiter write. Null input is accepted only for length
zero; null output fails. A zero-length encoding remains `01 00`, preserving
existing UART bytes. Decode now rejects malformed pointer arguments and uses
subtraction-based bounds to avoid overflow. Failure returns zero; a partially
written output is discarded by callers.

**Broadcast Hello restarting an established session: not reproduced.** Existing
`Link::tick` already separates connected operation from recovery and refuses
broadcast session records. Its handshake already source-locks a candidate,
recognizes identical retransmissions, retains a recent-Hello hash cache across
restart and keeps authentication/ECC/creation budgets across protocol resets.
Replayed public Hello cannot install keys or reset the active handshake directly.

**Related active-session starvation: confirmed and fixed.** A unicast Hello
with the current peer's source MAC previously advanced `lastRxCheck` before
`Session::open` rejected its non-DATA type. A captured Hello placed ahead of a
valid command in the same receive budget therefore caused valid records to be
skipped. The new 1,000-command spoof/replay test failed its delivery assertion
before the fix. `Link` now rejects non-DATA before charging the active receive
authentication interval. This protects custom transports as well as ESP-NOW.
The actual ESP-NOW callback also screens operational broadcasts/control frames
before they occupy the shared eight-slot RX queue. Its native-session flag is
atomic because it is read in the Wi-Fi callback and written by configuration.
The callback guard is only structural admission; GCM, peer/source validation
and replay checks remain authoritative.

**Handshake response amplification: confirmed and fixed.** Repeated identical
captured Hello or Response previously set `nextSend = now`, allowing one cached
response every service tick instead of the configured 250-ms retry cadence.
The new test failed at the first pre-deadline `tick`. Identical retransmissions
remain idempotent and now retain the existing send deadline. Fresh state progress
still schedules its first response immediately. Pairing, authenticated packet
bytes, cache lifetime and replenishing budgets remain unchanged.

The bounded defenses cannot prevent RF jamming, native-driver resource pressure,
or a party with valid current traffic keys denying service. Captured initial
Hello replay protection has a bounded in-memory cache, not an indefinite
persistent replay ledger. A legitimate fresh offer and expiry/backoff permit
reconnection; there is no permanent unauthenticated lockout.

## Targeted v4 review

Read the complete application-owned `protocol_v4` interfaces, core, derivation,
fragments, traffic, storage, worker, host/native adapters and activation guards,
plus WIRELESS_V4_PROTOCOL, IMPLEMENTATION, VALIDATION, HARDWARE_GATES, PREFLIGHT
and the complete WIRELESS_S31_PREFLIGHT. Reviewed the primitive boundary and
existing deterministic/vector/adversarial harness. The vendor implementation
is retained unchanged; this review does not claim a fresh line-by-line audit of
all upstream ML-KEM internals or a formal proof.

| Requested property | Source evidence and review conclusion |
| --- | --- |
| Key derivation/domain separation | `Derivation.cpp`: both 32-byte contributions enter SHA3-256 one-step KDF with explicit counter, output length, domain and canonical transcript. HKDF salt is transcript hash; labels distinguish pairing, session, direction, Finished, Commit, radio keys and identities. All-zero inputs/root and invalid directional material fail closed. Existing independent Python/C++ vectors exercise exact bytes. No new defect confirmed. |
| Transcript binding | Six objects bind normalized headers, exact lengths, roles, mode, exchange ID, source identity, per-object hash and body in fixed order. Nonce commitments bind P-256 key, KEM-key hash and nonce before reveal. SAS and Finished bind the complete exchange. No new defect confirmed; SAS retains its documented finite six-digit strength and modulo bias. |
| Handshake state transitions | `Core.cpp`: candidate/session separation, complete objects before KEM/derive, seven proof bits before installation, local physical confirmation plus authenticated peer confirmation and readback before persistence. ONLINE ignores fragments/control. Failed or missing TX completion cannot install keys and has a 500-ms timeout. No new defect confirmed. |
| Fragmentation/reassembly bounds | Exact object IDs/counts/sizes/index/padding/source checked before HMAC. Single fixed 1,568-byte buffer and 24-bit occupancy fit uint32. Conflicting duplicates destroy candidate; identical ones do not extend the 1,500-ms deadline. Added exact expiry and UINT64 addition-overflow boundaries; existing 500,000-case corpus retained. No new defect confirmed. |
| Cancellation/stale results | `Worker.cpp` and `Core::result`: exact request lengths, one slot, full generation+serial+operation checks. Running cancellation retires before wiping; queued/ready cancellation wipes immediately. Stale result and stale TX tickets cannot gain authority. Generation/serial exhaustion fails closed. Existing real-worker and simulator fault tests retained. No new defect confirmed. |
| Replay/sequence protection | `Traffic.cpp`: role-separated keys/nonces, authenticated 20-byte AAD, nonzero sequence under conservative limit, 64-entry replay window updated only after complete acceptance, bounded record renewal. Old session IDs and prior traffic keys rejected. Existing vectors, replay and near-limit tests retained. No new defect confirmed. |
| Secret zeroization | Candidate reset wipes ECDH/KEM/ML-KEM private material, derivation workspace, pre-auth/Finished/Commit keys and worker results; PSA P-256 handles destroyed. Worker buffers wipe on transfer/failure/cancel. ONLINE retains traffic keys and recovery only; no secret logging added. Register/spill/cache/physical remanence is not proven erased. No new defect confirmed. |
| Errors/failure recovery | Primitive, worker, storage, control-auth, timeout and install failures clear live authority and use bounded replenishing budgets/backoff. Storage provenance remains explicit. FIPS implicit rejection is checked through transcript/key confirmation, not misreported as mandatory explicit KEM failure. Existing fault suites retained. No new defect confirmed. |
| Resources/stack | Core workspace is caller-owned/fixed; KEM executes outside Core service. Existing S31 static estimates reserve 48 KiB worker stack with documented platform allowance; these are not measured high-water or free heap. No stack/task increase, allocator substitution or smaller KEM introduced. Actual S31 scheduling/stack/heap/RF/power remain platform gates. |
| Production separation | Production CMake excludes the core. `Interfaces.hpp` rejects ESP_PLATFORM; `S31Adapter.hpp` rejects activation. No negotiation/downgrade or v3 secret import. Legacy storage requires fresh v4 pairing. Both guards remain intact. |

No independently confirmed additional v4 correctness defect was found in this
targeted review, so no speculative cryptographic or protocol changes were made.
Focused boundary regressions supplement the existing large suites. The historical
preflight's statements that v4 did not yet exist describe its old assessment;
the later v4 protocol/implementation documents describe the isolated core.

## Executed validation

Host binaries use warnings as errors and ASan+UBSan. Reproduction outputs are
in ignored local logs, not claimed as immutable committed artifacts.

- Baseline COBS repro: expected ASan stack-buffer-overflow for 508-byte input /
  510-byte output; new insufficient-capacity regression failed before change.
- Fixed COBS: all requested lengths 0, 1, 253, 254, 255, 256, 508, 509, 510,
  plus 4,096; every insufficient capacity canary; null/SIZE_MAX arguments;
  10,000 deterministic randomized round trips up to 8,192 bytes, random malformed
  decode/capacity cases; existing 20,000 framed-packet tests.
- Final `scripts/test-wireless.sh`: PASS with ASan/UBSan, 1,000,000 authenticated
  GCM records, 1,000,000 protocol events, 52,000 corruption cases, 135,983 storage
  faults and 100,000 malformed authenticated UART streams. The new paced replay,
  ingress and 1,000-command control-packet admission/reconnection tests passed.
- Final `scripts/test-v4.sh`: PASS with ASan/UBSan, independent primitive/project
  vectors, pairing/rekey, 135,983 storage faults, 500,000 fragment cases and
  1,000,000 protocol events (148 successful installations in that fixed corpus).
- `scripts/check-v4.py --skip-host`: PASS for all 145 authoritative NIST cases,
  upstream functional testing, 42 unchanged pinned vendor files, independent
  Python vectors, eight application modules under Clang static analysis, and
  both negative activation compile tests. The full host run is separate above.
- Historical DTLS regression also passed with real Mbed TLS under sanitizers.

Physical ESP-NOW admission, actual packet loss/CCMP, latency, current draw,
stack high-water and S31 SDK/platform activation remain unmeasured here.
The parent audit's complete firmware/build matrix supplies target compilation
results separately; successful compilation does not establish those properties.

## Changed-file map

| File | Reviewable change |
| --- | --- |
| `firmware/PROT_OS7_MAX7219/src/HelmetProtocol/HelmetProtocol.h:46` | COBS write/argument/overflow bounds; subtraction-based decoder bounds. |
| `firmware/esp32/src/SessionHandshake.cpp:154` | Cached Hello/Response duplicates retain retry pacing. |
| `firmware/esp32/src/WirelessLink.cpp:183` | Non-DATA dropped before active authentication interval. |
| `firmware/esp32/src/WirelessTransport.h:5` | Testable bounded callback ingress policy. |
| `firmware/esp32/src/EspNowTransport.cpp:149` | Apply admission before queue insertion. |
| `firmware/esp32/src/EspNowTransport.h:11` | Atomic cross-task native-session flag. |
| `scripts/tests/helmet-protocol.cpp:14` | Required COBS lengths, capacities, randomized/malformed cases. |
| `scripts/tests/wireless.cpp:640` | Ingress, paced replay, budget refill, active-session malicious traffic/reconnect. |
| `scripts/tests/v4/Checks.cpp:141` | Exact fragment deadline, final fragment length and near-UINT64_MAX addition guard. |

Changes only add bounded pointer/capacity checks and bounded structural
admission in normal UART/radio paths. Cached handshake responses now respect
existing retry timing, reducing avoidable radio activity under replay. No
hardware power/latency saving is quantified, no operation changes CPU frequency,
and no PQ operation is introduced on the active command path.

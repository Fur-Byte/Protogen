# Coverage and assurance boundaries

Status describes what is implemented in the suite. The current run's
summary.json, not this inventory, determines whether a check passed.
A model-only row is not implementation verification.

| Production area | Formal evidence | Related regression | Scope / remaining gap |
| --- | --- | --- | --- |
| Wireless replay and application fields | replay-step, replay-three, action-fields, acceptedHistoryRejected, oldSessionRejected | verification/tests/replay.cpp, scripts/tests/wireless.cpp | Replay transitions and command value/duration admission checked against independent specifications. Session establishment and authentication are not proved by those checks. |
| UART COBS | cobs, cobs-malformed, extended variants, cobs-writer-step, cobs-reader-step; cobsInitial/WriterStep/WriterFinish/ReaderStep | verification/tests/assurance.cpp, scripts/tests/helmet-protocol.cpp | End-to-end symbolic round-trip bounded to 4/12 raw bytes. Full 90-byte encoder step/finish invariant and decoder block/copy bounds checked for arbitrary admissible cursor states. Native tests cover all packet lengths and general 254-byte code boundaries. This is not an end-to-end full-size symbolic round-trip theorem. |
| UART packet header, CRC and integers | packet-header, crc-byte, wire-integers, packetPayloadBounds, littleEndian16 | verification/tests/assurance.cpp, scripts/tests/helmet-protocol.cpp | All header field values and capacities, CRC-16 polynomial byte update, LE fields. Native full-length packet/stream round-trip and each single-bit corruption at each length. Full decoder timing state and complete CRC loop equivalence not mechanically refined. |
| Nonces and wireless crypto | nonce; oldSessionRejected | scripts/tests/wireless.cpp, uart-auth.cpp | Concrete nonce serialization injective in version, role and 64-bit sequence. Key uniqueness, entropy, authentication, handshake/rekey liveness, and platform crypto remain assumptions or native integration evidence. No cryptographic primitive proof. |
| Sensor processing | sensor-current; signedCurrentRange | verification/tests/assurance.cpp, scripts/tests/energy-monitor.cpp, sensor-recovery.cpp | All 24-bit current patterns; native exhaustive 16-bit SHT40 raw axes, CRC fault, NTC and percent boundary checks. I2C timing, calibration and thermal/electrical measurements not proved. |
| MAX7219 driver | display, display-existing; geometry theorems | scripts/tests/matrix.cpp, postmerge-rendering.cpp | Configured 112×8 framebuffer, valid mapping and allocation. Native allocation failure, SPI register and visibility tests. General panel geometry, lifetime, SPI timing and optics not fully proved. |
| Animation/behavior | easing, timelineEventBounds | verification/tests/assurance.cpp, scripts/tests/animation-v2.cpp, timeline-runtime.cpp | All 257 Q8 phases/curve byte values for monotonic range, endpoints; native all valid durations/elapsed pairs. Track interruption, handle generation, selection and timeline seeking exercised natively; not full formal state refinement. |
| Audio and ISR ring | audio-ring; audioCounterAdvance/Consume, ringProducer/Consumer, accumulator bounds | verification/tests/assurance.cpp, scripts/tests/audio-v2-unit.cpp, audio-corpus.cpp, firmware.cpp | Actual 512-slot counter/index functions under admissible occupancy, all 16-bit counter values, wrap, full/empty distinction. Native valid-config and invalid-sample DSP stress. Compiler/hardware memory ordering, ADC, FFT numerical error and all floating-point states not mechanically proved. |
| Actions queue and retry | action-cursor, timers; dispatchCount, blockedIntentRetained, elapsedModulo | verification/tests/assurance.cpp, scripts/tests/wireless.cpp, retry-delay.cpp | Concrete cursor invariant, no occupied slot overwritten, arbitrary uint32 timer arithmetic. Independent deque and production command admission tests include backpressure, expiration, rollover, stale removal, ordering and sequence exhaustion. Single-owner assumption; no proof of FreeRTOS races. |
| EEPROM storage | config-layout, brightness; configFieldBounds, configMarkerBounds, eepromNextSequence | scripts/tests/config-compatibility.cpp | All format/payload values and slot selection/wrap; current 53-byte record and legacy layouts. Actual storage routine under fake EEPROM interrupted-update faults at every write; no hardware EEPROM atomicity proof. |
| SecureStorage NVS | verifiedRequiresReadback, invalidNotWritten | scripts/tests/nvs-verification.cpp, storage-faults.cpp | Real save routine with mocked set/commit/readback errors; blob corruption and modeled power loss. Real IDF NVS durability, security configuration and secret lifetime not proved. |
| Brightness, safety and scheduling | brightness, timers, ring bounds | verification/tests/assurance.cpp, scripts/tests/configuration-safety.cpp, supervision-unit.cpp, wearer-io.cpp | All requested/approved values and ceiling up to 15. Native UART deadline eligibility and status expiry; corrected blocked wait uses a positive tick. Actual hardware load, safety, deadlines and starvation not established. |
| v4 / ML-KEM / UART crypto | Not run as formal cases | scripts/check-v4.py, check-uart-auth.py, check-pq-worker.py | Integration and threat assumptions need dedicated work; no change to production qualification. |
| Concurrency, HAL, physical qualification | Abstract producer/consumer bounds only | scripts/twin, flight-recorder, ingress-peer, scripts/qualification.py | Native/fake-peripheral interleavings are not a proof of the target scheduler, interrupt ordering, or physical behavior. F3 remains blocked. |

## Model correspondence

Lean geometry assumes the configured 14×1 tile chain and valid matrix
positions/rotations. The CBMC display harness calls the same framebuffer
kernel as the driver. Native matrix tests independently rotate expected
pixels, exercise allocation failure, and inspect fake SPI registers.

Lean replay represents the complete accepted history (not the bitmap).
The concrete transition verifies rejection preservation for any admissible
state and legal accept. The initial-state trace and native history comparison
check examples of the connection. This is not a machine-checked refinement
between Lean and C; session identity and authentication are assumed.

The Lean queue and timing models use mathematical counts/indices and actual
32-bit BitVec subtraction. The models do not prove ownership or atomicity.
The audio bounds assume quantized values ≤1023, and the EEPROM bounds assume
the current layout. A layout/constant change must update the model and its
tests. The native integer/sensor harnesses connect the mathematical bounds
to the corresponding C kernels, but no general translation equivalence is
claimed.

## Release implications

Formal tools and harnesses are host-only. The small production kernel
extractions preserve source-level behavior and public interfaces; compiler
output can nevertheless change. Do not infer target RAM/flash/latency
equivalence from host tests. Use the existing pinned target build matrix and
physical qualification to measure or reject changes before release.

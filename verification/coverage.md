# Coverage and assurance boundaries

Status describes what is implemented in the suite. The current run's
summary.json, not this inventory, determines whether a check passed.
A model-only row is not implementation verification.

| Production area | Formal evidence | Related regression | Scope / remaining gap |
| --- | --- | --- | --- |
| Wireless v3 ReplayWindow, WirelessSession | replay-step, replay-three; expiredStaysExpired, acceptedHistoryRejected, oldSessionRejected | verification/tests/replay.cpp, scripts/tests/wireless.cpp | Concrete transition checks all 64-bit inputs with top ≤ MaxSequence and legal admission; monotone high water and rejection preservation within session. Three-record bounded trace from reset. Authentication, key uniqueness, session freshness, nonce construction, persistence, invalid caller transitions not established. |
| UART COBS (HelmetProtocol) | cobs, cobs-malformed and extended variants | scripts/tests/helmet-protocol.cpp | Distinct allocated input/output, logical capacities within object; input ≤4 or ≤12 bytes; round trip, malformed lengths, canaries. 90/92-byte framing and 254-byte code boundaries rely on native regressions, not exhaustive proof. |
| UART integer fields and CRC packet framing | wire-integers; littleEndian16 | scripts/tests/helmet-protocol.cpp, uart-auth.cpp | All uint16/uint32 values, little-endian bytes, canaries; valid buffers. Entire pack/unpack, CRC, Decoder state and Gate authentication/freshness are not CBMC verified. |
| INA228 sensor conversion | sensor-current; signedCurrentRange | scripts/tests/helmet-protocol.cpp, energy-monitor.cpp | All 24-bit input patterns for signed 20-bit current. No proof of I2C transactions, SHT40 CRC/float conversion, NTC/logarithm, calibration, physical temperature or power. |
| MAX7219 driver | display, display-existing; tileIndex, rotatedTileIndex*, pixelIndex, localRotation | scripts/tests/matrix.cpp, postmerge-rendering.cpp | 112×8 geometry, all signed coordinates, valid rotation/mapping and allocated regions. Blank or arbitrary prior state as identified by harness. Constructor/setPosition/setRotation maintain mapping bounds under single-owner operation; no proof of allocation, lifetime, larger configurations, SPI timing, electrical or physical output. |
| Animation/behavior | timelineEventBounds | scripts/tests/animation-v2.cpp, timeline-runtime.cpp, blink-timing.cpp | Abstract event offsets for validated 32-event asset; no proof of complete animation interruption, interpolation, generations or face-selection implementation. |
| AudioV2 and FFT | amplitudeAccumulator, audioWindow, audioRing | scripts/tests/audio-v2-unit.cpp, audio-corpus.cpp, firmware.cpp | Abstract bounded integer samples and counts only; float rounding, FFT library, ISR ring, overrun, normalization and silence behavior remain implementation gaps. |
| Actions queue and clocks | enqueueCapacity, dequeueCapacity, nextSlot, elapsedModulo | scripts/tests/wireless.cpp, retry-delay.cpp | Count and index model and 32-bit modular subtraction; actual queue ordering, stale eviction, FreeRTOS synchronization, ISR interleavings and scheduler liveness not proven. Timer elapsed assumes less than one full counter cycle. |
| RetryDelay / ControlArming | retryExpires, retryStaysDisarmed, remainingBound | scripts/tests/retry-delay.cpp, wireless.cpp | Abstract pending-state transition, no guarantee of call frequency, raw/debounced release history, or physical wake. |
| ConfigStore EEPROM | configFieldBounds, configMarkerBounds, brightnessBound | scripts/tests/config-compatibility.cpp, configuration.cpp | Current 17-field/53-byte record in two 64-byte slots; legacy version parsing, CRC and interrupted-write ordering remain implementation/regression evidence only. |
| SecureStorage / DeviceConfig NVS | verifiedRequiresReadback, invalidNotWritten | scripts/tests/storage-faults.cpp, helmet-protocol.cpp | Boolean model of validation/set/commit/readback/ambiguous error. No proof of NVS durability or recovery, SDK/eFuse behavior, storage atomicity or secret handling. |
| Safety and supervision | brightnessBound, elapsedModulo | scripts/tests/configuration-safety.cpp, supervision-unit.cpp, sensor-recovery.cpp | Logical ceiling 1 and explicit approval, not thermal/electrical/wearer safety. Sensor timeouts and progress instrumentation not fully verified. |
| v4 / ML-KEM / UART crypto | Not run | scripts/check-v4.py, check-uart-auth.py, check-pq-worker.py | Integration and threat assumptions need dedicated work. Standard cryptographic primitives are not independently re-proven. |
| Scheduling, HAL, physical qualification | Not run | scripts/twin, scripts/qualification.py, physical trials | Host timing, interrupts, FreeRTOS, hardware registers, bus, reset, storage and optical/thermal/wireless measurements require separate evidence. F3 remains blocked. |

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

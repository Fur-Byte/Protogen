# P3 instrumentation and flight recorder

Base is `9a5cb99cc76499506ebca78ee23a3dbad1bc8e38`, the merge of
[P2 PR #4](https://github.com/Fur-Byte/Protogen/pull/4).
The [baseline workflow](https://github.com/Fur-Byte/Protogen/actions/runs/43902840839)
passed. This phase adds opt-in qualification instrumentation with
`PROTOGEN_FLIGHT_RECORDER=1`. Undefined or zero compiles out the recorder,
extra clock reads, worker metrics, and new USB command handlers. The option
does not change feature defaults, clocks, frame policy, storage formats,
task counts, radio v3, or bridge UART framing. The P1 goldens and P2
stimuli are unchanged.

## Ownership and publication

The Teensy main loop owns all record and timing writes, and the USB command
handler uses that same context. The ADC ISR publishes only an aligned
lock-free 32-bit modulo counter for dropped samples. There is exactly one
writer. It performs a relaxed load and store. The main loop loads the count
without resetting it and computes unsigned deltas. No payload or flags depend
on that counter, so release/acquire ordering for other memory is unnecessary.
The counter must be sampled before a full 2³²-event turnover. The ISR performs
no recorder output, formatting, multi-field updates, or interrupt masking.

C3 `app_main` owns the ring in both role builds. ESP-NOW callbacks keep using
their existing atomics and protected ingress machinery. The recorder reads
only numeric drop counts and existing main-owned security counters. The
existing wearer worker publishes a self-contained stack/work snapshot under
its existing FreeRTOS portMUX. The main task takes that lock for a copy once
per second. Ring operations and formatting do not run under the lock.
There is no recorder task or live C3 transport.

## Format and retention

Version 1 records contain 16 little-endian bytes in this layout.

| Offset | Field |
| --- | --- |
| 0 | Local microsecond timestamp, modulo 2³² |
| 4 | Sequence, modulo 2³² |
| 8 | Unsigned value (count, observation, measured value or old flags) |
| 12 | Unsigned 16-bit event ID |
| 14 | Unsigned 16-bit detail (category, flags, or compact reason) |

The owner copies a whole record before moving the cursor. The physical write
index is independent of sequence rollover. With 480 slots on Teensy and 128
on C3, full rings overwrite the oldest committed record. The overwrite
counter saturates. A missing cursor returns the current retained range and
overwrite count. No scan assumes old RAM is initialized. Reset starts an empty
ring with sequence zero. A debugger capture must be taken at an owner-safe
point after a completed append or iteration. No guarantee applies to an
arbitrary halt in the middle of an overwrite.

The first notification of each flood source is admitted immediately and
subsequent notifications are coalesced into at most one record per second
per source. The record retains the timestamp and reason of the last
notification in that window and the saturating count. Sensor state
transitions have independent admission from failure counters. If several
transitions occur within one window the last state wins. Sequence is the
authoritative append order, even if a delayed summary carries an earlier
timestamp. `merged` counts coalesced occurrences already summarized as
records. A sustained multi-source flood can eventually cause normal
overwrites within the finite retention window.

The versioned IDs are in
[FlightRecorder.h](../firmware/PROT_OS7_MAX7219/src/HelmetProtocol/FlightRecorder.h).
Payloads are selected numeric values. Expression details use the low byte for
expression ID and the high byte for origin, with a local observation as value.
Behavior details are bits for sleeping, visible, booped, blanked, and dock ready
in that order. Sensor state records set detail bit 15 on Teensy to distinguish
state from failure reason. Audio and UART loss records count events. C3 records
boot reason, link state, aggregate drops, replay and AEAD rejection counts,
queue saturation, heap and stack minima, and worker activity. No pointer,
MAC address, keys, nonces, tags, packets, decrypted payloads, or audio samples
are recorded. Recorder storage is volatile. It has no EEPROM, flash, or NVS
write path.

## Export and noninterference

The Teensy accepts three opt-in commands on the existing **local USB CDC**
console. Each stages one JSON line in the existing 1,024-byte reply buffer.
The existing send path checks available capacity and submits at most 64 bytes
per service opportunity. A disconnected consumer clears the staged reply
through the original USB path. A stalled consumer holds only that reply.
Main-loop audio, sensor, rendering, and UART calls continue. There is no
streaming snapshot, extra export queue, or production UART export.

- `RECINFO` reports version, oldest/next cursors, occupancy, overwrite and
  coalesced counts, sampled audio backlog peak, and the count and duration of
  the busiest sampled recording window.
- `REC <uint32>` requests a single record. Version, requested cursor, and
  the retained cursor range identify that response independently. A record
  already staged in the reply buffer remains coherent if the ring wraps
  while its output stalls.
- `RECSTAT <stage>` returns count, cumulative and maximum elapsed
  microseconds, and six histogram bins with exclusive upper bounds of 10,
  50, 200, 1,000 and 5,000 µs followed by an open bin.

An operation is bounded by the fixed USB input and reply work per poll.
There is no physical microsecond bound for formatting or write overhead in
this phase. The USB stage instrumentation can measure elapsed servicing on
target when qualified. Existing commands keep their original response
formats. A slow consumer can delay further USB commands on its own channel,
as before. The C3 recorder is inspectable through its ELF symbols
`p3C3Records`, `p3C3Ring`, `p3C3Timings` and `p3C3Rate` at an owner-safe
debugger stop. Decode a binary array dump and the three owner fields with:

```sh
python3 scripts/flight-decode.py ring.bin --used 128 --write-index 7 --next 135
```

A live C3 transport requires a separate board-specific safety review. This
phase adds no wireless command, ESP-NOW diagnostic data, or UART export.

## Timing semantics and qualification

Existing Teensy PERF, LATENCY, IDLE, STATUS and SENSORS diagnostics keep
their original meanings. P3 timings are loop work before WFI, loop-start
interval, render, compose, render digit writes, FFT, sensor acquisition,
UART service, USB service, audio service, radio tick and recorder append.
The Teensy has no measured radio stage and the C3 stages that apply only to
Teensy remain empty. On C3, the USB slot measures the existing board-local
console service, which may be UART0 rather than USB. Existing C3 PERF already
reports main work, heap minimum, radio activity, and worker/OLED counts.
P3 adds loop intervals, stage distributions, worker work maximum and stack
minimum, and main stack minimum. Paw production code has a battery sensor.
The OLED exists on the helmet role.

Times are local elapsed durations from unsigned clock differences. They
can include preemption/ISR delay and timer-resolution error. Nested stages
overlap and cannot be summed as CPU time. WFI is excluded from measured
loop work and contributes to time between loop starts. That interval is
not CPU utilization or sleep residency. Gap events above 20 ms on Teensy or
50 ms on C3 are diagnostic thresholds, not qualified deadlines. ISR overhead
is outside the stage timers. The host parity check uses P2 virtual time and
reports host runtime separately.

The target checker builds matched feature profiles on and off, reads linked
symbols and static resource figures, enforces the RAM budgets, and restores
the standard C3 build. Teensy records are declared DMAMEM and verified in
the 512 KiB OCRAM range. The code uses ordinary CPU loads and stores and
does not introduce DMA or require a cache-maintenance contract. Metadata and
histograms occupy ordinary static storage, with a four-byte ISR atomic and
the existing reply buffer reused. C3 uses one 2,048-byte .bss array per role
with owner metadata and histograms accounted for by the total static delta.

```sh
python3 scripts/test-flight-recorder.py
python3 scripts/test-flight-recorder.py --deep
python3 scripts/check-golden-frames.py
python3 scripts/check-golden-frames.py --recorder-enabled
python3 scripts/check-flight-builds.py teensy
python3 scripts/check-flight-builds.py c3
```

CI retains `build/flight-evidence` plus target run manifests. The fast
checks exercise wrapping counters and sequences, host concurrency, flood
admission, volatile-reset semantics, export/backpressure, frozen fixtures,
and exact P2 behavioral trace parity. Full CI adds the seeded deep replay
and one million notifications with prolonged USB backpressure. No hardware
was flashed or measured. Before P4, collect paired target captures under
matching workloads with the recorder disabled and enabled. Record stage
tails, service gaps, append/export costs, stack/heap minima, and compare
optical frame timing and audio drops with the existing qualification method.

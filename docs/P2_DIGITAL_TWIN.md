# P2 deterministic digital twin

P2 branches from merge commit `945fa778c04ff605c2244339d9fec9490ab48cae`.
[PR #3](https://github.com/Fur-Byte/Protogen/pull/3) merged the six P0–P1
commits. Its [PR workflow](https://github.com/Fur-Byte/Protogen/actions/runs/43802482153)
passed. The twin builds the existing sketch through `scripts/tests/sketch.h`
with Microphone, BoopSensor, VisorSensor and WirelessPaw. Optional UART auth,
VisorConnector, early reveal, and efficiency candidates are off in this
profile. P0–P1 still tests those separate build variants. Production source,
the Face Studio preview protocol, and the P1 golden fixtures keep their
original behavior and bytes.

## Run a replay

```sh
# Use ARDUINO_FFT_SRC from the pinned toolchain installer or the Arduino libraries directory.
make -C scripts/twin
python3 scripts/twin/replay.py scripts/twin/fixtures/visibility.json \
  --trace build/twin/visibility.tsv --report build/twin/visibility.json
python3 scripts/twin/test_replay.py
python3 scripts/twin/test_replay.py --deep --evidence build/twin/deep
python3 scripts/check-golden-frames.py
bash scripts/test-config-compatibility.sh
```

Replay v1 is a JSON object with `version`, `origin_us`, `duration_us`,
`loop_us`, `cosmetic_seed`, `epoch`, and an ordered `events` array.
Each event is `[relative_microseconds, operation, value]`. Its one-based
position is the sequence number. The first input can occur at 1 ms, after
the modeled setup transactions. Replay v1 allows up to 4,096 inputs,
120 virtual seconds, and a loop interval of 100–20,000 µs.

- `usb` takes a printable command. `uart` takes raw hex bytes. Truncate,
  corrupt, duplicate, delay, or reorder scripted injections by choosing
  their contents and ordered event times. `hello` sets a bridge generation
  through the actual protocol. `expression` uses the latest delivered
  status ticket and bridge. `duplicate` sends the previous command again.
- `visor`, `button`, `boop`, `sample`, `audio`, `sensor-stall`,
  and `audio-stall` control modeled input or acquisition. `audio_tape`
  optionally supplies up to 32,768 ordered 10-bit conversion values. When
  the tape ends, the model uses the held input and optional integer
  four-sample waveform at the 4 kHz acquisition rate.
- `hold` suppresses only main-loop opportunities for the given number of
  microseconds. `disconnect` disconnects the UART peer. `usb-disconnect`
  independently changes USB connectivity. `short-write` limits each
  accepted UART write to 0–32 bytes.
- `eeprom` optionally initializes the two 64-byte slots as hex. An absent
  image is erased storage. `power-cut-after` interrupts after the given
  number of changed EEPROM bytes. A second replay can boot the emitted image
  in a fresh process with an explicit new epoch.
- `cosmetic_tape` optionally supplies up to 4,096 unsigned 32-bit words.
  The deterministic cosine-free waveform and seeded cosmetic PRNG model
  local inputs. UART epochs are supplied independently. The runner has no
  cryptographic provider or production key material.

At a shared virtual timestamp, due ADC callbacks run first, then in-flight
UART completions, then inputs in file order, then the available main loop.
During SPI transfers the clock continues servicing due ADC and UART events
and applying scripted inputs. A loop that started by the horizon may finish
its bounded work after it. Busy loops coalesce missed opportunities. The
accelerated scheduler jumps over explicitly suppressed loop slots while
continuing to service ADC, UART, and input events. The reference visits
every nominal slot. Both policies produce identical traces for the test
corpus. Active and quiet ADC sampling remain explicit, as do active
debounce, blink, and polling opportunities.

The TSV header fields on each row are relative virtual time, record type,
and latest input sequence (zero for startup). The last field is context for
diagnosis. Earlier queued work can produce an effect after a newer input.
SPI rows contain full daisy-chain bytes at latch time. State rows capture
changes to expression, visibility, fault and link state, brightness, and
logical frame. USB output is captured as hex chunks. UART rows describe
complete decoded peer packets, including ACK result codes. The EEPROM and
totals rows distinguish terminal storage and measured queue losses. The
JSON report carries source, binary, input, artwork, configuration, and trace
identities. `--expect <file>` compares an explicitly supplied TSV and gives
the first divergent timestamp and sequence plus a reproduction command.
Normal verification never replaces expected files. These traces cover the
modeled observables. They omit internal CPU instructions, individual ADC
values, and partial outbound UART bytes from the trace.

## Fidelity ledger

| Subsystem | Fidelity | Actual code and model | Validation and physical limits |
| --- | --- | --- | --- |
| Sketch behavior | F1 | Actual loop, renderer, local controls, USB commands, sensor service, blink and transition logic. Explicit 64-bit host clock projects onto unsigned 32-bit firmware clocks. | Check rollover, collisions, retarget, hidden expiry and sleep/wake against independent assertions and reference stepping. No measured interrupt latency or target execution duration. |
| MAX7219 / SPI | F2 | Actual driver and rotation. 14 modeled devices, each with eight digit values. Full-chain commands latch together per transaction after 2 µs per byte at 4 MHz. Registers persist while shut down. | Six independent P1 logical/physical resting snapshots and the full P1 golden test. The hardware scan phase, optical simultaneity, current, brightness and temperature are outside the model. |
| ADC / audio | F2 | Existing host ADC callback timing, production 4 kHz timer, production 512-entry ring with 511 usable samples, production FFT and recovery. Integer waveform, tape, rail inputs and stalls. | Observe real drop counter under held consumer, fault flags, and recovery. Existing firmware suite uses <0.02 absolute bin tolerance against double-precision host FFT before discrete bar checks. Cross-target FFT parity and physical microphone characteristics remain unverified. |
| UART / USB | F2 | Production PawLink/COBS/CRC/gate and USB handler. UART uses independent full-duplex 115,200 8N1 serializers, each with an assumed 512-byte software bound including in-flight bytes. RX adapter uses the repository's 256-byte added RX buffer as a conservative cap. USB gets a separate assumed 512-byte RX cap and an immediate writable consumer. | Accepted, duplicate and expired ACKs, overflow, partial input, short writes, disconnect and new bridge generation. Teensy core FIFO depth, UART interrupt scheduling and USB CDC USB-frame scheduling are outside the model. |
| Wireless v3 | F1 | Existing separate host harness runs the actual session, radio packet validation and link logic with bounded in-memory transport. Its fixed corpus covers lost, duplicated, corrupted, reordered and stale messages, reconnect and reset. The replay peer begins at the UART boundary. | The baseline wireless and admission suites exercise these invariants. The TSV carries no RF trace or complete ESP-IDF endpoint execution. Production entropy is outside the replay profile. |
| EEPROM / reset | F2 | Production ConfigStore over two modeled 64-byte slots. A counted partial write terminates execution and exports the bytes. Fresh process tests generation reset and recovery. | Byte-exact P1 fixtures, approval validation, and boot after interruption. Physical writes, reset domains, retained RAM, and watchdog behavior are outside this model. |

The deep campaign repeats 16 seeded 12-second workloads in both execution
policies. Reports include virtual and wall time, trace event counts and bytes,
loop opportunities, peak queues, dropped events, and cumulative child-process
RSS high water (including sanitizer runtime). Each modeled queue is bounded
and the executable stops at one million trace records. The PR tier runs
five short cases, P1 frozen-frame comparisons, and the existing compatibility
suite. Scheduled, tag, and manual runs add the deep replay campaign. Results
and input/trace pairs are included in the existing CI evidence artifact.

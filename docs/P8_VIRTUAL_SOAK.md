# P8 deterministic recovery and virtual soak

The P8 runner uses the P2 host sketch, serial wire and ADC model. Each seed
boots once and carries its firmware, queue and timer state through the entire
requested duration. ADC conversions, UART bytes and file-ordered inputs
remain scheduled at their original virtual times. Foreground opportunities
are one millisecond apart. The accelerated scheduler skips slots during
explicitly injected foreground holds and still delivers due ADC and UART
events. There is no accelerated parking interval.

A 6.5-second fault sequence runs at the start of each 900-second interval
that can accommodate the whole sequence. The rest of each interval runs the
ordinary host firmware, including normal audio sampling and periodic UART
status. A day has 96 fault sequences. This duty cycle represents modeled
background operation with periodic bursts of faults. A virtual day does
not measure target execution time, acoustic or optical behavior, electrical
conditions, radio performance or safety.

## Run and verify a shard

Build the default twin with the pinned ArduinoFFT source. A dirty checkout
may produce local pilot data. Clean Git provenance is required for the
aggregate to mark the architecture target met.

```sh
make -C scripts/twin
python3 scripts/twin/soak.py run --seed-start 0 --seed-count 2 \
  --virtual-duration-ms 910000 --wall-clock-budget-sec 30 --per-seed-timeout-sec 15 \
  --output-dir build/p8-soak-pilot

python3 scripts/twin/soak.py aggregate --seed-start 0 --seed-count 2 \
  --virtual-duration-ms 910000 --output-dir build/p8-soak-pilot-aggregate \
  build/p8-soak-pilot/shard.json
```

Each output directory is new so evidence from separate runs stays intact.
The `shard.json` includes source and executable identities, requested seed
range and duration, runtime and execution limits. For each passing seed it
records the workload and semantic SHA-256, full duration, checkpoints, event
counts, peak queue occupancy, drops, rejections and terminal state hashes.
The semantic digest excludes runtime and diagnostic prose. A reproduction's
semantic digest matches across shard assignments for the same runner,
executable, profile, seed and duration.

The runner writes an atomic initial manifest and updates it after each
seed. A killed or unstarted seed has unknown completed virtual duration,
recorded as null, and contributes zero passing duration. Exit codes are
0 for passing the requested scope, 1 for a failure or invalid evidence and
2 for incomplete coverage. The aggregate checks the expected ranges and
durations, record and workload hashes, checkpoint counts, consistent
profiles, duplicate or unexpected seeds, missing seeds, unfinished shards
and matching source, runner and binary identities. It records each input
manifest's SHA-256. With zero shard artifacts it writes an incomplete
aggregate that lists every requested seed as missing.

The subprocess deadline is the earlier of the shard deadline and seed
timeout. The runner stops the child process group on timeout or if combined
stdout and stderr exceeds the compact 8 KiB limit. A shard manifest is capped
at 256 KiB. Successful shards retain compact summaries. The first failed
seed can retain the input and verified shorter prefix, each capped at 1 MiB,
and a 512 KiB trace tail, for under 3 MiB of total evidence per shard. A
reported invariant divergence carries virtual time, triggering input
sequence, expected value and observed value. If time remains, the runner
trims inputs after the divergence and verifies the same outcome before
saving the prefix. `failure.json` records the reproduction commands. Add
`--trace-from-us N` to a replay to capture bounded diagnostics from time N.

## Fault coverage

| Boundary | Executed host checks |
| --- | --- |
| Foreground, display and inputs | Foreground hold delays serving queued work, fills the audio ring and delays ADC reads. Cases cover rollover, visor visibility and blanking, boop-driven microphone pause, sleep and wake, and continued expression control after a permanent microphone fault. |
| ADC and protected visor | Timer stalls and ADC1 conversion stalls assert fault flags, then clear after healthy input. A microphone rail is distinguished from a missing conversion. The protected-dock profile exercises stale rail data, pilot loss, buffer gating, undervoltage and recovery. Existing microphone recovery probes cover capped retry backoff. |
| Configuration | The twin rejects an invalid saved expression even with a valid CRC and falls back from corrupt CRC or marker. It boots legacy bytes and selects the newer record at sequence rollover. The existing P2 integrated replay covers interrupted save and restart. The compatibility suite cuts power at every changed byte in the tested writes. Soak checkpoints verify the high brightness request stays within software approval and the intensity ceiling. |
| UART and wireless | The baseline twin exercises lost ACK, retransmission, a delayed stale command, reordered delivery, bounded queue pressure, partial frames, and reconnect with a new bridge generation. The old bridge command is rejected and cannot change the expression. Existing UART admission and wireless tests provide separate authenticated-session and wireless protocol evidence. |
| Experimental profile | Audio V2 detects synthetic speech while a timeline runs. Checks cover sample loss, a detector reset after timer loss, boop/speech precedence, timeline cancellation, track saturation and invalid assets. The default binary rejects timeline assets. |

The twin has modeled ADC values, display/register effects and UART frames.
Authentication and wireless host tests run at their own boundaries. The
scripted twin peer carries no production key material and does not execute
a physical ESP32-C3 radio stack. Dock and ADC faults are injected behaviors
in the model. A programmed intensity ceiling is a software invariant.

```sh
make -C scripts/twin BUILD=../../build/twin-p8-experimental \
  EXTRA_FLAGS='-DPROTOGEN_ANIMATION_V2=1 -DPROTOGEN_ANIMATION_TIMELINES=1 -DPROTOGEN_AUDIO_V2=1 -DVisorConnector'
python3 scripts/twin/test_soak.py --experimental-program build/twin-p8-experimental/twin \
  --evidence build/p8-faults
python3 scripts/twin/test_replay.py
bash scripts/test-config-compatibility.sh
bash scripts/test-uart-admission.sh
```

## Pilot and campaign status

A local Apple Silicon ASan/UBSan pilot completed seeds 0 and 1 at 910
virtual seconds each. Each seed processed 126 injected inputs across two
fault sequences, about 909,000 foreground loops and 3.63 million ADC
conversions. The measured child times were 0.316 and 0.310 seconds,
approximately 0.000344 wall seconds per virtual second. On this host the
linear estimate is about 30 seconds per full-day seed and 50 minutes of
aggregate execution for 100 seeds. These measurements include long
intervals of ordinary modeled operation. GitHub runners still need their
own rate measurement.

The pilot's two-seed summary occupied about 3.6 KB. At that size,
50 passing shard manifests occupy about 180 KB before archive overhead.
The manual workflow plans two seeds per shard with up to four concurrent
shards, a 300-second wall budget and a 120-second seed timeout. Each shard
job has an eight-minute job limit for checkout, artifact transfer, execution
and upload. A full run requests seed start 0, count 100 and duration
86400000 milliseconds. The focused suite demonstrates timed-out seeds,
no-artifact and partially completed aggregation, duplicate rejection and
child output overflow.

**Architecture target incomplete.** The pilot completed 2/2 seeds at
910,000 ms each. It completed **0/100 full-day seeds**. Run the bounded
manual workflow against the release candidate and aggregate the matching
shard artifacts before claiming the full virtual-soak target.

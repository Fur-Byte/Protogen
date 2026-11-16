# P4 shared behavior core

The reference is the clean [P3 merge](https://github.com/Fur-Byte/Protogen/commit/f298f3bbf2e07f9a4a4054e1d722123d0d50f34c).
It includes the P0–P3 merges. The reference worktree was detached at that SHA
before changes to the sketch. The host baseline reproduced P1 rendering and
the 23 P2 traces with P3 disabled and enabled. No approved measurement setup
was available for paired physical P3 captures.

## Ownership and dependencies

| State or operation | P3 owner | P4 owner and boundary |
| --- | --- | --- |
| Selected expression and sleep intent | Sketch globals mutated by controls, USB and PawLink | One behavior instance. The caller still orders and admits commands. |
| Transition source, retarget closure, progress, idle glances | Sketch render routine and expression handler | Behavior instance and explicit displayed-source input |
| Boop state, arm, origin, expiry, override | Sketch loop, analogous local sensor and remote handlers | Behavior instance. ADC samples and command admissions come from the shell. |
| Blink phase, interval, follow-up, cosmetic tape | Blink and sketch globals | Behavior instance. The shell reads available cosmetic entropy and passes words; UART authentication is unchanged. |
| Glitch, eyelid and frame eligibility | Sketch globals | Behavior instance. The shell calls at the original service points and commits frame eligibility after writing. |
| Speech activity, mouth aperture, FFT bitmap mutation | Microphone service / FFT | Same audio owner. Pose uses speaking/aperture values. A per-observation 64-byte spectrum snapshot goes to the renderer and its cache. |
| Visibility, dock readiness, debouncing and GPIO | Hardware services | Arduino adapters. They pass visibility decisions to the core and retain actual shutdown, intensity and power sequencing. |
| Command precedence, generation, freshness, security | USB then Paw UART then local controls in the loop, with independent validation | Same ordered shell. Wireless packets and keys never enter the core. |
| Causal IDs, post-write milestones, delay, supersession, recorder hooks | ResponseTiming and sketch | Existing shell, retaining acceptance, timestamp origin, real eye comparison, and post-SPI milestones. This boundary keeps recorder and timing semantics together. |
| Runtime configuration and EEPROM | ConfigStore and USB adapter | Existing schema and bytes. The portable implementation receives validated configuration by const reference. |

The extraction proceeds from pure transition math to behavior-owned state and
then integrates the audio snapshot at the render boundary. The clock reads,
GPIO/ADC and display calls stay at their existing service points. Host builds
link the same C++ translation unit that Arduino builds under the sketch's
`src` directory.

## Interfaces and observations

`Behavior::Core` owns expression and sleep intent, transient boop state,
blink phase and the two-word cosmetic input, glitch state, transition
anchors, and frame eligibility. Its progression methods receive explicit
timestamps, validated configuration, and small logical inputs. `poseAt`
returns an expression, closure, eye offset, boop/glitch state, and mouth
selection. Its transition anchor comes from the last emitted pose. The
standalone test compiles C++17 without Arduino or a sketch.

The shell captures all four 16-byte FFT bitmaps on the render path when
speaking in spectrum mode. The renderer, cache, and causal counterfactual
consume that same immutable value. A wake uses the snapshot from the frame
written before shutdown is released. The ADC ISR, FFT, acquisition cadence,
brightness controls, display writes, and runtime configuration codec stay
in their original locations. The snapshot is frame-local and adds no
persistent FFT state. The cache still retains one previous spectrum when
its existing compile-time flag is active.

`ResponseTiming` stays in the shell, including ownership of observation IDs,
timeouts, same-loop dispatch order, and P3 event emission. The shell
continues to compare real eye pixels and sample timestamps after the display
write. No milestone is inferred from the pose returned by the core. Preserving
this boundary leaves the future animation core responsible for pose
progression and the display adapter responsible for evidence of visible work.

## Verification and resource evidence

```sh
make -C scripts/twin behavior-core-test
python3 scripts/check-golden-frames.py
python3 scripts/check-golden-frames.py --recorder-enabled
python3 scripts/check-behavior-parity.py
python3 scripts/check-behavior-parity.py --deep
python3 scripts/test-flight-recorder.py --deep
python3 scripts/check-behavior-build.py
```

The `behavior-p3.json.gz` oracle records full original traces, including
SPI, UART, USB, and diagnostics. It was captured by running the unmodified
sketch from the clean P3 worktree. Normal verification reads the compressed
fixture and reports the first divergent record without normalizing any
fields. The deep corpus includes all 23 P2 scenarios and five additional
captures. The three-command collision checks the individual P3 events from
USB, UART, and local release at the same virtual timestamp. The four
animation profiles cover all early-reveal/cache settings. An explicit
recapture requires a separate clean worktree at the pinned P3 SHA and a new
output path for review.

The local pinned Teensy build measured an 84-byte linked core instance in
both profiles. Relative to P3, local flash occupancy decreased by 1,024 bytes
and static RAM1 variables by 64 bytes. RAM2 usage stayed constant. Compiler
stack estimates show 48 → 104 bytes in the default sketch loop and
56 → 112 bytes with the recorder and all features enabled. The render frame
estimate rose from 168 to 184 bytes by default and to 192 bytes with the
recorder. The snapshot accounts for 64 bytes of automatic storage. Stack
reports are per function and exclude callees and interrupts.

An optimized Apple arm64 microbenchmark observed about 5 ns for
`beginTransition` plus `poseAt` in a synthetic sequence. A 30-pair sanitized
host replay comparison measured about 18.13 ms median for P3 and 18.16 ms for
P4, including subprocess startup and capture. These are host measurements.
Actual Teensy stage costs, interrupt stack high-water, and physical latency
remain open qualification items. No C3 target sources changed.

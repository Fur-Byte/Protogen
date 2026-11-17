# P7 Audio V2 and mouth behavior

P7 starts at the verified P6 merge `8e741ebd2cf97939b2ca1b24ce034dff3b29e6e5`.
`PROTOGEN_AUDIO_V2` defaults to 0. Both compile-time modes keep the same
hardware, settings schema, USB command formats, UART and wireless protocols.
Audio V2 works independently of the animation track scheduler.

| Animation V2 | Timelines | Audio V2 | Profile |
| --- | --- | --- | --- |
| 0 | 0 | 0 | 000 |
| 0 | 0 | 1 | 001 |
| 1 | 0 | 0 | 100 |
| 1 | 0 | 1 | 101 |
| 1 | 1 | 0 | 110 |
| 1 | 1 | 1 | 111 |

Timeline support continues to require Animation V2. Without the microphone
feature the detector has no linked state and the existing USB microphone
unavailable reply applies. The host profile harness also exercises the
111 combination with the face cache and recorder disabled.

## Acquisition and rendering

The same 4 kHz QuadTimer, ADC0 ISR, 512-sample ring and maximum 128-sample
foreground drain are used in both modes. The main loop owns the detector and
publishes a value snapshot after each 64-sample window. It contains mean-square
energy, smoothed envelope, noise estimate, gate, activity, onset/offset,
periodicity and aperture. No raw samples enter the behavior core or any
diagnostic export. The P3 ISR counter publishes drops under its existing
contract.

The detector removes DC bias and smooths the existing 10-sample rectified
amplitude scale with the configured attack/release durations. Window level and
short fixed autocorrelations provide onset evidence. Periodic windows can
corroborate quieter onset using the peak rolling average on the same configured
sensitivity scale. Two contiguous qualifying windows are required to open.
Hold and release use sample counts, a lower release threshold and the existing
100 ms hold. The detector has no absolute time or wall-clock dependencies.

The noise estimate falls quickly with quieter windows. It rises slowly below
the gate when inactive, or after eight consecutive unvoiced windows in a
narrow amplitude band. Periodicity or a material change in level restarts
that evidence. This lets stationary background release an open mouth while
periodic activity can continue indefinitely. Strongly modulated noise can
still resemble speech, and some unvoiced speech can resemble background.

Calibration retains the legacy bias, rectification and averaging procedure.
When calibration completes, a fresh detector uses the new noise floor. The
firmware discards the partial FFT and activity state after queue gaps and
resets on ADC pause, restart, and sleep. Clipped windows cannot keep the mouth
open. Existing retry, fault and preference logic owns ADC recovery.

Aperture 0 means closed activity, with the normal expression mouth visible.
When speaking, amplitude mode uses existing slight, medium and wide mouth
geometry. Two windows and asymmetric thresholds stabilize changes between
adjacent apertures. Spectrum mode still uses the four 16-byte FFT bitmaps from
one frame-local observation. The existing renderer resolves sleep/visibility,
boop and glitch precedence. Active speech owns the mouth over a timeline
track, which keeps running and becomes visible again when speech ends.
The existing scheduler, face cache and dirty MAX7219 writes are reused.

## Reproducing the evidence

From the repository root with the pinned toolchains available:

```sh
python3 scripts/check-audio.py --held-out
python3 scripts/check-audio-profiles.py
python3 scripts/check-audio-profiles.py --target
```

The first command writes per-window traces and reports under
`build/p7-evidence/`. `mouth-demo.txt` shows the actual renderer during
silence, onset, quiet and sustained activity, a short pause, speech ending,
the background noise step and return to silence. `overload-demo.txt` shows
the actual mouth before a simulated foreground stall, after the queue
discard, and after acquisition recovers. The profile logs include the
existing nine sensor recovery cases. No microphone or physical display is
required. Timing in these files is simulated or host wall time as labeled.

The [frozen scoring protocol](validation/P7_AUDIO_SCORING.md) defines
the acceptance thresholds. [Iteration 2](validation/P7_AUDIO_ITERATION_2.md)
records the rejected first candidate and the fresh holdout split.
Normal PR CI runs the held-out corpus and focused host/target profiles.
Full CI adds the development corpus and retains the inherited 48-mask legacy
matrix. Feature-branch pushes alone do not start CI. Draft PRs start one
automatic run, and a manual full run is separate.

Physical sampling and CPU timing tails, stack high-water, optical response,
acoustic accuracy, electrical load and thermal behavior still require
qualified hardware measurement. P7 makes no hardware changes and creates no
recording or transport. Before P8 relies on V2, measure real audio conditions,
including wearer voice mixed with crowd noise, and confirm Teensy service-gap
and headroom margins with the existing P3 metrics.

## Local qualification

The independent iteration 2 holdout uses 30 ten-second clips and 107 checks.
Activity durations below exclude the three ambiguous modulated-noise clips.

| Metric | Legacy | Audio V2 |
| --- | ---: | ---: |
| Precision | 45.8% | 92.0% |
| Recall | 100% | 97.8% |
| False-open time | 76,704 ms | 5,504 ms |
| Missed speech | 0 ms | 1,408 ms |
| Peak modeled ring backlog | 6 samples | 6 samples |
| Modeled sample drops | 0 | 0 |
| Maximum modeled service gap | 1,448 μs | 1,448 μs |
| MAX7219 transactions | 4,430 | 1,958 |
| MAX7219 traffic | 124,040 bytes | 54,824 bytes |

Worst first onset was 16 ms and minimum sustained-voice recall was 99.8%.
In two speech-plus-noise step cases, release fell from 3,104 ms to 192/208
ms. The third (seed 78, 250 Hz carrier) lost 848 ms of labeled activity,
including premature closure. The ambiguous noise clips still opened the
mouth for 21,200 ms in total, compared with 21,360 ms for legacy. These
failures of separation remain useful inputs for physical qualification.

Pinned Teensy 4.0 builds used Arduino CLI 1.5.1, Teensy core 1.62.0 and
arduinoFFT 2.0.4. Results are byte counts, using the Teensy size report.

| Profile | Flash | RAM1 variables | RAM2 variables | Detector symbol |
| --- | ---: | ---: | ---: | ---: |
| 000 | 94,204 | 18,208 | 12,416 | 0 |
| 001 | 96,252 | 18,208 | 12,416 | 156 |
| 100 | 98,300 | 19,232 | 12,416 | 0 |
| 101 | 100,348 | 19,200 | 12,416 | 156 |
| 110 | 98,300 | 19,232 | 12,416 | 0 |
| 111 | 100,348 | 19,200 | 12,416 | 156 |
| 111 without microphone | 91,132 | 16,352 | 12,416 | 0 |

Net flash increased 2,048 bytes in each matched configuration. The ELF puts
the 156-byte detector at `0x20002a4c` or `0x20002a64` in RAM1.
Compiler frame estimates are 16→40 bytes for sample dispatch, 56 bytes for
the detector sample function and 16 bytes for window completion. Standalone
FFT, render and loop frames were 8, 200 and 104 bytes in both modes. These
are individual frames and do not establish peak call-stack use.

On the sanitized Apple Clang host, example median batch averages were
4.4→13.7 ns per quiet sample, 920→931 ns per FFT window with input preparation,
and 639→676 ns per composition. Maximum batch averages were 5.5→13.8,
957→980 and 663→681 ns respectively. The maximum end-to-end host wall
time for a ten-second clip was 33→34 ms. These are host measurements.
The simulated service gap includes modeled SPI and omits time spent running
host CPU instructions.

With Audio V2 disabled, all 23 deep P2 replay traces matched a separately
built P6 merge byte for byte. The 57 P1 checkpoints also matched with P3
recording off and on, and 28 independently captured P3 traces matched with
diagnostics included. The inherited Animation V2 tests, timeline compiler,
timeline editor smoke and Face Studio HTTP round-trip were run locally.

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

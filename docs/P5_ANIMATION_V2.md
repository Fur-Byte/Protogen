# P5 Animation Engine 2.0

P5 starts at the clean P4 merge `25eef9b4ccf8171338b646a2927f9d8e77aed123`.
That detached checkout reproduced 28 complete P3 reference traces, all 23
deep P2 scenarios, the frozen P1 frames and recorder-on/off parity before the
sketch changed. The independent P4 trace digests are tracked in
`scripts/tests/fixtures/p4-legacy-traces.json`. Verification checks those
digests and retains the complete P3 traces for first-record diagnostics.
No expected output is made from the candidate during tests.

## Feature gate and behavior

`PROTOGEN_ANIMATION_V2` defaults to zero and accepts only 0 or 1. Zero has
no track state or scheduler linkage and keeps P4 output. The flag adds no
settings, EEPROM data, protocols, tasks, interrupt work or hardware
interface. `Behavior::Core` remains the authoritative state. It owns the
track table and the handle for an optional settling reaction. The shell
still owns command validation and priority between its sources, hardware
safety, display commits, and causal observations.

With the flag enabled, a new expression can receive a short two-eye
settling squint after its legacy causal observation completes. New
expression requests cancel only that automatic track. One pending
settling request expires after five seconds. A rejected automatic
admission drops that accent and preserves every existing track.
The existing `idle_motion` setting optionally gives the eyes an eased
two-pixel outward-and-return glance. Ordinary blink scheduling, early reveal,
transition shape, closure, and boop lifetime retain their existing logic.
The other primitives are reusable through the portable track API and
exercisable in the twin. No production console accepts track commands.

## Track table and arbitration

Eight slots, indexed 0–7, hold start time, start order, generation,
duration, signed endpoints, region, motion, priority and easing. An
eight-bit mask records occupancy. The table occupies 168 bytes in the
pinned profiles. An admission validates the request, reclaims expired
slots, and takes the lowest free usable slot. Invalid requests and a full
table return explicit results without evicting any live track. Generations
advance on reuse. A slot at its maximum generation retires after release
for the rest of that boot, so a stale handle cannot alias a new occupant.

Each eye and the mouth select one eligible owner. Larger priority wins.
Ties select the most recent admission, even at the same millisecond. A
tie in start order selects the lower slot. Start orders compact into
bounded ranks before rollover, preserving relative age. A Full Face eye
motion competes independently for both eyes. Full Face mouth aperture only
competes for the mouth. Winning one region grants no authority over
another.

Preempted tracks continue aging invisibly. Their original curve can
resume if the mask expires before the track does. Sleep, visor blanking,
and connector safety still suppress display updates. The engine ages
tracks while blanked. Boop masks both eyes and the mouth. Blink, a legacy
expression transition, or a pending causal observation masks experimental
eye effects. Speaking and an active glitch retain their existing mouth
ownership. An override temporarily chooses which existing eye sprite to
sample and never changes the base expression. A new transition anchors its closing phase
to the last emitted per-eye sprite, position and effective lid. Retargeting
during a settling squint therefore starts from the emitted closure. V2
effects age behind that handoff. The shell tests actual emitted artwork for
already-visible targets and keeps per-eye source IDs for the post-write
counterfactual. Removing an effect at admission alone cannot manufacture
transition feedback. A boop that restores shifted Vwv to its canonical
art is observed as a visible change.

| Primitive | Bounds and output |
| --- | --- |
| Horizontal gaze | −2 to +2 pixels, independently per eye |
| Experimental vertical shift | −1 to +1 pixel, only on explicit tracks. The one-row right Wink suppresses its downward shift to retain the sprite. |
| Lid | Additional closure floor 0–8 |
| Squint and settling | Additional closure floor 0–4 |
| Expression override | One of six unmodified expression IDs |
| Mouth aperture | 0–3, while the legacy mouth is quiet |

Curves use Q8 integer linear, quadratic ease-in/out, smoothstep, and cubic
settling with exact endpoints. Durations are 0–5,000 ms. Age uses unsigned subtraction with a half-range
check so a sampled loop timestamp cannot expire a future admission.
A zero-duration track yields its target only at its admission timestamp.
A duration endpoint is eligible at that exact timestamp and expires
afterward. Subframe effects can fall between sampled frames. There is no
additional render loop or faster animation poll. `nextVisualDeadline()`
exposes the existing 16 ms cadence and 2 ms causal-feedback window.
Every evaluation is bounded by eight slots per region.

## Geometry, cache and evidence

Logical eyes retain X=96–111 and X=0–15, each at Y=0–7. The renderer clips
signed X/Y after transforming within the owning 16×8 region. No transform
copies a pixel into a neighboring region or moves an artwork byte.
Composition uses the existing immutable FFT snapshot. The visual cache
compares every resolved eye coordinate, closure, sprite override and mouth
override along with the original pose and applicable snapshot. Distinct
resolved values may produce the same pixels. The actual frame comparison
and physical digit mask still suppress all redundant SPI transfers.

The visual oracle takes existing independently frozen P1 eye masks and
uses inverse destination coordinate lookup. It tests all six sprites,
signed boundary shifts, complete closure and all new fields. The
MAX7219 oracle independently counts dirty digits and checks the expected
28-byte transaction per digit across 14 devices. Tests cover fully masked
changes with no SPI traffic. The twin additionally verifies the real
production loop under simulated boop, sleeping, visor hiding, audio input,
and concurrent UART/USB/local dispatch with the recorder, cache and early
reveal flags enabled. Host wall-clock reports include sanitizer or model
work as indicated. They do not measure Teensy performance.

## Reproduce and view

```sh
python3 scripts/check-animation-v2.py --demo
python3 scripts/check-animation-v2.py --deep
python3 scripts/twin/replay.py scripts/twin/fixtures/animation-v2.json --animation-v2
python3 scripts/check-behavior-parity.py --deep
python3 scripts/check-golden-frames.py
python3 scripts/check-golden-frames.py --recorder-enabled
python3 scripts/test-flight-recorder.py --deep
python3 scripts/check-behavior-build.py --animation-v2 --output build/p5-evidence/target.json
```

The demo prints 112×8 frames sampled from the actual twin. It includes a
steady gaze sweep, independent eyelid timing, a settling squint, gaze
resuming after boop, and a same-timestamp regional tie. The twin-only
`v2-track` input carries
`[region, motion, priority, curve, duration_ms, from, to]`. Enumerations
follow the order in
[AnimationV2.h](../firmware/PROT_OS7_MAX7219/src/BehaviorCore/AnimationV2.h).
`v2-cancel` takes `[slot, generation]`. These are replay test controls.
The desktop preview's original commands remain available unchanged.

Fast CI builds both profiles and runs the independently specified unit,
visual, geometry and dirty-SPI checks. The full workflow adds the seeded
stress and inherited deep tests. Resource reports and traces are retained
under `build/p5-evidence`. No devices were flashed. Physical P3/P4
qualification, frame-time and stack high-water measurements, and viewing
vertical clipping on the assembled display remain outstanding.

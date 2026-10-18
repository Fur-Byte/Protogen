# Response latency and scheduling qualification — 2026-10-17

## Pipeline and corrected delays

The operational path is physical paw edge →30ms stable debounce → action intent
→fresh authenticated helmet state →128-byte ESP-NOW record → source/replay/AEAD
validation → bounded helmet UART queue →1152008N1 authenticated COBS frame →
Teensy HMAC/epoch/ticket/action checks → animation pose/composition → dirty-row
SPI → MAX7219/LED visibility. Navigation acts on **release**, boop on press;
hold/combination gestures include their deliberate hold interval. A press held
for seconds cannot have a navigation response within100ms of initial contact.

Confirmed changes:

1. An acknowledged action invalidated Paw status until the next100ms active
   poll. A matched ACK now coalesces an immediate authenticated status request.
2. Review reproduced cached pre-command status causing consecutive Next targets
   `1,1` instead of `1,2`. Helmet status remains unavailable until a matched
   UART ACK and a subsequent status. Teensy sends state promptly after its ACK
   drains. Paw checks expected resulting state and monotonic status sequence;
   epochs reset the expectation. A bounded150ms settling period permits a
   legitimate simultaneous local override without permanent lockout.
3. The initial fence fix could deadlock on a lost UART ACK. Exact in-flight
   retries (kind/length/epoch/sequence/ticket/payload) now pass while cache is
   unavailable. The original300ms freshness deadline is not extended by retries;
   it clears the fence and allows recovery on subsequent status. Independent
   exact-production-body sanitizer tests confirmed two retries and recovery.
4. The renderer sampled transition feedback at16ms intervals. It now checks at
   2ms only until the first changed frame, then returns to16ms. The driver still
   writes only changed rows. Dense sampling adds bounded CPU work during that
   short interval; it is not claimed as an energy saving.
5. A configured1000ms transition produced186.492ms first response in the model.
   Closing/reopening ramps now cap at60ms; longer durations extend the closed
   hold. The original160ms curve and selected total duration remain intact.
   New tests cover both160ms and1000ms from all six expressions.
6. The pinned SDK default100Hz made `pdMS_TO_TICKS(5)` equal0. The nominal poll
   therefore yielded/spun rather than blocking. Production now explicitly pins
   supported1000Hz and checks it at compile time/build validation. The active service poll is2ms on two ticks. Bounded post-radio UART and
   command passes avoid another scheduler period after receiving useful data. CPU frequencies remain Teensy600MHz/C3160MHz; this is not
   dynamic frequency scaling. Both coarse100Hz stress and production1000Hz host
   worker fixtures remain covered.

Finite queues/rate limits, stale-state rejection and safety interlocks remain.
Sustained RF interference, an unbounded input flood, reconnection or a physically
absent/unsafe visor cannot have a universal100ms visible-response guarantee.
Dock power qualification and Hall debounce are intentional safety delays, not
removed to satisfy a latency number. Ordinary sleep/wake with a present qualified
visor is modeled separately from cold attachment or RF session establishment.

## Host measurements and their boundaries

`test-latency.sh` executes the actual Teensy sketch, real UART HMAC implementation
in authenticated mode, application gate, renderer and modeled ADC/SPI boundaries.
All sensors are enabled; quiet microphone acquisition runs at4kHz. It covers six
source expressions, ordinary transitions, sleep/wake, boop, and128 malformed UART
bytes plus stalled USB output. A boop from an already identical vwv face is
classified as unchanged pixels, not fabricated as a measured response.

The final model charges30ms debounce, four2ms C3 service opportunities and
three assumed1ms radio deliveries for status request/reply/command after idle,
then87us per1152008N1 UART byte. Shared ServicePollMs=2 is used by production
and the test. Post-radio UART/command passes remove an otherwise extra poll. FakeSPI charges2us/byte at4MHz. It does **not**
execute the complete ESP main task, measure RF, or charge target graphics,
HMAC/CRC/DSP/ISR execution or unpredictable task scheduling. Separate tests run
real wireless Link/crypto and exact production callback/status-ordering logic.

Before dense first-feedback scheduling, the ordinary160ms sleepy/heavy scenario
was103.684ms (CRC mode). With the original42ms upstream fixture, dense rendering produced93.628ms CRC
and99.370ms authenticated. The refined idle-aware41ms upstream fixture produces
final maxima92.628ms CRC and98.370ms authenticated across160/1000ms durations.
The same-fixture rendering improvement is10.056ms; the extra1ms is a modeled
scheduling-fixture difference, not measured physical gain;
no pre-change authenticated maximum was recorded because the earlier assertion
stopped that run. The authenticated model has only1630us margin: it is useful
regression arithmetic, **not proof of physical under100ms**. Unmodeled costs
can exceed that margin; the physical target remains an explicit release gate.

`LATENCY` on Teensy reports validated-UART entry to subsequent visible SPI
change/shutdown: accepted/completed/skipped/superseded/expired/pending and last/
maximum microseconds. It is a diagnostic segment on a local clock, not an
end-to-end measurement or a causal proof under overlapping local input.
Sequence/keys/nonces are not printed. ESP `LATENCY` reports intent queue, UART
queue/full, ACK and main-work maxima with tick rate; these local segments must
not be added across unsynchronized clocks as if measured end-to-end.

## Exact physical measurement procedure

Use a logic analyzer/oscilloscope and photodiode (or a high-speed recording with
stated time uncertainty). Do not add trace GPIO pins to unreviewed wiring.
Observe the existing paw button contact/release and the first relevant LED
region changing. Record UART0/1 logic levels and SPI CS/clock/MOSI on approved
high-impedance probes to separate wire/service/composition costs. UART is3.3V;
use a common safe reference, never connect5V signals to MCU inputs.

Test each of six source expressions, both directions/navigation release, boop
press, sleep/wake, repeated rapid valid actions, active microphone/boop/visor
sensors, near-bound UART/USB traffic and matched authenticated modes. Include
idle for several keepalive periods, queued actions after ACK, one lost ACK,
packet loss/rekey and local override; classify offline/recovery and safety-held
samples separately, not as successful online trials. Repeat with the optically
slowest first-change expression and maximum permitted transition duration.

Capture press, action edge, first visible change and complete expression with
an external shared timebase; report all four and p50/p95/p99/max, missing responses,
trial count and instrument uncertainty. Target action-edge-to-first-visible
under100ms in healthy online and ordinary idle-wake conditions. Preserve the
160ms transition completion metric separately. Compare ADC sample/ISR cadence,
audio drops, queue counters and watchdog/reset cause during the same trials.
Do not wear or electrically qualify a helmet from the host result alone.


## Burst authentication scheduling

A real encrypted-Link regression queued two valid records before one receiver
visit. The old loop authenticated the first then discarded the second while its
5ms rate interval was active. The receiver now checks that cooldown **before**
dequeueing and retains later records for subsequent visits. The authentication
rate and finite queue size are unchanged; ordinary ACK/status bursts progress
without avoidable loss. Sustained interference still cannot receive an arbitrary
latency guarantee. Focused and full protocol suites cover the new behavior.

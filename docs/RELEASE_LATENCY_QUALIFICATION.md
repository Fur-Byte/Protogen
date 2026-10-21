# Response latency and scheduling qualification — updated 2026-10-20

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

The October17 **98.370ms authenticated** maximum measures first feedback,
often the old eyes beginning to close. It does **not** measure requested artwork
onset. Current tests separate all three causal stages, using actual emitted eye
pixels and a source-expression counterfactual to exclude unrelated mouth/audio,
natural blink, glitch and visor blanking. A reversal to artwork already visible
sets `target_already_visible` with a valid zero-time onset and continues tracking
feedback and completion. A request for the already selected logical expression
is skipped.

Current maxima from the same idealized action-edge model (milliseconds):

| Authenticated UART / transition | First feedback | Requested-expression onset | Completion |
| --- | ---: | ---: | ---: |
| Default close/swap/open,160ms |98.370|180.818|228.986|
| Optional early reveal,160ms |60.762|60.762|220.658|
| Default close/swap/open,1000ms |98.370|1012.874|1073.874|
| Optional early reveal,1000ms |60.762|60.762|1068.658|

These are **modeled**, not physical CPU/RF/electrical measurements. The maximum
may come from different source/scenario cases; columns are not additive stage
budgets. The model includes ordinary idle-wake, all sensors and4kHz acquisition,
real HMAC and FFT functionality, bounded malformed UART input and stalled USB.
It excludes target instruction/ISR execution cost, real radio loss/retries and
unpredictable scheduling. A pixel difference is a reproducible onset proxy;
human recognizability still requires optical/artistic review on the helmet.

Default `PROTOGEN_EARLY_EXPRESSION_REVEAL=0` preserves the160ms artistic style.
Building with `-DPROTOGEN_EARLY_EXPRESSION_REVEAL=1` reveals new eyes at the start
while retaining close/hold/open duration and continuous retargeting. This is an
explicit visible tradeoff, not a silent speedup or a new EEPROM setting. The
same option is tested at0/160/1000ms. Neither option guarantees100ms in loss,
reconnect, safety inhibition or arbitrary hostile traffic.

Teensy `LATENCY` reports **local dispatch to causal SPI** only, with
`physical_clock:false`. Origin, source sequence and monotonic observation ID
identify commands even when a sequence is reused after reconnect. Feedback,
onset and completion have separate last/max/valid fields. Superseded/skipped
identities, boop-delayed observations and5s expirations are tracked. Safety
blanking cannot complete an expression observation. Local, USB and UART commands
share this causal path. Keys/nonces/tags are never printed.

ESP `LATENCY` still reports local intent/UART/ACK/main-work segments; clocks are
unsynchronized and must not be summed as physical end-to-end measurements.
Teensy `IDLE` is explicitly `wfi_call_elapsed`, `includes_isr:true`: it measures
wall time around WFI, including interrupt execution, not true sleep residency or
energy savings. CPU baselines remain Teensy600MHz/C3160MHz.

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

Capture press, action edge, first causal feedback, requested-eye artwork onset
and completed expression with
an external shared timebase; report these separate intervals and p50/p95/p99/max, missing responses,
trial count and instrument uncertainty. Target action-edge-to-first-feedback and requested-expression onset
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

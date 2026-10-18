# Release power and interrupt-wait qualification

Status: software policy and regression work only. No helmet current, power,
battery runtime, optical latency, WFI residency, interrupt latency or temperature
measurement was performed for this release. Keep the Teensy 4.0 baseline at
600 MHz, SPI at its existing configured rate and microphone sampling at 4 kHz.
The supported C3/v3 radio, authentication, pairing, security settings and optional
hardware defaults remain in force. This procedure does not authorize uploading,
rewiring, changing eFuses, enabling S31/v4, or raising brightness.

Use [EFFICIENCY_QUALIFICATION.md](EFFICIENCY_QUALIFICATION.md) for the existing
physical energy/optical capture schema and analyzer, [HARDWARE_WIRING.md](HARDWARE_WIRING.md)
for the power boundary, and [RELEASE_RECOVERY.md](RELEASE_RECOVERY.md) for matched
images and rollback. Existing historical measurements and external board data
are not measurements of this assembled helmet.

## Reviewed behavior and changes

Read the complete `1956268..3275ff8` remediation diff and its reports before this
review, then checked the current main loop, idle helper, serial scheduler and
pinned Teensy core. The core's `startup.c::configure_systick()` configures the
100 kHz external SysTick source with reload 99 and priority 32. This supplies a
nominal 1 ms periodic interrupt without changing CPU frequency. The application
services USB/UART, controls, ADC1 and dock interlocks before considering hidden
interrupt wait. Visible rendering and audio use the ordinary loop; they do not
enter this idle path.

The original idle checks already rejected PRIMASK, interrupt context, deep sleep,
CCM low-power selection and disabled SysTick. This review adds conservative
checks for BASEPRI, FAULTMASK, sleep-on-exit, the expected SysTick source and
reload. These defend against future core/library reconfiguration; the current
supported application does not intentionally set those incompatible states.
The injected policy tests establish fallback behavior, not a reproduced
physical hang on the baseline firmware.

A separate avoidable-work issue was reproduced in the actual host sketch:
unsent serial output kept the hidden loop spinning even when the transport had
zero write capacity. RX also cannot be consumed behind that pending reply under
the existing serial ordering. Runnable work now mirrors that scheduler: pending
TX is runnable only with positive capacity; otherwise RX is runnable only after
TX drains. Buffered bytes remain queued, output stays ordered, and partial TX
continues immediately while capacity exists. No input/output packet format or
receive budget changes are required.

| State | Work that remains serviced | Interrupt-wait policy |
| --- | --- | --- |
| Visible face, quiet or animated | UART/USB, button/Hall, physical/remote boop, rendering, blink, audio/DSP when enabled, ADC1 and dock safety | Existing visible path; no new WFI or clock scaling. |
| Explicit application sleep | Controls, communications, command expiry/status, ADC1 dock voltage and recovery, pilot qualification | WFI only after runnable serial work is serviced and entry guards pass. |
| Hall-hidden face | Same safety/control work; speech/rendering are paused by existing visibility policy | Same guarded hidden wait; physical inputs continue periodic polling. |
| Display unavailable or dock disconnected | Commands and retries, pilot/rail sampling, safe power/buffer requests | Same guarded hidden wait; no attempt to wake the display using an invalid rail. |
| TX blocked, with or without queued RX | Retain ordered pending output/RX; retry capacity at the next service opportunity | May wait because existing service cannot advance those bytes until capacity returns. |
| Runnable RX or partially drained writable TX | Existing bounded service budgets and protocol validation | Keep running until the immediately runnable budget/backlog is drained. |
| Unexpected interrupt/sleep/tick configuration | Existing caller state is preserved | Decline WFI and return to normal polling; report the guard reason. |

No dynamic voltage/frequency scaling, visible-path sleep, oscillator shutdown,
peripheral clock gating, radio receive windows or deeper MCU sleep is introduced.
Those require measured headroom, peripheral/security review and independent
qualification. Saved settings and the brightness approval/ceiling must be
recorded for every trial; a build default does not override EEPROM settings.

## WFI entry contract and race analysis

`ControllerIdle::guard()` is the same pure predicate called at target entry.
The target reads the actual registers immediately before the wait and checks
its architectural masks against the pinned core at compile time.

| Guard | Required condition | Decline code |
| --- | --- | ---: |
| Execution/masking | Thread context, PRIMASK=0, BASEPRI=0, FAULTMASK=0 | 1 |
| Sleep/power mode | SCR SLEEPDEEP=0 and SLEEPONEXIT=0; CCM CLPCR LPM=RUN (0) | 2 |
| Periodic wake | SysTick ENABLE=1, TICKINT=1, CLKSOURCE=external (0), reload=99 | 3 |
| All conditions pass | Perform DSB, WFI, ISB, then return to the ordinary loop | 0 |

No guard repairs a caller's registers or temporarily masks interrupts. A byte or
tick arriving after the software queue check can be handled before WFI; the next
periodic tick still provides another service opportunity. Thus the design does
not rely on a one-shot event remaining pending across the check/WFI boundary.
With the inspected core configuration and no prolonged ISR execution, the added
wait for an already handled event is at most the next nominal tick. ISR load,
peripheral behavior and the actual time to resume/poll still need measurement;
this is not a universal physical 1,000-microsecond latency promise. Unrelated
interrupts or debugger activity can also end a WFI early.

Arm documents interrupt priority requirements and sleep-on-exit behavior in the
[Cortex-M7 Generic User Guide, sections 2.5 and 4.3](https://documentation-service.arm.com/static/61efd8352dd99944d05142d1).
NXP distinguishes core wake from deeper system wake: SysTick does not provide a
GPC system wake source in the deeper modes described by
[AN12085, section 5.3.4](https://www.nxp.com/docs/en/application-note/AN12085.pdf).
That distinction is why the application requires run mode and refuses deep
sleep rather than assuming SysTick qualifies every low-power configuration.
NXP's [AN12245](https://www.nxp.com/docs/en/application-note/AN12245.pdf) describes
RT1060 power domains and a different evaluation board; its current tables are
not substituted for helmet measurements.

## Diagnostics and their limits

Use the read-only USB `IDLE` command for entry counters/guard status, `PERF` for
render/configuration/wait totals, `STATUS` for the actual audio timer and drops,
and `SENSORS` for recovery/fault diagnostics. On the radios capture `INFO`,
`PERF` and `SECURITY STATUS` through the existing local console. Do not log pairing
or UART keys. Avoid continuous diagnostic polling during energy captures: it
creates USB/formatting/interrupt work and changes the workload being measured.
Record counters once before and once after each interval instead.

The idle fields are since boot; unsigned counters and the 64-bit elapsed total
saturate instead of wrapping. `last_guard` describes the most recent idle entry
check and returns to 0 after a permitted entry; it is not a history of causes.

| Field | Meaning | Limit |
| --- | --- | --- |
| `attempts` | Calls reaching the idle-entry policy | Does not count visible-loop passes. |
| `waits` | Accepted wait calls/returns | An accepted WFI can return immediately; it is not proof of sustained sleep. Host calls model this path without ARM WFI. |
| `declined` | Idle entries rejected by a safety guard | Investigate a nonzero delta in an otherwise qualified production configuration; no state is changed to hide it. |
| `last_guard` | 0 permitted, 1 context/masks, 2 sleep/power mode, 3 tick configuration | A later permitted call can replace a previous cause. |
| `total_us`, `max_us` | Software `micros()` differences around accepted waits | Include interrupt execution before the second timestamp; not calibrated CPU sleep residency. |
| `over_budget_waits` | Software-reported waits greater than 2,000 microseconds | Diagnostic threshold for investigation, not a command timeout or proof of deadline failure. |

For an interval of externally measured duration T, `delta(waits)/T` is a
software wait-return rate. It is not an IRQ-source count. Differentiate USB,
SysTick, UART and ADC activity with an external trace or qualified profiler.
Do not report `1-total_us/T` as CPU utilization: the timestamp path includes
ISR work, and the pinned `micros()` implementation combines SysTick with DWT
cycle counts. Its behavior while the core waits, including early UART wake,
must be checked against the external instrument. Saturated counters require a
new boot before further differencing. No mA, joule or battery-life claim follows
from these counters alone.

## Repeatable physical measurement procedure

1. Record the full firmware commit/source identity, all three image checksums,
   compiler/core/library pins, actual features/UART-auth mode, complete `GET`
   settings and active brightness approval/ceiling. Record module and meter IDs,
   calibration/uncertainty, battery/supply state, RF environment, enclosure/ambient
   temperature and connector configuration. Keep a verified rollback bundle.
2. Review the existing power wiring with power off. Use a meter boundary that
   includes the helmet input, all converters, Teensy, displays, radio and wearer
   peripherals. Meter the paw separately. The regulated output voltage/divider
   is not an energy meter. Use the fitted calibrated INA228 only over valid
   intervals and cross-check it with an independent whole-input instrument.
   Resolve USB/VIN backfeed and ground-reference safety using the existing
   wiring instructions before attaching probes; this review makes no wiring
   changes and does not authorize them.
3. Make matched bench images from one source snapshot. Candidate uses the
   guarded WFI instruction. The comparison baseline changes only that literal
   `wfi` instruction to `nop` in an isolated copy of `ControllerIdle.h`, retaining
   the guard, counters, timestamp calls, settings and all other sources. Preserve
   the exact one-instruction patch and both build manifests/checksums. This is
   an unshipped bench comparison, never a silent production change. Do not use
   revision 1956268 as the matched energy baseline: it differs in safety and
   other behavior. Do not alter CPU voltage/frequency to manufacture a saving.
4. Warm up under the chosen state until temperature stabilizes; record the
   settling criterion and actual warm-up duration. For each state below collect
   at least three alternating matched baseline/candidate pairs, at least ten
   minutes per interval. Reverse pair order on later trials. Use identical
   physical/audio/radio action scripts and initial conditions; retain complete
   voltage/current/energy traces and diagnostic snapshots. A power-limit setting
   or a single instantaneous reading is not an interval-average measurement.
5. Separate steady energy intervals from timing/fault-injection intervals.
   Exercise startup, first shutdown, repeated wake, ordinary operation, pairing/
   renewal/reconnection and recovery in additional mixed-workload trials. Keep
   any missed reaction, reset or invalid meter interval visible in the record.
   Include boot/wake energy using the external meter rather than filling gaps
   with INA estimates.
6. Compute average input power from cumulative energy delta divided by external
   elapsed time; compare each pair with the combined meter/calibration/drift
   uncertainty. Require repeatable reduction beyond uncertainty before claiming
   savings. Record whole-helmet and paw power separately. If the difference is
   unresolved, report it as unresolved. Any runtime estimate must state measured
   usable battery energy, workload mix and converter effects; qualify it with an
   actual matched runtime test before publishing a battery-life improvement.
7. Capture actual action-to-first-optical-reaction latency using the existing
   schema-2 common-timebase optical procedure. Preserve default transitions and
   debounce, record both online and idle/wake conditions, and retain at least
   1,000 attempts per condition/trial with all misses counted. Use its p99<100 ms
   target for the triggering edge to first reaction; completed-expression
   latency is a separate metric. Neither UART ACK nor SPI CS is optical proof.

Minimum state matrix for steps 4–7:

| State / workload | Required variants |
| --- | --- |
| Visible static expression | Microphone disabled; enabled quiet 4 kHz; repeat each expression at the same qualified brightness. |
| Visible active | Recorded speech, natural blinks/transitions, physical/remote boop and UART/radio bursts under the agreed action script. |
| Application sleep | Paw online and absent; USB physically disconnected and attached-idle as separate trials. |
| Hall-hidden | Qualified return/removal, repeated button and paw actions; distinguish intentional hidden output from missed visible reactions. |
| Dock absent / reconnecting | Pilot-open, controlled reattachment and recovery; verify both requests safe and rail acquisition continues. |
| Backpressured console | USB and UART independently held at zero write capacity, with/without queued RX; resume capacity and verify ordered, complete output. |
| Sensor faults | ADC1 timeout/invalid conversion and microphone rail/no-progress; capture retry duty cycle and valid recovery, without defeating hardware protection. |
| Security/radio stress | Legitimate pairing/renewal, replay/invalid traffic, loss/reconnect; compare deadlines and cryptographic settings, not only average current. |

Record measured summaries with at least these columns (blank cells mean missing
evidence; do not replace them with model values):

```csv
trial_id,variant,state,source_sha256,settings_sha256,start_s,end_s,input_energy_start_j,input_energy_end_j,ambient_c,case_c,uncertainty_j,raw_trace_path,diagnostics_before_path,diagnostics_after_path
```

Retain raw current/voltage traces and optical captures, external clock settings,
trigger method, sample rate/bandwidth, uncertainty calculation and analysis
version. Submit the existing analyzer's strict `run.json`, `energy.csv` and
`latency.csv` fields for supported comparison types; extra state-specific WFI
trials retain the same evidence standard but must not be mislabeled as an
already supported optimization flag. Independent review of raw evidence remains
required even when a statistical report passes.

## Exact board checks before WFI can be qualified

These checks are future bench work, not results of the host suite. Use a fixture
and an owner-approved instrument plan; never inject overvoltage or defeat the
hardware pilot/OVP gates just to provoke a software fault.

- Inspect actual PRIMASK, BASEPRI, FAULTMASK and IPSR at a normal entry; verify
  thread mode/unmasked context. Inspect SCR, CCM CLPCR, SysTick control and reload
  against the table. Do not test mask/sleep-mode rejection by changing those
  registers on an in-use helmet; the pure policy tests cover unsafe combinations.
- With USB/radio activity minimized, confirm the external periodic wake/poll
  spacing continues in hidden states. Sweep a repeatable input edge across the
  tick phase, including immediately before and after the queue check/WFI entry;
  retain the worst observed wait-to-service interval. Test UART RX and USB events
  arriving in that interval, plus already pending data and spurious wakes.
- Repeat with the debugger detached: debugger presence can change WFI behavior,
  interrupt cadence and current. If a temporary trace marker/profiler is used,
  choose only an independently reviewed unused pin/resource, preserve production
  pin mappings, retain its exact bench-only patch, and repeat power/latency trials
  without that instrumentation. This release does not allocate a new marker pin.
- Verify ADC0 timer cadence at 4 kHz during all visible acquisition workloads;
  inspect sample drops, ADC fault/recovery counters and optical speech response.
  Hidden microphone pause is intentional; confirm it restarts when policy allows.
  Verify ADC1 and rail freshness through hidden WFI, backpressure and reconnection.
- Scope pilot, buffer request, power request and actual switched rail across
  detach, reattach, waking and SPI initialization. A sleeping core cannot replace
  independent hardware gating. Confirm no extra ready state is created by a stale
  measurement and that each powered reconnect receives full MAX7219 setup.
- Verify byte ordering/completeness, RX overflow behavior and request expiry during
  blocked/resumed TX, both authenticated UART modes, USB connect/disconnect and
  sustained radio traffic. No throughput/deadline regression may be hidden by an
  improved current reading.
- Compare software `micros()` wait deltas with an external timebase for tick and
  early-UART wake. Inspect reset/watchdog, minimum heap/stack and thermal evidence
  during sustained mixed load. Diagnose every over-budget wait; a healthy-looking
  average does not excuse a missed safety or action deadline.

## Executed software evidence and remaining gate

The idle-policy regression uses the actual sketch/driver and ASan+UBSan. Six
additional guard conditions were observed failing against the extracted original
policy before the new guards were added; ordinary original guards stayed green.
Counter tests were observed failing before attempts/declines/budget/saturation
accounting was implemented. Actual-loop USB and UART backpressure regressions
failed before the runnable-work correction. The final actual-sketch idle suite
passed with AddressSanitizer and UndefinedBehaviorSanitizer and warnings as
errors, including all six additional guard scenarios, counter saturation, blocked
USB/UART with and without RX, partial/resumed TX byte preservation, USB disconnect,
Hall/display/dock hiding, button wake, and the new bounded `IDLE` response. Its
runner remains integrated in `scripts/test-firmware.sh`.

A separate translation unit including the real Teensy core `Arduino.h` and the
production `ControllerIdle.h` was compiled for Cortex-M7 using the installed
`arm-none-eabi-g++` 15.2.1 toolchain, `-Wall -Wextra -Werror`, hard-float target
options and `F_CPU=600000000`. Compilation passed. Disassembly showed all four
special-register reads (PRIMASK, IPSR, BASEPRI, FAULTMASK), followed on the accepted
path by DSB, WFI and ISB. The check used Teensy core 1.62.0 headers. The temporary
translation unit/object/disassembly were saved under `/tmp/protogen-idle-tests/`;
these are local diagnostic artifacts, not immutable release evidence.

Host execution does not execute ARM WFI or emulate clock gating. The target
object proves accepted instructions and compile-time core-mask agreement, not
board execution, wake timing or power reduction. The parent release report
records final complete target builds and whole-suite results separately.

Release power qualification remains open until the dated raw electrical,
interrupt/timing, optical and thermal captures above exist and pass independent
review. Current software evidence supports bounded policy and preserved APIs;
it supplies no measured energy-saving percentage or physical latency result.

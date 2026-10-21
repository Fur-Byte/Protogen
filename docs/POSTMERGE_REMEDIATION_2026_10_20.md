# Targeted post-merge remediation — 2026-10-20

Baseline: merged `main` **f04cbf49494cbe162e8ba70cf6b6c5e73f01f2d6**.
This follows the supplied independent Sonnet audit; it does not rewrite PR1.
The supplied audit and every harness member were inspected before execution.
Useful harness behavior was reproduced with project code and real Mbed TLS/PSA
and FFT dependencies; radio, sensors and SPI remain explicitly modeled.

## Findings and disposition

“Resolved and verified” below means the specified software regression passes,
not hardware qualification. Optical, RF, ADC, thermal and energy behavior still
need the bench procedures below.

| Finding | Disposition | Fix and acceptance evidence |
| --- | --- | --- |
| M1 captured Hello owns helmet handshake | Resolved and verified | Request-bound receiver freshness precedes ECC; old Request cannot create a candidate, old Proof/Challenge fail after restart/nonce retirement. New control revision2 explicitly rejects legacy admission. |
| M2 invalid tags exhaust global budget | Resolved and verified | Bounded owner HMAC service without invalid-tag lockout; atomic two-ECC admission and fixed queues retain expensive-work limits.48 reconnects while8–40/s invalid-tag traffic continues, worst1040 modeled ms. |
| M3 misleading first-visible metric | Resolved and verified for diagnostics; physical target unqualified | Causal feedback, target onset and completion are distinct, command-bound metrics. Default160ms artistic transition remains. Optional early reveal improves modeled onset with an explicit visible tradeoff. |
| M4 rapid commands flash superseded artwork | Resolved and verified | Retarget from the last emitted face and eyelid closure;11,664 frame-sequence regressions across both reveal settings,0/160/1000ms, repeated/reversed/overlapping commands pass. |
| L1 ADC sleep/wake false fault | Resolved and verified in software; hardware validation required | Cancel/invalidate pending IR ADC1 when Boop-only acquisition stops.16 configuration/scenario combinations,200 cycles each; connector rail monitoring and microphone ADC0 remain intact. |
| L2 callback queue interference | Implemented; physical validation required | Early operational peer/type filter and coherent critical-section identity/generation publication; stale-generation entries cannot dispatch. Host policy tests pass; no physical encrypted-peer bypass is claimed. |
| L3 UART candidate replacement | Resolved and verified | Stateless boot/generation/time-bound challenge IDs preserve earlier valid Confirms amid captured Requests. Four real-HMAC modes cover multiple captures,40 renewals each, expiry, replay counters, entropy failure, reset and wrap. |
| L4 missing rights/license clarity | Blocked by external permission/distribution review | Original sketch/artwork grant and model license version remain unresolved; FFT GPL obligations require owner review. [Inventory](../THIRD_PARTY_NOTICES.md) retained and clarified; no invented grant or FFT rewrite. |
| L5 CI integrity/timeout | Resolved and verified for regression; hosted result recorded in PR |120min job timeout, pinned checksum-manifest digest before archive validation, tampered-manifest rejection and host-package/version evidence. Strict warnings/sanitizers/pinned actions/least privilege remain. |
| Natural blink entropy outage | Resolved and verified | After100ms use a fixed cosmetic pause, with counter; cryptographic entropy failure never falls back. |
| WFI time overstatement | Resolved and verified for labeling | `IDLE` says `wfi_call_elapsed`, `includes_isr:true`; no true sleep-residency or power-saving claim. Existing shallow WFI and CPU baselines retained. |
| Teensy watchdog proposal | Deferred hardware qualification | A new watchdog needs bootloader/reset-loop and peripheral-stall experiments; no risky reset policy was added for an informational finding. |
| Reset during BOOST | Hardware qualification required | Every intensity path remains under `Settings::MaxBrightness=1`; external MAX7219 state may persist until startup writes. Reset/retention at the approved ceiling needs physical capture. |

No unresolved actionable software defect was identified in the final targeted
independent review. This is scoped engineering evidence, not a proof against all
attacks or an assertion that the helmet is electrically qualified.

## Source and reproducible red/green evidence

| Path / function | Change / regression runner |
| --- | --- |
| [SessionHandshake.cpp](../firmware/esp32/src/SessionHandshake.cpp), `start/receive/tick/retireAdmission`; [WirelessConfig.h](../firmware/esp32/src/WirelessConfig.h) | Explicit Request9→Challenge10→Proof11, revision2,288-byte canonical transcript, full32-byte tags and separate labels; `scripts/test-postmerge-wireless.sh`. Baseline assertions reproduced expensive candidate occupation, genuine Request rejection after8 bad tags, and invalid Finished abort. All now pass. |
| [SecurityState.h](../firmware/esp32/src/SecurityState.h), `Bucket::take`; [WirelessLink.cpp](../firmware/esp32/src/WirelessLink.cpp), `tick` | Atomic ECC reservation,4 recovery records/2ms including notification storms, challenge FIFO backpressure before RX dequeue.12 reconnects during40/s genuine captured Request replay, worst305 modeled ms; six independent-review attack phases worst38 modeled ms. |
| [PROT_OS7_MAX7219.ino](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino), `selectExpressionCommand/renderExpression/observeExpressionResponse`; [ResponseTiming.h](../firmware/PROT_OS7_MAX7219/ResponseTiming.h); [UsbControl.ino](../firmware/PROT_OS7_MAX7219/UsbControl.ino) | Last-emitted retargeting and causal stage/identity diagnostics; `scripts/test-postmerge-rendering.sh`, `scripts/test-latency.sh`. Supplied baseline flashed superseded artwork in336/360 cases; zero in the expanded11,664 fixed cases. Reused sender sequences have distinct observation IDs. |
| [SensorHardware.ino](../firmware/PROT_OS7_MAX7219/SensorHardware.ino), `update` | Hidden/sleep IR cancellation without touching ADC0 or connector voltage monitoring; `scripts/test-adc-sleep.sh`. Baseline pending-conversion/timeout regressions fail before the fix and pass after it. |
| [UartAuthentication.h](../firmware/PROT_OS7_MAX7219/src/HelmetProtocol/UartAuthentication.h), `challengeId/decode/advanceClock` | Public issue-time32+PRF96 ID bound to boot seed, nonce, epoch, generation and timer era. Full256-bit frame HMAC unchanged. `scripts/test-uart-admission.sh`: original single-candidate and intermediate two-slot design both failed captured-request displacement; stateless implementation passes all four modes. |
| [EspNowTransport.cpp](../firmware/esp32/src/EspNowTransport.cpp), `configure/receive/onReceive`; [WirelessTransport.h](../firmware/esp32/src/WirelessTransport.h) | Peer/source checks before enqueue, generation checks across reset, queue calls outside critical sections; `scripts/test-ingress-peer.sh`. Old admission helper accepted foreign operational DATA; new policy rejects it. |
| [install-toolchains.py](../scripts/install-toolchains.py), [toolchain.lock.json](../toolchain.lock.json), [workflow](../.github/workflows/firmware.yml) | Manifest SHA verified before parsing/downloading CLI archive. `python3 -O scripts/tests/test-toolchain-integrity.py` reproduced acceptance of a tampered manifest before the fix and rejects it after, before installation. |

The supplied ZIP SHA256 is
`2f7874948a1fccff040b7bfdc3017d3381faa322c41e940878626e0c21492b8c`;
audit SHA256 is
`c937934bde48b1dccd45ea075becd910f32a68e92c4ef27cbdc16d563aa3a298`.
Local evidence is under ignored `build/postmerge-audit/`, including preserved
harness inputs, individual RED/GREEN logs and final command/result indexes.
Tests are integrated in tracked runners rather than depending on local ZIP paths.

## Availability and compatibility boundaries

Revision2 adds two128-byte control frames:896 bytes per clean reconnect versus
640 before; each peer still has one P-256 generation and one agreement. There
is no ECC for a captured old Request. Four fixed challenge hash/source entries,
5ms reply cadence and RX backpressure bound cheap work without trusting source
MAC accounting. Invalid Finished/Commit preserves a valid candidate. Fresh live
Proof can supersede an obsolete candidate; exact retries never extend its life.
Admission subkeys/nonces/reply hashes are wiped at completion/clear.

Upgrade **both C3 helmet and paw together**. DATA stays version3/128bytes;
NVS pairing schema2 PSKs may be retained. Legacy Hello1 is rejected; no fallback
can silently restore replay-vulnerable admission. Negative compatibility and
one-loss-per-control-type tests pass. Teensy UART v3 envelope/HelmetLink framing
and pairing gestures remain compatible. The stateless UART SID is opaque to
ESP peers; it is not an abbreviated authentication tag.

[Official ESP-NOW documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/network/esp_now.html)
allows unencrypted unicast/broadcast reception without an added peer; encrypted
unicast requires matching peer/LMK. Early callback filtering is defense in depth,
not proof that the reported queue attack crosses native encryption on hardware.
No availability bound covers RX overflow, arbitrary RF loss or broadband jamming.

## Latency and power

All following milliseconds are **modeled physical-action-edge timing**, using
real application/HMAC/FFT code but idealized radio/service/ADC/SPI clocks. Target
CPU/ISR execution and RF behavior are excluded. Maxima may come from different
cases; they are not additive stage budgets.

| Authenticated UART configuration | First causal feedback | Target-expression onset | Completion |
| --- | ---: | ---: | ---: |
| Default160ms close/swap/open |98.370|180.818|228.986|
| Optional early reveal,160ms |60.762|60.762|220.658|
| Default1000ms transition |98.370|1012.874|1073.874|
| Optional early reveal,1000ms |60.762|60.762|1068.658|

`PROTOGEN_EARLY_EXPRESSION_REVEAL=0` is the artistic default; opt-in1 exposes new
eyes before closure and retains the intended duration. It is a compile-time
choice, not a persistence migration. Physical recognition and under100ms
qualification remain open. [Measurement instructions](RELEASE_LATENCY_QUALIFICATION.md)
require a shared external timebase for button release/press, causal light,
requested-eye pixels and completion, with sensor activity, idle-wake, rapid input,
UART load and RF loss classified separately.

No physical or estimated energy-saving percentage is claimed by this patch.
WFI elapsed includes ISR execution.600MHz Teensy/160MHz C3 clocks, shallow WFI,
4kHz microphone and safety sensors remain. Use the existing
[current-measurement procedure](RELEASE_POWER_QUALIFICATION.md); report visible,
hidden, sleeping and disconnected rail current separately with wake latency and
interrupt cadence. Dynamic frequency scaling remains unjustified for this scope.

## Independent review

A separate GPT-6 Astra agent independently inspected current code and rebuilt
real-crypto regressions. It found a new medium availability issue: a full cheap
challenge FIFO discarded delivered live Requests at only20 replay packets/s,
without RX overflow. Parent reproduced it before changing code and added a
failing `<4000ms` reconnect assertion across six arrival phases. Backpressure
before dequeue fixes it.

| Independent modeled-radio reproduction | Before | After |
| --- | ---: | ---: |
|20 replay packets/s, timed bursts |17288ms|38ms|
|28/s, paced arrivals |4270ms|50ms|

Reviewer reran both own reproducers and integrated wireless/rendering/UART/ADC/
ingress/integrity tests after the fix; no further concrete defect or new High/
Critical finding was identified. Callback/RTOS behavior was inspected, not
physically measured. This is an independent targeted review, not formal protocol
verification or a claim of exhaustive vulnerability absence.

## Release handoff

Experimental S31/v4 stays outside production selection. Existing P-256 +
ML-KEM-1024 algorithms, activation barriers and supported-stable SDK/memory-
protection gates are unchanged. S31 preview compilation and guarded uncalled UART
relinks are research evidence only; module GPIO/ADC/USB/power/RF, watchdog/task
stack, secure provisioning/storage, boot/encryption/debug and recovery still
require supported SDK plus physical qualification. See
[S31/v4 release gates](RELEASE_S31_V4_READINESS.md).

Brightness remains1 with explicit saved approval; EEPROM format3 atomicity and
old high-request upgrade protection are regression-tested. No upload, eFuse,
frequency/brightness increase, hardware operation or automatic merge occurred.
Final build resources, validation totals, commit and hosted CI are recorded below
and in the pull request so the final commit does not embed its own hash.


## Final build resources and validation

Compared with the merged baseline using the same pinned toolchains (bytes):

| Configuration | Flash before → after | Static RAM before → after |
| --- | ---: | ---: |
| Teensy default |76796 →79868 (+3072)|15456 →15648 (+192)|
| Teensy all sensors + paw + connector |92156 →95228 (+3072)|18080 →18272 (+192)|
| Same with authenticated UART |100348 →102396 (+2048)|19520 →19712 (+192)|
| C3 helmet |757262 →760324 (+3062)|35132 →35380 (+248)|
| C3 paw |682260 →685268 (+3008)|33148 →33380 (+232)|

The additional diagnostic state and bounded admission FIFO account for the RAM
increase; causal metrics/admission code account for flash. These sizes retain
ample configured capacity; static RAM excludes runtime heap/stacks. C3 values
are PlatformIO memory accounting, not ELF file size or summed linker aliases.
Source-bound default BIN exports are760992/685952 bytes; their difference from
flash accounting is packaging/alignment. Both roles were also built with unique
temporary development UART keys, then rebuilt in default mode; no key was
committed or provisioned to a device. The all-feature authenticated early-reveal
Teensy build also passed (102396 flash/19712 static RAM).

The64-session sanitizer host crypto benchmark observed111.918ms mean/168.661ms
max on this loaded development host,7frames/896bytes and two ECC operations per
peer. This is **measured host process time**, not S31/C3/RF reconnect timing or
an energy estimate. Host object sizes were Handshake1264/Link2928 bytes; ABI and
sanitizer memory are not target runtime resource measurements.

All commands listed below completed successfully locally (strict warnings, ASan/UBSan retained):

- `scripts/test-firmware.sh` (including configuration/EEPROM/COBS, all sensor
  masks, sensor/mic recovery and production-HMAC UART rendering),
  `test-efficiency.sh`, `test-latency.sh`, all five new targeted runners,
  `test-s31-platform.sh`, `test-wireless.sh`, `test-v4.sh`, `test-dtls.sh` and
  independent v4 vector regeneration check.
- All12 top-level Python test programs in normal **and** optimized modes
  (24 invocations), including runner portability, build provenance, provisioning
  validation and new installer integrity.
- `check-features.py` (eight real sensor builds), `check-extensions.py` (five),
  `check-uart-auth.py` (three), `build.py`, both `build-esp32.py` roles in default/
  auth/restored-default modes, `check-mlkem-preflight.py`, and `check-v4.py`.
- Isolated `check-s31-preflight.py --preview` and `check-s31-uart.py --preview`:
  pinned SDK/negative activation guards and uncalled research relinks only.
- PlatformIO MCP fresh helmet/paw builds and size reports independently confirmed
  the C3 default build accounting. No serial monitor/upload/hardware operation.

The first [hosted PR run39540100638](https://github.com/Fur-Byte/Protogen/actions/runs/39540100638)
passed the new adversarial suites but exposed a preview fixture assumption:
immediate simulated approach after wake sometimes preceded the fresh released IR
sample required to rearm. A fixed-clock local sweep reproduced89/300 failing
phases with no sensor fault. The fixture now advances10 modeled ms with released
input and explicitly asserts a fresh release/no fault before the original positive
boop assertion. Sixteen wake-phase cases and40 repeated preview suites pass in
addition to normal/optimized tests. Firmware safety policy was not weakened.
The final pushed commit requires a new successful hosted run; the failed run is
retained as reproduction evidence.

Final source identity for firmware/scripts/tools/profiles/lock:
`4a305ff3d0e24177fd8d82a92f3928cc2752d593080ab86e60548e490d13a531`.
Each build manifest was checked against that identity. The PR records the exact
published commit and hosted Actions run links once final CI completes. Rolling
Ubuntu package revisions are recorded, not claimed fully hermetic. Physical
brightness/current/thermal, latency, power, ADC and RF qualification remain open;
licensing permissions and supported S31 SDK/security dependencies remain external
release blockers. No software-test result changes those dispositions.

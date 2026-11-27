# Brightness, stored approval, and electrical qualification

Review date: 2026-10-17. Base reviewed: `1956268..3275ff8` and the complete
comprehensive remediation report plus configuration/sensor, implementation,
power/wiring and efficiency qualification documents. This follow-up preserves
`Settings::MaxBrightness = 1`; no board was accessed, flashed, or electrically
measured. The release remains **unqualified for electrical safety** until the
physical checks below have recorded results.

## Disposition and actual evidence

| Question | Classification | Evidence / limitation |
| --- | --- | --- |
| Can any normal intensity-setting path exceed the compiled ceiling? | Software verified | Driver register writes clamp at `Max72xxPanel.cpp:103`; startup, boost expiry/cancel, SET, LOAD, and dock initialization also apply the base approval. Ceiling 0/1/15 host variants inspect every intensity transaction. |
| Is intensity 1 electrically safe in this helmet? | Unverified, hardware blocked | No module RSET, LED Vf/current, supply/drop/inrush or temperature measurement is available. The ceiling limits PWM duty, not segment peak current. |
| Does a future ceiling increase activate an old requested brightness 15? | Confirmed defect, fixed | A historical schema-2 EEPROM fixture programmed 15 under a prospective ceiling-15 build before this change. The new storage approval caps that fixture at 1; the exact new-format EEPROM image saved under ceiling 1 also remains at 1 under ceiling 15. |
| Could a non-brightness Face Studio edit authorize that old request? | Confirmed defect, fixed | Apply previously sent every field, including unchanged brightness. The real inline-script event test failed, then passed after brightness was omitted unless explicitly edited or selected in a profile. |
| Is an INA228 physically connected and calibrated? | Unverified | It is optional, disabled by default. Repository wiring specifies the PD-input rail; no live identification or readings were obtained. |
| Can existing INA data be trusted merely because trim memory is healthy? | Confirmed defect, fixed | A register fixture with MEMSTAT=1 and CNVRF=0 was accepted before the fix. Now incomplete conversions are invalid and diagnostics identify the reason. |
| Can FULL hide a bad energy accumulator transition? | Confirmed defect, fixed | Actual-worker regression reproduced FULL bypassing an implausible delta, and a pending FULL request surviving a measurement fault. Both now leave SOC unknown; recovery requires fresh valid data and a new owner FULL command. |

## Actual module and power measurements

The values below must come from the assembled helmet, not a generic breakout
listing. None were measured during this review.

| Required item | Actual result available |
| --- | --- |
| Driver markings/variant and LED identification, modules 1–14 | Unknown; nominal design is MAX7219, 14 × 8 × 8 = 896 LEDs |
| RSET resistance and tolerance, each module 1–14 | Unknown; inspect resistor marking and measure appropriately with power removed |
| LED Vf at measured pulse current, temperature and color | Unknown |
| Segment peak pulse current and scan/PWM waveform | Unknown |
| Regulated face voltage at source and farthest module | Unknown |
| All-on and representative-expression average current | Unknown |
| Startup, wake, docking and fault/retry inrush/overshoot | Unknown |
| Exact supply/converter/eFuse parts, ratings, configured limits and tolerance | Planned parts documented; installed configuration unverified |
| Positive/return conductor lengths, area and connector/contact resistance | Unknown |
| Driver, converter, contact, wire and enclosure temperatures | Unknown |

A repository schematic/BOM recommendation is not an observation of installed
hardware. The optional dock's ADC divider qualifies only its sampled voltage
window, not LED current, connector temperature or RSET suitability.

## Datasheet model, not a measured current limit

The official [MAX7219/MAX7221 Rev.6 datasheet, pp.7–10](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7219-MAX7221.pdf)
sets peak segment current using RSET and LED Vf. Table 11 examples at Vf=2.0 V
are 11.8 kΩ/40 mA and 28.0 kΩ/20 mA; at 3.0 V they are 10.6 kΩ/40 mA and
24.5 kΩ/20 mA. These are selection data, not measurements of this helmet.

For eight scanned rows, intensity `b` gives MAX7219 PWM duty `(2b+1)/32`.
An enabled individual LED averages `Ipeak × PWM/8`. All 896 LEDs on therefore
average `14 × 8 × Ipeak × PWM`; do not divide that total by eight again.

| Worked example only: Ipeak=40 mA | PWM | LED-chain average, excluding driver overhead |
| --- | ---: | ---: |
| MAX7219 intensity 0 | 1/32 | 0.140 A |
| MAX7219 intensity 1 | 3/32 | 0.420 A |
| MAX7219 intensity 15 | 31/32 | 4.340 A |

Simultaneous on-pulse current can approach `14 × 8 × Ipeak`, or 4.48 A in this
example. MAX7221 uses different duty values. Intensity zero is illuminated;
shutdown is the off control. Thermal reference: per-driver
`PD = V+ × 8mA + (V+ − Vf) × PWM × Ipeak × active segments`.

The arithmetic is a model with assumed current and all-on occupancy. Unknown
RSET, component identity, Vf, tolerances and temperature prevent converting it
into an actual current bound. Reduced PWM does not reduce the segment pulse
amplitude. Scan limit remains eight rows; reducing scanned rows changes LED
average current and requires a separate review. Display-test mode bypasses
ordinary display control, so the driver explicitly clears it during startup
while shutdown before applying intensity and enabling the composed frame.

For a measured power-path resistance `Rloop`, use `ΔV = I × Rloop` and
`Pwire/contact = I² × Rloop`. Include both conductors, PCB paths, joints and
contacts, and unequal parallel-contact sharing. At an assumed 4.5 A and 0.05 Ω,
these are 0.225 V and 1.0125 W. This example does not approve a wire gauge or
contact. Measure the installed resistance/drop and hot spots. Driver junction
estimates also require actual package/board thermal conditions; enclosure,
ambient, foam and airflow make a generic package thermal resistance insufficient
for an on-body qualification.

## Explicit storage format 3 and upgrade policy

The public runtime configuration remains schema 2 with the same 17 fields and
`SET brightness=0..15` syntax. UART and wireless formats are unchanged.
**EEPROM record format changes explicitly from 2 to 3 on the next SAVE.**
Reads of older records never write EEPROM. This is a storage revision, not an
unannounced reinterpretation of schema 2.

| Byte range in a 64-byte slot | Format 3 meaning |
| --- | --- |
| 0–3 | Existing `P8CF` magic |
| 4–5 | Little-endian record format **3** |
| 6–7 | Payload length **36** |
| 8–11 | Existing sequence counter |
| 12–45 | Existing 17 requested configuration values, two bytes each |
| 46–47 | Saved approved base brightness, two bytes |
| 48–51 | CRC over bytes 0–47 |
| 52 | Existing commit-marker value `0xa5`, written last |
| 53–63 | Unused |

Formats 1 (30-byte payload) and 2 (34-byte payload) remain readable with their
original CRC/marker positions. They did not record an approval, so their
historical requests receive a fixed approval of **1**, independent of any future
compiled ceiling. Format-3 approval must be 0–15 and no greater than its saved
request. Invalid approvals, payload lengths, CRCs and uncommitted records are
rejected. The two-slot sequence and interrupted-write behavior remain intact.

Base output on startup, LOAD, wake/restoration and reconnect is:

`min(requested brightness, saved/runtime approval, compiled MaxBrightness)`.

An explicit `SET brightness=...` authorizes only the level that the currently
compiled ceiling permits. Unrelated SET fields preserve approval, including
when requested brightness itself is unchanged. SAVE records the requested value
and **approved effective base** level, excluding any temporary BOOST. GET/STATUS
add `brightness.approved`; SAVE acknowledges `record_format:3` and the saved
approval. The same field is intentionally command-specific: GET/STATUS report
the current runtime authorization; SAVE reports the approval written into the
EEPROM record (the effective base level, not a temporary BOOST). SAVE does not
mutate runtime authorization. After LOAD or reboot, the persisted approval is
the runtime approval. Consumers must not substitute a SAVE reply for a fresh
GET/STATUS snapshot. The wire format and brightness ceiling are unchanged.
Face Studio shows approval and sends a brightness field only after a
brightness edit or selection of a profile containing it. Profile export still
retains the requested value; applying such a profile is an explicit request.

A future higher ceiling must first pass hardware qualification. A recorded
approval of 1 remains 1 after that firmware upgrade until an explicit brightness
request is applied and saved. Previously approved levels can resume up to that
recorded approval when a temporarily lowered compiled ceiling is restored; the
ceiling never authorizes beyond the saved approval by itself. Runtime BOOST is
an explicit temporary request limited by the compiled ceiling; it neither raises
the base approval nor gets persisted.

Do not downgrade to firmware that predates brightness approval/ceiling support
without deliberately resetting both configuration slots and reapplying a
qualified low brightness. Such old readers reject format 3 and may select a
retained older slot; this implementation cannot make old firmware enforce new
rules. Keep the compatible firmware and EEPROM backup together for recovery.

## Existing electrical telemetry and fault handling

The documented INA228 is on **15 V PD input upstream of both converters**;
its current is not 5 V LED-branch current. The configured shunt is 15 mΩ, current
LSB `5/2^19 A`, SHUNT_CAL=1875 and ADCRANGE=0, matching the documented Adafruit
#5832 option. Verify the installed board and calibrate against a reference.
The [Adafruit board documentation](https://learn.adafruit.com/adafruit-ina228-i2c-power-monitor/overview)
identifies its shunt; that does not establish which board is physically fitted.

The [TI INA228 datasheet](https://www.ti.com/lit/ds/symlink/ina228.pdf)
provides the conversion-ready, trim-health and accumulator-overflow flags.
The worker now checks completion, stages whole readings, and publishes values
only after validation. It keeps the existing 1 Hz schedule and 10 ms I2C transfer
timeouts, waits until the next scheduled visit after configuring conversions,
and retains existing 5 s bus backoff. New counters saturate instead of wrapping.

`WearerIO::Status` adds feature-enabled state, last valid sample time, valid
sample count, failure/recovery counts and a fault reason. Numeric cached readings
must be interpreted with validity and sample age; zero samples means no measured
value exists. ENERGY diagnostics expose the upstream rail only. No monitor
reading or current estimate is invented when disabled, absent, stale or faulty.

| Energy reason | Meaning |
| ---: | --- |
| 0 | No current fault |
| 1 | I2C bus/read failure |
| 2 | Unexpected device/manufacturer identity |
| 3 | No completed conversion / waiting for first scheduled sample |
| 4 | Trim-memory failure |
| 5 | Math or energy accumulator overflow |
| 6 | Reading outside existing supported range |
| 7 | Implausible accumulator transition |
| 8 | Measurement aged beyond the existing freshness window |
| 9 | Configuration write failed |

FULL cannot waive a counter fault, and a FULL request pending at a measurement
fault is discarded. Valid recovery restores measurements; SOC stays unknown
until a new owner FULL request is processed against healthy data. Existing NVS
SOC layout and owner authorization stay unchanged. These diagnostics do not add
a fast overcurrent cutoff, temperature interlock or display-rail measurement.
The 1 Hz upstream monitor cannot qualify pulse current or inrush; the SHT40
measures its local ambient and the optional NTC measures battery-case location,
not MAX7219 junctions or connector hot spots.

## Physical qualification needed before a ceiling increase

Record the firmware hash, EEPROM format/approval, feature flags, supply and
module identities, ambient conditions, instrument calibration/bandwidth and
wiring/contact configuration for each run.

1. Inspect all fourteen RSET resistors and LED/driver identities. Capture segment
   pulse current, Vf, PWM and scan timing at the most demanding module. Compare
   against that LED's pulse/average ratings and driver limits with tolerances.
2. On a current-limited bench, start at the present ceiling. Measure all-on,
   sparse and normal frames; startup, sleep/wake, BOOST and power/reconnect
   transitions. Check near/far face voltage and ripple, peak and average branch
   current, input energy, inrush and overshoot. Capture independent measurements
   during boot and monitor faults; cross-check valid INA intervals.
3. Check worst-case continuous supply capability, converter efficiency/thermal
   derating, eFuse minimum/maximum limits and inrush against real wire/contact
   limits. Measure each suspect joint and parallel path, not just total input.
4. Run to thermal steady state in the real enclosure and airflow at the intended
   ambient/load range. Log driver, converter, connector, wire and accessible
   surface temperatures against approved component/application limits. Exercise
   partial contact, one failed parallel path and protection faults on the bench.
5. Verify low/high EEPROM fixtures through an upgrade, confirm approved output
   stays low before any explicit new request, then test one proposed higher
   level only after its electrical results justify it. Record the resulting
   approved value and SAVE acknowledgment. A successful build is not approval.

## Software verification and source map

Tests use the actual sketch, driver, storage and INA worker with simulated
peripheral boundaries, not physical measurements. Before fixing, legacy EEPROM
brightness 15 under ceiling 15, missing CNVRF, FULL/counter bypass, retained FULL
across a fault, and Face Studio's unrelated-edit payload each failed their
regressions. Each fixed case was then observed passing under ASan/UBSan where
applicable.

- `scripts/test-configuration-safety.sh`: ceiling 0/1/15; formats 1/2/3 in both
  slots; CRC-valid invalid approvals; read-only legacy loads; explicit and
  unrelated SET; sequence rollover/corruption; every interrupted byte budget
  through record plus commit margin; unchanged-save wear; exact EEPROM image
  saved under ceiling 1 then booted under 15; bounded JSON and real UI events.
- `scripts/test-efficiency.sh`: baseline and deadline scheduling each run direct
  INA register/freshness/range/I2C tests, worker conversion fault/FULL/counter
  rejection and recovery, bus fault/reconfiguration, and disabled monitoring.
- Full firmware, efficiency and Face Studio suite results are finalized below.

| Source | Relevant behavior |
| --- | --- |
| `Configuration.h:15` | Requested brightness, internal approval, effective output, explicit SET and equality |
| `ConfigStore.h:12` | Independent format 3, legacy approval 1, validation, canonical saved approval |
| `PROT_OS7_MAX7219.ino:93` / `:129` | Approved base restoration and startup |
| `UsbControl.ino:17` / `:92` / SAVE branch | Approved reporting, apply semantics, explicit format acknowledgment |
| `VisorConnector.ino:53` | Approved base on reconnect |
| `Max72xxPanel.cpp:103` | Unconditional compiled intensity ceiling, unchanged at 1 by default |
| `WearerIO.h:4` / `WearerIO.cpp:95` | Energy reason API and fresh atomic input reading |
| `WearerIO.cpp` energy task branch | Valid-only publication, bounded retry, FULL/counter fault policy |
| `tools/helmet/index.html` | Explicit brightness intent in Apply/profile payloads |
| `scripts/tests/brightness-upgrade.cpp` | Cross-firmware storage and interruption regressions |
| `scripts/tests/energy-monitor.cpp` | Actual INA worker/register fault regressions |
| `scripts/tests/test-face-studio.js` | Actual inline event-to-command regression |

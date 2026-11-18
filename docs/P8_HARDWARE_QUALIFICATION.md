# P8 physical hardware qualification

**UNQUALIFIED. All physical procedures are NOT_RUN.** The committed
[physical manifest](../hardware/p8-physical-manifest.template.json) is a blank
operator template. Its target names describe build profiles. Physical board
IDs, module revisions, calibration, and installed options are unknown.

## Bench record

1. Copy the template into the run's evidence directory. Record the UTC date,
   operator, assembly ID, and individual board markings and revisions. Confirm
   which roles and features are physically fitted. For each installed
   microphone, sensor, connector, regulator and display module capture its ID,
   role and location. Record the physical LED dimensions, tile order, rotations,
   and evidence of which cells map to the eye and mouth regions. Use `UNKNOWN`
   for remaining unknowns. Record verified absence explicitly and justify
   `NOT_APPLICABLE` against the intended release configuration.
2. Record the Git commit, build source and manifest checksums, each running
   firmware image checksum, tooling and dependencies, feature masks, UART auth
   mode, pre-test settings and approved brightness. Use exact running build
   evidence with each recording. Record any diagnostic or bench-only flags or
   instrumentation and repeat affected release-path checks with the matched
   candidate. Inherited release settings and documentation are in
   [README_RECOVERY.md](README_RECOVERY.md) and
   [RELEASE_RECOVERY.md](RELEASE_RECOVERY.md).
3. Inspect supply isolation, grounding and approved current limits before
   energizing the assembly. Identify the battery or current-limited bench
   supply, expected rails, charger and regulators, wirings and protections.
   Use approved bench wiring and safe probe references with the helmet off the
   wearer for electrical work. Avoid operating uninspected or damaged wiring.
   Record ambient and enclosure conditions, instrument IDs, calibration dates,
   uncertainty, supply state, and rail/fixture details. Use the existing
   [wiring diagrams](HARDWARE_WIRING.md),
   [connector map](../hardware/connector-map.csv), and
   [calibration ledger](../hardware/calibration-records.csv).
4. Identify and verify a matching recovery archive off-device before any
   authorized firmware restore. Store authorized configuration backups
   separately from pairing keys and provisioning information. The following
   procedures require an authorized operator and approved bench setup. P8
   performed zero flash operations and zero physical trials. Use existing
   recovery and provisioning steps when separately authorized.

## Trial matrix

Repeat across the installed release variants and the operating conditions
listed below. Before each trial, record the pass limits and required sample
count from the applicable qualification plan or component rating. If a limit,
measurement setup, or required sample count is missing, retain the data and
leave the affected gate `UNQUALIFIED`. Apply existing project brightness,
configuration and flashing policies throughout.

| Trial | Procedure and observations |
| --- | --- |
| `timing` | Capture actual loop/service intervals, interrupt cadence and duration or masked intervals where measurable, ADC gaps/drops, FFT elapsed cost, SPI CS/clock/burst lengths, and animation onset/settle. Use a shared external timebase for contacts, UART/SPI and light output. Exercise visible render, speech and boop interaction, sleeping/wake, visor disable/re-enable, UART/USB backpressure and saves. Record Teensy stack headroom with an approved calibrated high-water method and C3 main/wearer/optional UART-event task stack minima and heap under peak workload. Record methods and account for nested stages, debug probe effects and instrumentation overhead. Follow [P3 timing semantics](P3_INSTRUMENTATION.md#timing-semantics-and-qualification) and [latency qualification](RELEASE_LATENCY_QUALIFICATION.md#exact-physical-measurement-procedure). |
| `audio` | With the installed mic in its actual mounting, use consented wearer speech and repeatable quiet, speech-plus-noise, and noise-only clips. Log speaker/position, reference sound level when instrumented, loudness, distance, ambient noise and gain/configuration. Label spoken intervals independently, score missed/false-open time, onset, release, and sustained-voice recall with mouth observations and ADC/drop/rail/recovery counters. Test safe acoustic overload and return to ordinary speech, pause during boop/visibility changes, and recovery with playback stopped. Repeat legacy and Audio V2 in matched runs. P7 seed 78 missed 848 ms of labeled activity in a 250 Hz speech-plus-noise case. Include representative microphone trials for this failure mode before any Audio V2 default approval. See [P7 evidence](P7_AUDIO_BEHAVIOR.md#local-qualification). |
| `visual` | Observe the physical eyes and mouth from expected viewing angles and lighting. Record tile rotation and chain order, glyph readability, blink/glitch/mouth behavior, transitions and flicker. Correlate high-speed video or photodiode with actual SPI, including boot, sleep, wake, visibility/dock transitions, and authorized ordinary controller restarts. Verify intensity commands agree with the active approval and ceiling and record any stale light during CPU reset. Use the existing [brightness procedure](RELEASE_BRIGHTNESS_QUALIFICATION.md#physical-qualification-needed-before-a-ceiling-increase). |
| `power_thermal` | Log input and relevant board/panel rail voltage and current, rail transients, and temperatures at relevant regulators, wiring/connectors and modules. Compare idle, sleep, wake, and sustained permitted dense patterns at the active brightness ceiling with microphone/radio active where installed. Document supply settings, initial state, ambient, sampling rate and uncertainty. Use measured component limits for stop criteria and cease when instability or unsafe temperature occurs. Follow the [power procedure](RELEASE_POWER_QUALIFICATION.md#repeatable-physical-measurement-procedure). |
| `wireless` | Exercise installed helmet and paw together in the approved v3 profile with matched UART settings. Use both pairing confirms and verify physical-code mismatch rejection. Exercise benign controlled link loss, orientation/body attenuation, reconnection, repeated role restarts, packet/ACK loss, held-button behavior and session renewal. Timestamp actions and LED feedback on a shared timebase, capture losses and queue/expiry/rejection deltas, and verify stale intents or duplicate actions never execute. Use the [wireless scenarios](WIRELESS_VALIDATION.md#physical-checks-still-required-not-executed-in-this-workspace) for scope. Protect pairing material and use only the ordinary, separately authorized bench controls. |
| `recovery` | Verify the known-good archive and settings backup and record their checksums. Compare settings and authority before and after ordinary restarts, normal saves, and authorized safe fault-recovery exercises. Confirm startup display policy, recovered sensors and UART/radio session freshness, and absence of duplicate actions. When separately authorized demonstrate the documented matching-image restore and verify role hashes, settings, and local/remote behavior afterwards. Destructive power or storage interruption experiments are outside this procedure. See [release recovery](RELEASE_RECOVERY.md). |
| `one_hour_stress` | Run one continuous hour of realistic mixed load on the connected, identified setup. Document the timed script of visible animations, quiet/speech input, boop and visor changes, idle/wake, local actions, installed wireless use, and ordinary saves. Log supply/thermal and service/ADC/queue changes, resets, unexpected blanking and optical anomalies. Monitor throughout and record start and end counters and any interruption. An interrupted or short session leaves the continuous-hour requirement open. |

## Evidence and sign-off

Fill one measurement entry per trial and operating condition. Record results,
tested hardware and firmware hashes, conditions, equipment and uncertainty,
duration, values with units, counter deltas, every observed failure, and
paths and SHA-256 checksums for the supporting captures. Include response
trial counts, misses, p50/p95/p99 and maximum where applicable, and separate
healthy online, reconnecting and intentionally inactive cases. Retain hashes
of reviewed and redacted evidence. Do not export private keys, pairing secrets,
decrypted packets, or unconsented wearer audio.

Use the [qualification schema](validation/qualification-v1.schema.json) to
write F3 records with the completed manifest and instrument captures as hashed
evidence. Map the trials above to `hardware.timing`, `hardware.audio`,
`hardware.optical`, `hardware.electrical`, `hardware.wireless`,
`hardware.recovery`, and `hardware.one-hour` respectively. Use F3 for
measurements made on the identified physical assembly and retain the relevant
`NOT_RUN` or `UNQUALIFIED` state for missing evidence. `experimental.approval`
requires a separate decision after the affected measurements are reviewed.

Before the trials, record the intended assembly ID and installed board IDs
from inventory. Obtain the exact Git SHA, per-role image digests and feature
flags from the independently verified release build and retain this candidate
file separately from the measurement records. For example, the file shape for
a Teensy and a helmet radio is shown below. Replace every illustrative value
with the actual identity and include every installed role and active setting.

```json
{
  "source_sha": "<full Git SHA>",
  "assembly_id": "<assembly ID>",
  "boards": {"teensy": "<Teensy board ID>", "helmet": "<helmet radio board ID>"},
  "firmware_sha256": {"teensy": "<HEX SHA-256>", "helmet": "<helmet firmware.bin SHA-256>"},
  "feature_flags": {"Microphone": 1, "WirelessPaw": 1, "PROTOGEN_AUDIO_V2": 0}
}
```

Use the same role names in each F3 record's `hardware.boards` and
`hardware.firmware_sha256`. Report the observed assembly ID and feature flags
with each run. Legacy parallel arrays remain readable as board/image pairs
in index order. Release readiness requires records with named roles and an
independent candidate file. Omitting the file still verifies the evidence
and leaves physical release gates unqualified.

```sh
python3 scripts/qualification.py verify "$qualification_manifest" \
  --artifact-root "$evidence_directory" --source "$exact_source_sha" \
  --candidate "$candidate_file"
```

Any physical gate required for the intended build remains open until its
measured conditions, results and supporting hashes have been reviewed.

## Prioritized bench preparation

The available physical assembly and instruments are **unknown**. No device
or optional module has been confirmed by this qualification record. First
inventory the helmet, installed modules and calibrated instruments using the
manifest. With the corresponding equipment identified and operator approval,
run these trials in order.

1. Teensy 4.0 service gaps, ADC drops and stack headroom using a suitable
   timing instrument and approved stack measurement method.
2. MAX7219 mapping, blanking, readability and flicker using the actual panels
   and, where timing matters, a photodiode and oscilloscope.
3. Installed microphone, representative wearer speech and mixed background
   noise using independently labeled trials. Include the P7 848 ms missed-speech
   case and compare matched legacy and Audio V2 profiles.
4. Supply voltage, current and thermal behavior using a suitable multimeter,
   current measurement and temperature instrument under rated load.
5. Helmet and paw ESP32-C3 communication, if both roles are installed, including
   reconnect, latency and stale-command rejection.
6. Verify and restore a matching known-good firmware bundle under separately
   authorized procedures, then verify settings and command freshness.
7. Run the documented connected one-hour workload and retain measurement
   files, operator identity, instrument details and hashes.

A Teensy board, panels, microphone, power instrumentation, oscilloscope,
photodiode and both wireless roles are required only for their respective
applicable trials. Their current availability is unconfirmed. Missing
equipment and unknown installation details remain open in the manifest.

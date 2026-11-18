# P8 recovery, reliability and release qualification

P8 starts from the verified P7 merge
`2741065bf2ff01a1ca25f9094bce54938a8acd01`. The release verdict is
**production blocked** pending physical evidence and release approval.
A host simulation, target build, or synthetic recovery archive can establish
only its own gate.

## Recovery and supervision

The main loop remains the sole Teensy owner of configuration, animation and
foreground service. Existing sensor state machines own fault detection,
backoff and retry. The ADC ISR queues samples only. The C3 roles retain their
existing task, packet admission and recovery ownership. Existing output and
security authority remain with their original components.

`PROTOGEN_SUPERVISION_OBSERVE` defaults to 0. The optional Teensy observer
records service epochs and a maximum observed gap for foreground completion,
render attempts and foreground microphone drains. The foreground records
progress at the end of a pass. While the display is hidden, sleeping or
unavailable, display and audio progress are not required. Paused or recovering
audio is exempt until sampling resumes. USB and UART receive traffic is not a
condition for progress. The existing synchronization and service scheduling
are used in every profile.

| Observer | P3 recorder | Expected |
| --- | --- | --- |
| 0 | 0 | Legacy |
| 0 | 1 | Independent P3 recorder |
| 1 | 0 | Observer and local USB diagnostic |
| 1 | 1 | Both independent mechanisms |

The soft thresholds are 200 ms for awake foreground and 64 ms each for
render and audio. Elapsed time uses unsigned differences. Availability and
staleness are updated by the owner. When progress resumes, an over-budget gap
still appears in the latched count. After a deliberate save the actual elapsed
storage time is credited up to 100 ms for the next observation, and longer
saves remain visible in the storage counters. These are diagnostic thresholds
and do not define qualified timing limits. The foreground can report a
missed opportunity only after it runs again. There is no independent detection
or automatic recovery.

`SUPERVISION` on the existing local USB interface returns bounded aggregate
counts and times. It is enabled only in an observer build. The observer
holds no packet contents, audio, credentials or durable state. It executes
constant work per pass and adds no tasks.

`python3 scripts/check-supervision.py` compares complete host fixture output
across all four profiles. The fixture enables Animation V2, timelines, Audio
V2, face cache and early reveal and checks rollover, sleep, save, paused
audio and a foreground stall. `python3 scripts/check-supervision.py --target`
captures actual Teensy linkage, memory and static frame estimates. Host
timing is not a measurement of the Teensy.

## Evidence and soak

The [v1 schema](validation/qualification-v1.schema.json) distinguishes
static/build analysis, executable unit tests, integrated host simulation and
measurements from identified physical equipment as F0 through F3.
`scripts/qualification.py record` executes a command on a clean exact source
checkout and records its exit status and log hash. `verify` checks every
artifact against the supplied source SHA and records readiness per gate.
`merge` combines independently collected records for the same SHA. A passed
F3 stress result requires at least 3,600 continuous seconds and identified
boards, instruments, firmware, conditions and operator. A physical gate can
contribute to release readiness only when the named board roles, installed
image hashes, assembly and feature flags match a separately supplied candidate.
See the [candidate record format](P8_HARDWARE_QUALIFICATION.md#evidence-and-sign-off).
Trust in who
conducted a test and the correctness of its oracle still require review.
Artifact hashes detect changed contents and are not release signatures.

```sh
python3 scripts/qualification.py verify evidence.json --source FULL_GIT_SHA --artifact-root build/p8
python3 scripts/check-invariant-mutations.py --group full
```

The [soak procedure](P8_VIRTUAL_SOAK.md) defines compact seed evidence,
failure reproduction and the exact coverage report. Two seeds per shard, a
120-second per-seed deadline, a 300-second shard wall budget and four
concurrent CI shards bound the manual workflow. It builds one pinned host
twin and aggregates every shard, including incomplete coverage. Ordinary
PR CI checks the focused faults without scheduling the long soak.
The release verifier checks the retained aggregate's binary and runner hashes,
passing shard references and coverage. Recomputing that aggregate requires
the original shard files.

The [release and restore procedure](RELEASE_RECOVERY.md) requires a clean
source checkout, matching exact build manifests and toolchains, artifact
hashes and compatible feature/authentication modes. Previous archives require
independently retained SHA-256 evidence and complete internal verification.
Automated rejection tests use synthetic fixtures. An actual restore to
physical boards remains a separate production gate.

## Outstanding release gates

| Gate | Required evidence |
| --- | --- |
| Software compatibility | Existing frozen rendering, EEPROM, protocol and feature regression tests |
| Integrated recovery | Deterministic faults, resource bounds and exact soak coverage |
| Target compilation | Full legacy Teensy matrix, both C3 roles and focused opt-in profiles |
| Timing | On-device ADC drops, service gaps, stack high-water and render/SPI/FFT measurements |
| Acoustic | Identified microphone, representative speech including mixed background noise, calibration and recovery |
| Optical | Physical panels, blanking, readability, transitions and maximum intensity |
| Electrical and thermal | Measured power, rails, regulators and one-hour representative connected load |
| Wireless | Both identified roles, pairing, reconnect, loss, freshness and latency |
| Rollback and recovery | Identified actual matching bundle and verified restoration |
| Experimental approval | Separate approval tied to real hardware evidence |

P7 reported 848 ms of missed speech in a mixed-background synthetic case.
Audio V2 remains opt-in while representative acoustic evaluation is pending.
The physical manifest has unknown hardware fields and recorded trials
`NOT_RUN`. The [hardware procedure](P8_HARDWARE_QUALIFICATION.md) lists
required measurements. The [watchdog and retention report](P8_WATCHDOG_RETENTION.md)
covers pinned mechanisms, reset-loop hazards and the residual risk of lit
MAX7219 pixels during CPU failure. Watchdog feeding, persistent crash writes
and reset-retained production storage require separate approval.
See the [reliability inventory](P8_RELIABILITY_INVENTORY.md) for existing
coverage and gaps.

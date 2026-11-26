# Teensy recovery candidate

This is an opt-in candidate, not a production approval. The normal build
continues to use neither the hardware watchdog nor the new idle setup.
ESP32-C3 firmware is unchanged.

## Activation contract

Build the identified Teensy image with the pinned toolchain, using
`PROTOGEN_CONTROLLER_RECOVERY=1`, `PROTOGEN_SUPERVISION_OBSERVE=1` and a
nonzero `PROTOGEN_RECOVERY_BUILD_ID` (64-bit unsigned integer). Assign a new
identity from the exact source snapshot, toolchain/dependencies and effective
build configuration. Retain the full digest and the resulting HEX digest in
the build manifest. Never reuse an identity across different images. A 64-bit
identifier is a diagnostic discriminator, not a security credential.

The foreground owner starts WDOG1 only after setup and a completed service
pass. The candidate timeout is 2 seconds; feeding is limited to once per
100 ms after a completed pass with required rendering and audio progress.
The save path checkpoints before writing, but never feeds or extends the
deadline. Startup before arming is not covered. An already enabled watchdog
is not taken over. This is not a measured timeout selection for the assembled
helmet; a long flash operation can still cause a reset.

The two 64-byte cache-line-aligned OCRAM slots hold only numeric metadata:
identity, publication sequence, retry count, stage, uptime and service
counters. The inactive slot is invalidated, body flushed, and commit marker
published last; CRC detects accidental corruption. No EEPROM or radio key
writes occur for diagnostics. On a trusted same-image warm reset, the previous
checkpoint is available via the read-only local USB `RECOVERY` command.
It is advisory, not a complete crash trace or proof of exact fault location.
The raw reset cause is sampled at setup entry, not before ROM/core startup.
The data is not guaranteed to survive power removal, firmware updates,
brownout or every reset domain.

The policy accepts an exact power-on cause as a fresh start; an exact software
or watchdog cause requires a valid matching journal. Unknown/mixed causes
and incompatible/missing records lock out arming. Three watchdog resets
without 30 seconds of healthy progress lock out the watchdog; normal USB
and controller service remain available. This is a watchdog lockout, not
a fail-dark or safe-output guarantee. A new cold start may clear the lockout.
No reset is requested by the diagnostic reader. The counter cannot establish
a lifetime limit across cold resets or a bootloader that misreports causes.

## Idle candidate

`PROTOGEN_IDLE_RUN_MODE=1` selects RUN (LPM=00) only if the observed
CLPCR value is exactly 0x79 and deep sleep/sleep-on-exit are absent.
It leaves other fields intact, changes the register once during setup and
continues to apply the original context, clock and SysTick guards at each WFI.
The combined profile skips this setup when watchdog arming is locked out.
It does not enter SoC WAIT or STOP, or rewrite clock gates. NXP's
[RT1060 A errata](https://www.nxp.com/docs/en/errata/IMXRT1060CE_A.pdf)
and [B errata](https://www.nxp.com/docs/en/errata/IMXRT1060CE_B.pdf)
describe ERR050143; do not loosen the original WAIT/STOP guard to work around
the issue. No battery savings are established by counting WFI calls alone.

## Qualification

Run the recovery journal host test (also included in the firmware suite).
Run `python3 scripts/check-recovery-build.py` using the pinned toolchain
for default, bare-board and all-feature builds and ELF layout checks.
The healthy interval restarts after any foreground gap over 200 ms or
unhealthy required service observation.
Check exact target ELF placement, including no overlap with heap, crash
report, other DMA objects or cache lines. A separately hashed bench snapshot
may add a one-shot stall command, but it must never be in the release image.
Test normal sleep/wake, save, repeated resets, lost/corrupt journal,
different identity, healthy interval clearing, and blocked interrupts.
Restore the known-good image and settings afterward. Repeat all applicable
physical gates with the actual display, sensors, radios and instrumented
power supply before enabling either option by default.

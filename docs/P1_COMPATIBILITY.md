# P1 compatibility and regression checks

The production sources, sketch settings, toolchain lock, artwork, and
hardware definitions are byte-identical to the pinned
[baseline](P0_BASELINE.md).

## Review the renderer fixtures

`scripts/tests/fixtures/rendering-v1.tsv` pins the source commit and the
57 checkpoints defined by `scripts/tests/golden-rendering.cpp`. Each row
contains shutdown state, transaction counts, 112 logical bytes in row-major
MSB-first order, and 112 SPI bytes in tile-major digit order after decoding the
daisy chain. Power-up driver initialization precedes the recorded deltas.
The test independently checks rotation 3, one complete 28-byte chain write
per changed digit, no write for an unchanged frame, and the face loaded at
wake before enabling the outputs.

All four archive-era resting logical frames match the original ZIP and the
verified historical CI artifact. The independent archive decoder also checked
the four complete physical outputs. Representative manual mapping checks
included Happy tile 0 digit 2 `0x60`, tile 6 digit 5 `0x3f`, and tile 13
digit 2 `0x06`. The fully closed transition leaves the eye tiles blank.
The saved coupled cases include continuity when the latest expression
supersedes an in-flight choice, the fixed and sampled follow-up blink,
retained registers during shutdown, a visibility debounce boundary, and the
hidden boop expiry. Transition, blink, and boop checks cross millis rollover.
The mouth checkpoints use controlled
audio state and FFT bitmap bytes. The existing firmware suite separately
exercises microphone and physical-sensor simulations and the existing
post-merge suite checks 5,832 expression sequences in each reveal mode.

```sh
python3 scripts/check-golden-frames.py
python3 scripts/compare-archive-faces.py
```

Normal verification only reads the fixture. To audit its original capture,
create a separate clean worktree at
`520fca6b3858d5d711366e8c11afdf340e7b7e57`, then run
`python3 scripts/check-golden-frames.py --capture-baseline <worktree> --output <new-review-path>`.
This command refuses to overwrite the committed fixture. An intended visual
change requires a reviewed diff and independent evidence for the changed
logical or physical outputs.

## Review configuration compatibility

`tools/helmet/config-metadata.json` is compiled from the actual
`Configuration.h` table, `RuntimeConfig` defaults, and `ConfigStore.h`
constants. It exports public field names in wire order, integer bounds,
defaults, offsets, storage versions, and the brightness ceiling/approval
defaults. The Face Studio test checks that its field list and numeric bounds
match this export and that unrelated edits preserve the saved approval.
Cross-field constraints remain in firmware `validConfig` and have dedicated
negative cases. Approval is private storage metadata and separate from the
public brightness field. Use `--write` only to regenerate a reviewed export.

`scripts/tests/fixtures/eeprom-v1.hex` contains independently packed
little-endian 64-byte slots with standard CRC-32 in this order:
v1 sequence 1, v2 sequence 1, v3 sequence 1, v3 sequence 2, and v3 sequence 3.
The harness checks the decoded configuration, exact bytes from current writes,
separate CRC and marker corruption, and every possible interruption after a
changed byte before the final commit. Recovery is tested for both migration
from existing legacy records and saving over a used slot. The test checks
that a retry produces the exact expected bytes and storage beyond the two
slots stays intact.

```sh
python3 scripts/generate-config-metadata.py --check
bash scripts/test-config-compatibility.sh
node scripts/tests/test-face-studio.js
```

## Review build and mutation coverage

Fast CI uses five plain Teensy masks, including the untouched default sketch,
two UART-auth masks, the face cache with all five features and auth, and both
auth settings for each C3 role. Full CI runs on the weekly schedule, version
tags, and manual dispatch. It adds both Teensy production partitions and the
combined C3 bench profile. C3 helmet builds
verify presence or absence of the linked UART routine. The paw uses radio
in both configurations and does not contain the helmet-only UART bridge.
C3 roles share installed packages, Arduino cases reuse the core cache with
separate object paths, and CI parallelizes the Teensy groups. The independent
host efficiency suite still checks individual candidate flags.

```sh
source build/toolchains/environment.sh
python3 scripts/check-baseline.py
python3 scripts/check-build-matrix.py fast teensy
python3 scripts/check-build-matrix.py full teensy --group plain
python3 scripts/check-build-matrix.py full teensy --group auth
python3 scripts/check-build-matrix.py full esp32
PROTOGEN_PIO_JOBS=4 python3 scripts/build-efficiency.py case_2hz --with oled_1hz --with oled_pages --with oled_protection --with checkpoint_5min --with wearer_deadlines --with main_events
python3 scripts/check-invalid-configurations.py
python3 scripts/check-invariant-mutations.py --group full
```

The full local production matrix passed 32 plain and 16 authenticated Teensy
cases plus four C3 role/auth cases. Bench checks cover the Teensy face cache and
the combined C3 profile. Each compile-rejection probe requires its
specific guard diagnostic and runs alongside working controls. The mutation
runner verifies a passing clean control, edits temporary host sources, and
requires the intended regression assertion or rejection. Eight detected
mutations cover the brightness ceiling and saved approval, EEPROM CRC and
marker, freshness and duplicate handling, a bad efficiency flag, and
authentication/replay ordering. Build and mutation manifests record the
individual cases and source status. No mutated images are built for hardware.

Default Teensy and both default C3 static figures matched the baseline. The
full-feature Teensy build used 95,228 bytes flash and 18,272 bytes RAM1 variables
without UART authentication, and 102,396 / 19,712 with auth. RAM2 variables
remained 12,416. The authenticated helmet C3 image used 764,268 bytes linker
flash and 35,980 bytes static RAM. The paw stayed at 685,268 / 33,380 in both
auth build modes. The isolated combined C3 bench profile used 764,782 / 36,420
for the helmet and 688,314 / 33,380 for the paw.

The default Teensy HEX matched the pinned build byte for byte. All eleven
application object files matched per C3 role after stripping debug sections.
The paw's allocated ELF contents matched except for the project version and
build timestamp. A helmet rebuild after generating the PlatformIO compilation
database also changed the SDK driver link order. The map placed SPI and I²C
driver archives in a different order, with identical linker occupancy and
resolved SDK configuration. Full-image bit reproducibility therefore remains
unverified for the C3 helmet. The physical qualification items identified in P0
remain open.

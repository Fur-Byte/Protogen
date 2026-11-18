# Matching firmware and USB recovery

Keep recovery bundles private. Authenticated firmware images can contain the
provisioned UART key even when the ZIP contains no standalone key file. Check
[redistribution requirements](../THIRD_PARTY_NOTICES.md) before distributing
any source or firmware build.

Rebuild Teensy and both wireless roles from matching current sources with the
pinned toolchain. Use a clean Git checkout and run `python3 scripts/build.py`
and `python3 scripts/build-esp32.py` from the same commit. Keep generated outputs
under the ignored `build/` directory. Then create a checksummed development bundle.

```sh
python3 scripts/package-release.py --output build/releases/release.zip \
  --previous /path/to/verified-previous-release.zip \
  --previous-sha256 "$PREVIOUS_RELEASE_SHA256" \
  --configuration /path/to/helmet-profile.json
```

`PREVIOUS_RELEASE_SHA256` comes from the independently retained record of the
previously verified bundle. For a first bundle, use `--initial-release` and omit
both previous-release arguments. The manifest records the absence of a rollback
bundle. Keep the known-good archive and its digest off the helmet and verify
every file against `release.json` before flashing.

Packaging checks Git status and the exact commit independently of the hashed
build inputs. Both successful build manifests must identify the current commit
and source hashes. A documentation-only commit still requires fresh builds.
Git may ignore build output outside the source directories. Within `firmware`,
`scripts`, `tools` and `profiles`, unexpected untracked files and ignored
inputs are rejected. The generated ESP32 compilation database and Python bytecode
under script and tool cache directories are permitted. Put the output in an
ignored build directory or outside the checkout. The packager publishes the
ZIP only after all checks pass and preserves an existing output path.

`release.json` schema 2 records the exact Git commit, source-content hash,
build compatibility, configuration-backup presence, rollback digest and a
hash of every payload. It labels all output as development with physical
qualification pending. This is a local build and archive integrity check.
Production approval requires the physical evidence in the qualification
procedures.

Matching source hashes alone do not establish UART compatibility. The packager
also checks authenticated UART modes when Teensy's `WirelessPaw` is enabled and
records selected features and bridge availability in `release.json`. A default
Teensy image has optional wireless control disabled; bundling radio images does
not enable it. Authenticated images still require the correctly provisioned
per-helmet key and a hardware handshake check. Treat authenticated firmware as
per-helmet material and keep its bundle private. Store the provisioning key
separately from the firmware and configuration backups. Keep the UART provisioning
header outside the checkout and point the existing build tooling at that directory.
Previous recovery ZIPs must contain the Teensy HEX, both radio roles' firmware,
bootloaders and partition tables, toolchain pins, wiring and recovery
references, and matching successful build evidence. The packager verifies the
independent archive digest, rejects duplicate or unsafe paths, and checks that
archived files match both the archive manifest and the build evidence. A
schema-1 bundle can be used if the retained digest, contents and its build
evidence satisfy those checks. A recovery ZIP is limited to 256 MiB of
uncompressed data.

The bundle contains Teensy HEX, matching helmet/paw firmware/bootloader/partition
images, build manifests, dependency provenance/licenses/bundled sources, toolchain
pins, wiring references, and these instructions. Installer download caches retain
additional pinned toolchain/dependency archives and sources; preserve
`build/toolchains` and `build/platformio` alongside the release for offline rebuilds.
Optional schema-2 helmet backups contain only validated settings/calibration;
pairing secrets, NVS dumps and arbitrary key headers are excluded. Configuration
backup absence is recorded rather than silently replaced by defaults.

Read local diagnostics with
`python3 scripts/export-diagnostics.py --port /dev/ttyACM0 --output diagnostic.json`.
Its allowlisted numeric/boolean status and PERF fields exclude pairing material.
The exported `configuration` object can be saved as a helmet profile for the
packager and restored through the existing settings tool, then explicitly saved.
For the ESP devices record `INFO`, `PERF` and `SECURITY STATUS` locally; none of
these commands exports the pairing key. Re-pair through the physical procedure
after replacing a wireless module; do not store pairing secrets in repair logs.

For Teensy recovery, isolate USB/battery power according to
[HARDWARE_WIRING.md](HARDWARE_WIRING.md), connect a micro-USB data cable, select
Teensy 4.0 / USB Serial / 600 MHz with the locked core, and load the verified HEX
using Teensy Loader. Press the physical Teensy program button if automatic reboot
fails. Restore the settings profile and explicitly save it after checking the
connected optional hardware. Recover helmet and paw together using the matched
legacy development artifacts and the board-specific USB/flashing instructions in
`firmware/esp32/README.md`; preserve NVS unless intentional physical re-pairing is
required. The partition-table bundle identifies each image, not a generic S31
flash offset. Follow the existing provisioning/recovery policy for secured boards;
do not burn eFuses or disable secure boot to recover them.

Maintain actual module/connector identifiers and dated calibration records in
`hardware/calibration-records.csv`. Record fitted shunt/NTC, battery/charger module
identification and manufacturer limits, probe/meter serials, calibration methods,
measurements and the applicable firmware SHA. A blank ledger means unrecorded
calibration. Use `docs/HARDWARE_WIRING.md` plus `hardware/connector-map.csv` as the
single wiring reference and update them together when a real repair changes it.

Run the isolated packager regression tests with
`python3 scripts/tests/test-release-packaging.py`. They create temporary Git
checkouts and archives with synthetic payloads that must never be flashed.

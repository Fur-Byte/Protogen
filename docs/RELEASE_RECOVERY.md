# Matching firmware and USB recovery

Keep recovery bundles private. Authenticated firmware images can contain the
provisioned UART key even when the ZIP contains no standalone key file. Check
[redistribution requirements](../THIRD_PARTY_NOTICES.md) before distributing
any source or firmware build.

Rebuild Teensy and both wireless roles from matching current sources with the
pinned toolchain. Then create a non-overwriting, checksummed development bundle:

```sh
python3 scripts/package-release.py --output build/releases/release.zip \
  --previous /path/to/verified-previous-release.zip \
  --configuration /path/to/helmet-profile.json
```

For the first release only, explicitly use `--initial-release` in place of
`--previous`. This records the lack of a previous release; it never fabricates
one. Verify every file against `release.json` before flashing. The packager
verifies the previous ZIP before including it, keeps its checksum, checks current
build states/source identities and artifact hashes, and leaves existing bundles
untouched. Keep a separate copy of the verified previous bundle off the helmet.
Bench images and unqualified S31 images are not production releases.

Matching source hashes alone do not establish UART compatibility. The packager
also checks authenticated UART modes when Teensy's `WirelessPaw` is enabled and
records selected features and bridge availability in `release.json`. A default
Teensy image has optional wireless control disabled; bundling radio images does
not enable it. Authenticated images still require the correctly provisioned
per-helmet key and a hardware handshake check. The tool adds no standalone key file.
Previous recovery ZIPs must contain the Teensy HEX, both radio roles' firmware,
bootloaders and partition tables, toolchain pins, and matching successful build
evidence. A checksum-valid but incomplete archive is rejected.

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

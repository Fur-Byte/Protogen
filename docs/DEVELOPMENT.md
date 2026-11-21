# Build, preview, and test Protogen

The Teensy 4.0 runs the face and handles local controls. The ESP32-C3-DevKitM-1 in the helmet
bridges wireless commands to the Teensy. The Seeed XIAO ESP32-C3 runs the paw. Check the
[wiring guide](HARDWARE_WIRING.md) and [C3 firmware guide](../firmware/esp32/README.md)
for the board variants and supported connections. The S31 and v4 work is separate research.

## Install the pinned tools

Work from the repository root on macOS or Linux. Use Python 3.11 or later and a C++17-capable
Clang. The full host suite also uses CMake, Ninja, and Node.js. The
[toolchain lock](../toolchain.lock.json) records Arduino CLI, Teensy core and library versions,
and the PlatformIO and ESP-IDF versions used for development builds.

```sh
python3 scripts/install-toolchains.py
source build/toolchains/environment.sh
```

Use `--teensy-only` with the installer to skip the C3 tools. Source the environment file in
each shell that uses the installed tools. The installer writes under `build/`.

## Compile or preview

| Task | Command | Result |
| --- | --- | --- |
| Build the selected Teensy features | `./scripts/build.sh` | HEX and matching manifest in `build/teensy40/` |
| Build the C3 helmet and paw | `python3 scripts/build-esp32.py` | Role binaries in `build/esp32/` and `build/esp32-build.json` |
| Open Face Studio simulation | `python3 scripts/helmet-tool.py` | Local preview at `http://127.0.0.1:8765` |
| Replay with the digital twin | `make -C scripts/twin` then `python3 scripts/twin/replay.py scripts/twin/fixtures/visibility.json` | Deterministic host trace |

Use the [USB and settings guide](UPGRADE.md#use-the-preview-and-configuration-tool) when
connecting an installed Teensy. Preview uses the host compiler and the FFT source from
`ARDUINO_FFT_SRC`. Close serial monitors before connecting Face Studio to the hardware.

## Pick a regression check

| Changed area | Focused check |
| --- | --- |
| Rendering and panel writes | `python3 scripts/check-golden-frames.py` |
| EEPROM and configuration | `bash scripts/test-config-compatibility.sh` |
| Wireless and admission | `bash scripts/test-postmerge-wireless.sh` |
| Face Studio and timeline replay | `node scripts/tests/test-face-studio.js` and `python3 scripts/check-timelines.py` |
| Recovery packaging | `python3 scripts/tests/test-release-packaging.py` |

`python3 scripts/check-baseline.py` runs the consolidated host suites. For a full target
matrix, run `python3 scripts/check-build-matrix.py full teensy` or
`python3 scripts/check-build-matrix.py full esp32` for the affected target. Follow
[the CI policy](../AGENTS.md) when the PR workflow already provides that evidence.

## Read the build evidence

For a Teensy build failure, start with `build/current-build.json` and the compile log named
by its run directory. A prior HEX can remain after a failed attempt. Use its adjacent
manifest to verify the source, features, and toolchain before using the image. For C3,
start with `build/esp32-build.json` and `build/esp32-build.log`. Follow the
[recovery guide](RELEASE_RECOVERY.md) before packaging or restoring images.

## Qualification and publication

Host tests and target builds are software evidence. Physical release gates remain in the
[bench qualification matrix](P8_HARDWARE_QUALIFICATION.md), including timing, RF, optics,
power, recovery, and the continuous-hour trial. For production provisioning requirements,
see [production provisioning](PRODUCTION_PROVISIONING.md).

GitHub currently lists this repository as public. The
[publication audit](PUBLIC_RELEASE_READINESS.md) documents unresolved rights and
redistribution work. Follow [the third-party notices](../THIRD_PARTY_NOTICES.md) before
sharing source, generated material, or firmware packages.

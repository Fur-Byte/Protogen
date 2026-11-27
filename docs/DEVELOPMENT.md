# Build, preview, and test Protogen

The Teensy 4.0 runs the face and handles local controls. The ESP32-C3-DevKitM-1 in the helmet
bridges wireless commands to the Teensy. The Seeed XIAO ESP32-C3 runs the paw. Check the
[wiring guide](HARDWARE_WIRING.md) and [C3 firmware guide](../firmware/esp32/README.md)
for the board variants and supported connections. The S31 and v4 work is separate research.

## Install the pinned tools

Work from the repository root on macOS or Linux. Use Git, Python 3.11 or later with
`venv`, Make, and a C++17-capable Clang. The full host suite also uses CMake, Ninja, and Node.js. The
[toolchain lock](../toolchain.lock.json) records Arduino CLI, Teensy core and library versions,
and the PlatformIO and ESP-IDF versions used for development builds.

The lock also records SHA-256 source-content identities for the Teensy core,
arduinoFFT, Adafruit GFX, Adafruit BusIO and bundled ADC. These pins were
derived from the official PJRC and Arduino Library Manager release archives
after matching their hashes to the freshly retrieved package indexes on
2026-11-27. The tree identity hashes a sorted JSON map of relative file paths
to SHA-256 contents; only the Arduino CLI-generated core `installed.json`
is excluded. Build evidence also checks the libraries actually selected by
the compiler, and release packaging rejects missing or mismatched evidence.

The installer additionally checks cached core/library archives if present.
This is not an end-to-end authenticated toolchain or reproducible build:
compiler executable trees, PlatformIO packages, ESP-IDF and Python
transitive dependencies still need independent content pins. Update hashes
only by reviewing and verifying upstream release artifacts, not by accepting
the contents of a failing local installation. Existing manifests without
the new content evidence require a fresh build before packaging.

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

## Find the owner and the regression check

| Entry point | Contract to preserve | Focused check |
| --- | --- | --- |
| [Teensy foreground loop](../firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino) and [BehaviorCore](../firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp) | The loop admits inputs and commits SPI writes. The core computes the pose for the supplied time. | `python3 scripts/check-golden-frames.py` |
| [ConfigStore](../firmware/PROT_OS7_MAX7219/ConfigStore.h) | EEPROM records and brightness approval must survive interrupted saves and reloads. | `bash scripts/test-config-compatibility.sh` |
| [C3 application](../firmware/esp32/src/main.cpp), [wireless link](../firmware/esp32/src/WirelessLink.cpp), and [wearer task](../firmware/esp32/src/WearerIO.cpp) | The link checks expiry before dispatch. The wearer task publishes coherent sensor snapshots. | `bash scripts/test-postmerge-wireless.sh` and `bash scripts/test-efficiency.sh` |
| [Face Studio server](../scripts/helmet-tool.py) and [controls](../tools/helmet/studio.js) | USB commands are serialized. A refresh failure can follow an acknowledged mutation. | `python3 scripts/tests/test-helmet-tool.py` and `node scripts/tests/test-face-studio.js` |
| [Timeline compiler](../scripts/timelines/compiler.py) and [simulator](../scripts/timelines/simulator.py) | The preview runs the compiled asset in the sketch twin. Equal-time event order matters. | `python3 scripts/check-timelines.py` |
| [Recovery packager](../scripts/package-release.py) | Each new bundle binds the role images to their build evidence and identifies the separate rollback bundle by digest. | `python3 scripts/tests/test-release-packaging.py` |

`python3 scripts/check-baseline.py` runs the consolidated host suites. For a full target
matrix, run `python3 scripts/check-build-matrix.py full teensy` or
`python3 scripts/check-build-matrix.py full esp32` for the affected target. Follow
[the CI policy](../AGENTS.md) when the PR workflow already provides that evidence.

Changes to shared firmware kernels also have [formal and native assurance checks](../verification/README.md).
Use the [coverage map](../verification/coverage.md) to find the matching wrapper tests,
input bounds, and assumptions before changing a kernel. The formal runner has its own
pinned x86_64 Linux tools.

## Read the build evidence

For a consolidated host run, `build/baseline-host/manifest.json` lists the command,
exit code, and log path for each suite. Open the log for the failed suite to find
the first diagnostic.

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

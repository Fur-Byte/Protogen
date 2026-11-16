# P0 firmware baseline

The clean, non-shallow `Fur-Byte/Protogen` checkout began on `main` at
`520fca6b3858d5d711366e8c11afdf340e7b7e57`, the exact architecture review
commit. The commit-to-HEAD diff was empty. Baseline tests and builds ran in a
separate detached worktree from that revision.

## Repeat the checks

Install the pins in [toolchain.lock.json](../toolchain.lock.json) with
`python3 scripts/install-toolchains.py`, or use an existing installation with
the same versions. The installer writes `build/toolchains/environment.sh`:

```sh
source build/toolchains/environment.sh
python3 scripts/check-baseline.py
python3 scripts/build.py
python3 scripts/build-esp32.py
```

The host runner records commands, exits, skips, compiler versions, source
identity and logs in `build/baseline-host/`. The builders write
`build/current-build.json`, `build/runs/*/manifest.json` and
`build/esp32-build.json`. Host tests use the existing C++17 test doubles and
ASan/UBSan by default. See the workflow for dedicated experimental preflights
and target combinations. CI's experimental checks compile or execute isolated
code and do not qualify experimental platform support.

## Historical CI provenance

[Run 39666666802](https://github.com/Fur-Byte/Protogen/actions/runs/39666666802)
published artifact 12381783108. The downloaded ZIP SHA-256 was
`b89b1848a0474fc4ca274eac5147937f8ae5a22e0e2a09d71037c58370742402`.
All 319 tracked inputs listed in each of 12 Teensy manifests match the
materialized pinned checkout byte for byte. The extra input in each was
`firmware/esp32/compile_commands.json`, which the installer's
`pio run -t compiledb` generates there before the builds. This accounts for an
untracked file and explains the dirty flag without evidence of changed tracked
inputs. The old manifests lack complete status entries, so unrelated historical
changes outside the recorded inputs cannot be ruled out. The exact generated
path is now ignored and excluded from the input digest. Future manifests retain
the full Git porcelain status, including other untracked files.

The original ZIP was retrieved from the local Git tree. Its hash matches the
historical CI artifact. The independent archive test reproduced all four
archive-era resting faces at all 896 logical and 896 physical bits per face.
See [the artwork comparison](ARCHIVE_FACE_COMPARISON.md).

## Build configurations and static occupancy

The local pinned builds used Arduino CLI 1.5.1, Teensy core 1.62.0, the
`teensy:avr:teensy40:usb=serial,speed=600,opt=o2std` FQBN, PlatformIO 6.1.19,
espressif32 7.1.3, ESP-IDF 6.1.0 and Mbed TLS 4.1.0. The lock lists selected
Arduino libraries, their versions and the source hashes for bundled Entropy.

| Default build | Flash occupancy, bytes | Static RAM, bytes | Additional figures |
| --- | ---: | ---: | --- |
| Teensy 4.0 | 79,868 | RAM1 15,648; RAM2 12,416 | 443,104 RAM1 local-variable space; 511,872 RAM2 heap-region space |
| C3 helmet | 760,324 / 1,536,000 | 35,380 / 327,680 | 760,992-byte binary |
| C3 paw | 685,268 / 1,536,000 | 33,380 / 327,680 | 685,952-byte binary |

These default builds reproduced the historical static sizes. The historical
artifact also reports Teensy all-features flash/RAM1 at 95,228/18,272 bytes
with UART auth off and 102,396/19,712 bytes with auth on.

The Teensy defaults disable VisorSensor, BoopSensor, Microphone, WirelessPaw
and VisorConnector. All eight bench efficiency flags and early expression
reveal default off. Compiled display geometry is 112×8 over fourteen MAX7219
tiles. Brightness ceiling is 1, with existing persisted approval enforcement.
C3 helmet and paw use distinct board profiles and radio v3. UART authentication
is optional and off in the default images. The SDK defaults disable secure
boot, flash encryption and NVS encryption.

Actual fitted options, wiring, flashed versions, electrical brightness
qualification and physical performance were unverified. The linker figures
carry no message-latency, runtime stack/heap, thermal, RF or power measurements.

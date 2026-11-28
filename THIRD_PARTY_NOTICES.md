# Third-party notices and redistribution status

This inventory records source and license evidence for the private development
repository. Each upstream license applies to its own material. There is no
repository-wide license grant. See the [publication gates](docs/PUBLIC_RELEASE_READINESS.md)
before sharing a source snapshot, build, recovery bundle or CI output.

## Tracked source, archives and assets

| Material | Provenance and terms | Redistribution work |
| --- | --- | --- |
| Feronium's PROT_OS7 7.0 source, art and original Nano schematic | The sketch header in `archive/PROT_OS7_original.zip` credits Feronium. The sketch and artwork have no verified redistribution grant. The ZIP's library licenses cover those libraries. The tracked Nano schematic is byte-identical to the ZIP copy. | Obtain written permission covering original and modified code, artwork, schematic, and generated renderings, or exclude them and independently replace the required parts. Retain author attribution. Keep the original ZIP and its macOS metadata out of any clean public snapshot. |
| Current PROT_OS7 firmware and outputs | `firmware/PROT_OS7_MAX7219/Icons.h` preserves original bitmap data. The active `FFT.ino` and main sketch have matching source spans. `FaceRenderer.h` composes the original art, and the blink, glitch, expression, microphone and scheduler paths evolved from the imported sketch. See the [archive comparison](docs/ARCHIVE_FACE_COMPARISON.md). | Review file-level ancestry before granting rights. Include `docs/archive-face-comparison.png`, frame and replay fixtures, audio/animation demos, CI traces, preview outputs and compiled firmware in the artwork/provenance review. |
| Protogen Head by ZillionRoss | [Thingiverse design 4268402](https://www.thingiverse.com/thing:4268402). Both downloaded ZIP notices say Creative Commons Attribution without a version or license URL. All 53 Part 1 files and 13 Part 2 files match their extracted copies. | Confirm the exact license version and its scope across geometry, images, PDF, instructions and logos. If confirmed, credit ZillionRoss and the title, link the source and exact license, retain required notices and identify any changes. Until then, exclude all of `assets/head-model/` from a clean export and link to the original listing. |
| MAX7219 driver, archived and locally modified | Mark Ruys, [arduino-Max72xxPanel](https://github.com/markruys/arduino-Max72xxPanel), BSD-3-Clause. The archived and active copies retain the upstream notice. The active README documents the project's SPI, storage and bounds changes. | Retain the license, attribution and disclaimer in source and reproduce them with binary distributions. The license restricts use of names for endorsement. |
| Archived arduinoFFT 1.5.6 | Didier Longueville and Enrique Condes, [arduinoFFT](https://github.com/kosme/arduinoFFT). The ZIP contains a GPLv3 copy and source headers permitting GPL-3.0-or-later. | Preserve original notices, license and covered source for any redistribution. Apply the linked-firmware review below to old binaries as well. |
| Archived Adafruit GFX 1.9.0, fonts and examples | Adafruit Industries' BSD-2-Clause text is in the ZIP. Fonts include converted GNU FreeFont headers and individually credited assets such as Tom Thumb. The GFX library notice alone does not establish their terms. See [GNU FreeFont's terms](https://www.gnu.org/software/freefont/FAQ.html). | Exclude the archive in a clean export. If proposing standalone redistribution of an archived dependency, check the exact upstream revision and each bundled font/example notice and source obligation. |
| Mbed TLS UART subset and configured headers | Mbed TLS contributors, [v3.6.5](https://github.com/Mbed-TLS/mbedtls/tree/v3.6.5). Upstream dual-license headers permit the Apache-2.0 selection. Source/header hashes and modifications are recorded in `firmware/PROT_OS7_MAX7219/src/UartCrypto/UPSTREAM.json`. | Retain the bundled license, original headers, applicable NOTICE material, and prominent modification notices. The repository's authentication wrapper and build configuration have separate provenance. |
| Experimental mlkem-native | [Project authors, v2.0.0](https://github.com/pq-code-package/mlkem-native/tree/d1b2fe782888bdb761a50336012923180be7f502). The code offers Apache-2.0, ISC or MIT. The portable subset selects MIT. The vendored `PROVENANCE.json` and test pins identify source and archive hashes. | Retain the MIT license and copyright notices for the selected files. The 42 recorded source hashes matched. Test-only vectors and upstream test programs retain their notices. |
| Protocol vectors, host fixtures and tooling | `scripts/tests/v4/make_vectors.py` generates the tracked deterministic v4 vectors. `mlkem-preflight-pins.json` separately pins downloaded NIST ACVP datasets and standards. The UART fixture, EEPROM and replay samples are test data. | Preserve upstream vector provenance and terms if exporting those datasets. Review frame-bearing fixtures and artwork-derived demo output separately from numeric protocol vectors. Verify ownership of local scripts, schemas, profiles, documentation and hardware drawings before applying a new grant. |

Some archived files have no exact upstream commit recorded. Keep the copies and
their notices available for private recovery. Record the origin and grant for
each file included in a proposed public export.

## Pinned build dependencies

| Dependency | Verified evidence | Distribution implications |
| --- | --- | --- |
| arduinoFFT 2.0.4 | [Tagged source and license](https://github.com/kosme/arduinoFFT/tree/v2.0.4). Source credits Didier Longueville, Enrique Condes and Bim Overbohm and specifies GPL-3.0-or-later. | Microphone-enabled Teensy builds link FFT code. Inventory covered linked source and keep notices together with exact corresponding source and build scripts. |
| Entropy bundled with Teensy 1.62.0 | Walter Anderson, [upstream project archive](https://code.google.com/archive/p/avr-hardware-random-number-generation/). Installed `Entropy.h` and `Entropy.cpp` specify GPL-3.0-or-later and are pinned by hash in `toolchain.lock.json`. | The default Teensy sketch also links Entropy for blink randomness. Removing the optional microphone feature leaves a GPL dependency in the default image. |
| Adafruit GFX 1.12.6 and BusIO 1.17.4 | Adafruit Industries, [GFX BSD-2-Clause notice](https://github.com/adafruit/Adafruit-GFX-Library/blob/1.12.6/license.txt) and [BusIO MIT notice](https://github.com/adafruit/Adafruit_BusIO/blob/1.17.4/LICENSE), checked in the installed packages. | Preserve the GFX notice and disclaimer with distributed sources or binaries. Include the MIT copyright and permission notice with copies or substantial portions of BusIO. Review separate bundled files if shipping the libraries wholesale. |
| Teensy core 1.62.0 and ADC 9.1 | [PJRC / Paul Stoffregen Teensy cores](https://github.com/PaulStoffregen/cores). The installed Teensy 4 core has permissive notices, including a PJRC board-selection condition in some files. [Pedro Villanueva's ADC](https://github.com/pedvide/ADC) carries MIT terms in the installed bundle. | Preserve notices and conditions for the exact linked core and ADC source. Review bundled libraries, compiler runtime, and any installer binary independently if including them in an offline kit. |
| ESP-IDF 6.1.0, Mbed TLS/PSA 4.1.0, and ESP32-C3 blobs | [Espressif ESP-IDF](https://github.com/espressif/esp-idf) carries Apache-2.0 for its own code. [Mbed TLS contributors](https://github.com/Mbed-TLS/mbedtls) and [TF-PSA-Crypto contributors](https://github.com/Mbed-TLS/TF-PSA-Crypto) publish their separate terms. SDK components and precompiled radio archives carry their own notices. | Record the installed package, linked objects, licenses and notices for each helmet/paw image. Check the exact C3 radio blobs and bootloaders before redistributing the images or SDK. S31 preflight files have separate pins and remain research. |
| Arduino CLI 1.5.1 and PlatformIO Core 6.2.0 | [Arduino CLI](https://github.com/arduino/arduino-cli/tree/v1.5.1) is GPL-3.0-or-later. [PlatformIO Core](https://github.com/platformio/platformio-core/tree/v6.2.0) is Apache-2.0. Both are external build tools. | Their use to build firmware is distinct from redistributing the tools. If shipping an offline kit, inventory exact transitive packages, compilers, tool binaries, sources and notices from that kit. |

### Proposed licensing scope

For independently authored Teensy code whose ownership the maintainer has
confirmed, propose **GPL-3.0-or-later** to align with the linked Entropy and FFT
libraries. Candidate project-authored files include new behavior modules and host
build helpers once their ancestry and contributor rights are confirmed. Apply
the grant only to an approved file list, with SPDX headers, copyright notices
and the full GPL text. The existing terms for separately licensed upstream
pieces continue to apply. No license is being applied here to the imported
sketch, bitmaps, model, archive, or other material with unresolved ownership.

Before distributing a linked Teensy image, confirm compatible grants from all
rights holders and meet GPLv3 sections 4–6 as applicable, including license text,
notices, corresponding source for the exact build, and installation information
when required. Start from the actual linked-object list. The private recovery
ZIP is a maintenance bundle and needs an independent source/notice check before
distribution. See [the GPLv3 text](https://www.gnu.org/licenses/gpl-3.0.html).

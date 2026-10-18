# Attribution and license inventory

This is an inventory, not a new license grant. No repository-wide license is
assigned by this remediation. Upstream notices and the recovery archive remain
intact; distributing a combined firmware requires checking its dependencies'
terms as well as obtaining any missing permission for the original sketch.

| Component | Evidence and license status |
| --- | --- |
| Original PROT_OS7 7.0 sketch and artwork | `archive/PROT_OS7_original.zip`, sketch header dated 12-06-2022, identifies Feronium. The archive contains library licenses but no license grant for the original sketch or artwork was found. Preserve attribution; the maintainer must obtain/record the author's permission before claiming a license for that material. Historical contact details in the archive are not this project's security reporting channel. |
| MAX7219 driver | Mark Ruys, 2013, BSD three-clause notice in `firmware/PROT_OS7_MAX7219/src/Max72xxPanel/license.txt`; retained. Local safety/bounds/pilot changes do not replace that license. |
| arduinoFFT 2.0.4 | Enrique Condes, [upstream](https://github.com/kosme/arduinoFFT), GPL-3.0-or-later according to the installed pinned source/license. It is an external build dependency; original archive includes its older license. |
| Adafruit GFX / BusIO | External pinned dependencies. BSD notices are supplied by their respective upstream packages and must accompany distributions as required. Original GFX notice remains in the recovery archive. |
| Mbed TLS UART subset and headers | Apache-2.0 notices and provenance in `firmware/PROT_OS7_MAX7219/src/UartCrypto/LICENSE` and `UPSTREAM.json`; ESP-IDF Mbed TLS/PSA source remains an external pinned dependency. |
| ESP-IDF / Teensy core / bundled ADC and Entropy | External pinned toolchain and libraries; their own source notices apply. ESP-IDF includes separately licensed binary Wi-Fi components; do not describe every SDK component as Apache-2.0. |
| Experimental mlkem-native | Pinned upstream v2.0.0 commit and hashes in `scripts/tests/mlkem-preflight-pins.json`, retained MIT license selection. No change of backend/license, cryptographic internals, or production activation. |
| Protogen Head model | **Protogen Head by ZillionRoss**, [Thingiverse thing 4268402](https://www.thingiverse.com/thing:4268402), both parts under `assets/head-model`. Supplied `LICENSE.txt` says Creative Commons Attribution, but does not specify a version. Retain those files; verify the exact version with the author/source before representing a specific CC BY version. No geometry was modified in this remediation. The upstream page returned HTTP 403 during this review. |

The original sketch's missing grant remains unresolved; absence of a LICENSE
file must not be interpreted as permission. Head-model credit above records the
available downloaded notice without inventing broader rights.

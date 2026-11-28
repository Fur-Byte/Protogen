# Native ESP-IDF migration candidate

This is an experimental alternative, not a production release backend. The
existing PlatformIO workflow and release packager remain authoritative. The
native build uses the pinned ESP-IDF 6.1.0 source package, RISC-V GCC 15.2,
CMake, Ninja, ROM ELF data and hash-locked IDF Python environment. It does
not execute PlatformIO, SCons, or its esptool postinstall hooks; the SDK and
tool archives are still obtained from the reviewed PlatformIO registry mirror.
The supported candidate host is Linux x86-64 / CPython 3.13.

## Commands

Run these in a fresh checkout with Python 3.13 and Git:

```sh
python3 scripts/protogen.py doctor
python3 scripts/protogen.py setup --backend native
python3 scripts/protogen.py build all --backend native --output build/native-first
python3 scripts/protogen.py audit --dependencies
python3 scripts/protogen.py test --all
python3 scripts/protogen.py verify --formal --tier extended
```

For the host regression suite, first install the reference toolchain and
source its environment script, and provide the host compiler/analyzer
prerequisites described in DEVELOPMENT.md. Reference build requests for either
radio role compile both roles, by design. Use the existing packaging script
for development recovery bundles; `protogen.py release --verify` accepts a
bundle and an independently retained `--sha256` for read-only validation.

The native setup creates new SDK and Python directories, never overwrites
existing installations, and publishes a completion marker only on success.
A failed install is not automatically repaired or promoted. Archive cache
entries for native downloads are keyed by expected SHA-256 and validated
before extraction. To reuse verified external caches, pass
`--wheelhouse <directory>` and `--archives <directory>` (flat archive names).
For a different SDK/venv location, use `scripts/native_esp.py --sdk-root ...
--python-root ...`. The native environment does not inherit arbitrary
compiler or Python flags. The build records SDK/tool content hashes,
installed distribution versions, source identity, artifacts, warning counts,
and SPDX 3.0.1 JSON-LD from Espressif's SBOM tool. The SBOM tool may omit
the mirror-packaged compiler; that limitation is recorded alongside the
separately pinned compiler tree. Python versions and lock evidence are not
a proof that installed Python file contents were never altered. The
bootstrap interpreter, pip, system tools and OS remain trust roots.

## Migration evidence and gates

The first minimal-component native prototype built both roles, but
MBEDTLS_HARDWARE_RSA_DS_PERIPHERAL differed from the reference. Retaining
the full SDK component discovery removed that difference in the helmet
configuration. Exact JSON configuration comparison and byte-for-byte
partition comparison are mandatory before cutover. Firmware binaries are
not assumed identical. Native use of the official CMake project avoids the
reference SCons compile stage. Compare first-party flags, ELF symbols and
memory occupancy, run the full existing assurance suites and qualify UART
authentication, experimental v4, S31 research, and assembled hardware before
switching defaults. Do not use experimental artifacts in the current release
packager or flash them on production hardware.

The actual pinned ARM GCC 15.2.1 and ESP RISC-V GCC 15.2.0 compiled C23 and
C++23 probes (`nullptr`, `if consteval`, `std::expected`) without warnings.
No first-party runtime source or default language flags changed in this
migration. The SDK's native CMake already selects its vendor language flags;
a standard bump must be justified by full target and behavior qualification.

The native CI workflow installs the actual locked native and reference
environments separately, builds both roles, and fails on configuration or
partition drift. Optional manually dispatched clean-output reproducibility
experiments compare binary, bootloader and partition bytes; this is not
independent-host reproducibility. Optional attestation runs only on main
after comparison and uses a pinned GitHub provenance action. These
attestations do not establish SLSA Build Level 3 or production approval.
No signing key is stored in the repository. Cross-host SDK installation,
independent-host reproducibility and the physical gates remain outstanding.

In the full-component prototype, the 1,359 helmet and 1,356 paw SDK
configuration keys matched exactly and both partition tables were identical.
Reference/native application sizes were 762,704/762,592 bytes (helmet)
and 687,952/687,856 bytes (paw). These were development builds without UART
authentication, not physical qualification. The comparison CI additionally
rejects an unreviewed binary size increase above the larger of 4 KiB or 1%;
this is a review threshold, not a performance guarantee.

Fresh extracted SDKs must identify ESP-IDF 6.1.0, not the enclosing
Protogen Git checkout. Native setup writes the exact locked SDK version
marker; the build checks it against the generated project description.
The SBOM is an inventory, not a vulnerability assessment. The pinned
SBOM tool normally synchronizes external CVE exclusions if its cache is
empty, which failed under the CI offline proxy. Native generation supplies
an explicit empty external exclusion file, disables synchronization, and
does not emit VEX assertions. This prevents reliance on ambient caches or
external vulnerability data during the build. Independently review current
vulnerability information separately before release.

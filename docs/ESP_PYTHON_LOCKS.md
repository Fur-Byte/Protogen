# Verified ESP development Python lock sets

These locks capture the tested Linux x86-64 CPython 3.13.5 environment:
22 PlatformIO dependencies, 62 ESP-IDF dependencies (including setuptools)
in the two remaining runtime groups. The selected artifacts
were downloaded and each SHA-256 was independently matched to non-yanked
release metadata from PyPI on 2026-11-27. The esptool 5.7.0 source distribution
declares setuptools >=77; the lock uses 84.2.1 and builds offline without
build isolation. No unpinned build backend is fetched by the helper.

Prepare a local wheelhouse using the exact versions and hashes in the runtime
lock files, with subdirectories platformio and idf. Place
esptool's pinned source archive in idf alongside the wheels. Then run:

    python3 scripts/install-locked-python.py --wheelhouse /path/to/wheelhouse --output build/locked-python

The helper creates new isolated environments only, requires hashes, disables
index access and cache reuse, checks dependencies, and records success only
after both requested environments finish. A failed install may leave an incomplete
new output directory; it must not be reused as verified. The helper never
modifies the existing development venvs or the installed PlatformIO packages.

Other Python ABIs/host platforms need separate wheel verification. Hash
locking release artifacts is not a guarantee of source-code safety or
bit-for-bit deterministic wheels from source distributions.

## Default installation candidate

On Linux x86-64 with CPython 3.13, the normal installer now selects the
locked provisioning path. It downloads the exact pinned release artifacts
and verifies them before extraction or installation. Instead of invoking
PlatformIO package postinstall hooks, it creates the SDK package tree from
the independently verified archives, creates the locked Python environments,
and prepares the upstream IDF venv metadata.
The build wrapper disables pip index access and requires hashes when it sees
the locked-install evidence. This is artifact identity evidence, not a
guarantee that an installed Python environment was never modified.

Use a fresh checkout/build directory on the qualified host. Existing
environments are deliberately not overwritten. An interrupted run requires
a fresh build directory rather than trusting partially installed packages.
For other Python ABIs or operating systems, the installer emits a warning
and uses the previous unqualified Python dependency resolution path.

The isolated helper also accepts an independently prepared local wheelhouse
and archive directory for offline provisioning.

## Compatible modern dependency updates

The default qualified development lock uses PlatformIO 6.2.0 (its
Starlette requirement is now <2), Starlette 1.10.0, urllib3 2.8.0,
and IDF-environment cryptography 48.0.1. Espressif's reviewed constraint was <49. Do not raise that bound without
upstream support and separate qualification.
The installer accepts multiple SHA-256 pins, downloads only matching,
non-yanked wheels (plus the explicitly locked esptool source archive),
and pip chooses a compatible artifact offline. The main C3 CI job uses
Python 3.13 to exercise the actual locked installation, rather than its
legacy unqualified fallback. Automated updates must pass that job.

The reviewed parent compatibility ranges are recorded separately in
`esp-compatibility.json` and checked inside each installed environment,
for the two Python environments. These are the relevant constraints
for this change, not a replacement for the full upstream SDK support matrix.
Do not auto-merge version updates that pass only source tests: actual locked
provisioning, pip metadata consistency, parent compatibility and target
compilation are required.

PlatformIO 6.2.0 also changed its core SCons requirement to ~4.41101.0
(SCons 4.11.1). A CI build compiled both roles successfully but rejected
the changed tool tree because PlatformIO had replaced the old 4.40801.0
package. The registry archive and extracted tree for 4.41101.0 are now
independently hash-pinned. Setup checks the parent's version requirement;
the locked build checks actual SCons resolution with auto-install disabled
before invoking the compiler, and still verifies package trees afterward.


## Reference uploader migration (2026-11-28)

PlatformIO espressif32 8.0.0 replaces the Python esptool 4.11.0 package
and its postinstall dependencies with the standalone esptool 5.4.0
release package (registry version 2.50400.0). The locked Linux x86-64
archive and all installed files are hash-pinned. Inspection of the
PyInstaller archive tables in its espsecure and espefuse executables
identified cryptography 50.0.1 metadata. The esptool executable itself
did not include cryptography metadata. The obsolete 46.0.7 contrib lock
and installation path are removed, not overridden. There is no generated
contrib subtree exemption in the installed-tool verifier.

This supersedes the incompatible Dependabot request in PR #77. The
separate SDK Python environment remains on cryptography 48.0.1 under
Espressif's constraint; this is not a blanket vulnerability remediation
or an assertion that the vendor binaries are vulnerability-free.
CVE-2026-69247 is fixed upstream in cryptography 50.0.0; the old SDK
environment still requires separate dependency/security review.

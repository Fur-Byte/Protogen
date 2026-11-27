# Verified ESP development Python lock sets

These locks capture the tested Linux x86-64 CPython 3.13.5 environment:
22 PlatformIO dependencies, 62 ESP-IDF dependencies (including setuptools)
and nine PlatformIO esptool postinstall dependencies. The selected artifacts
were downloaded and each SHA-256 was independently matched to non-yanked
release metadata from PyPI on 2026-11-27. The esptool 5.7.0 source distribution
declares setuptools >=77; the lock uses 84.2.1 and builds offline without
build isolation. No unpinned build backend is fetched by the helper.

Prepare a local wheelhouse using the exact versions and hashes in the three
lock files, with subdirectories platformio, idf and esptool-contrib. Place
esptool's pinned source archive in idf alongside the wheels. Then run:

    python3 scripts/install-locked-python.py --wheelhouse /path/to/wheelhouse --output build/locked-python

The helper creates new isolated environments only, requires hashes, disables
index access and cache reuse, checks dependencies, and records success only
after all three installs finish. A failed install may leave an incomplete
new output directory; it must not be reused as verified. The helper never
modifies the existing development venvs or the installed PlatformIO packages.

This is a verified installation candidate, not the default firmware installer.
The upstream PlatformIO package postinstall hooks and IDF environment
bootstrap can independently install dependencies. Before switching the normal
build to these locks, integrate the isolated environments with those hooks
and demonstrate an offline build of both C3 roles without package mutations.
Other Python ABIs/host platforms need separate wheel verification. Hash
locking release artifacts is not a guarantee of source-code safety or
bit-for-bit deterministic wheels from source distributions.

## Default installation candidate

On Linux x86-64 with CPython 3.13, the normal installer now selects the
locked provisioning path. It downloads the exact pinned release artifacts
and verifies them before extraction or installation. Instead of invoking
PlatformIO package postinstall hooks, it creates the SDK package tree from
the independently verified archives, creates the locked Python environments,
and prepares the upstream IDF venv metadata and esptool dependency directory.
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

# ESP build tool archive tree integrity

The Linux x86-64 pins were independently derived from nine release archives
whose SHA-256 values matched freshly retrieved PlatformIO registry metadata
on 2026-11-27. The installed files matched the archive bytes, resolving
hardlinks and checking the exact relative symlink targets. GDB packages
are optional; absent optional packages are recorded, not silently treated
as verified. The build wrapper verifies tool archive trees before invoking
the build and checks them again before accepting successful build evidence.

The verifier excludes PlatformIO package metadata, generated Python
bytecode and the esptool package's separately installed `_contrib` directory.
The latter is explicitly unverified by this tree check. The independent
Python dependency locks address source artifact identity, but the upstream
postinstall hook must still be integrated with those locks to qualify the
complete default installer. No assurance claim is made for other host
architectures, concurrent local tampering, or interpreter binary contents.
The lock requires independent upstream review for updates.

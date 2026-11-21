# Contributing

Start with the [developer guide](docs/DEVELOPMENT.md) for setup, focused tests,
and build evidence. Use a topic branch and open a pull request against `main`.
When changing behavior, keep the firmware, replay fixtures and generated metadata consistent.

In the pull request, name the affected hardware and feature flags. List the
tests you ran and identify which results came from host tests, simulations,
target builds or physical boards.

- Use the toolchain versions pinned in [toolchain.lock.json](toolchain.lock.json). See the
  [regression commands](README.md#audit-and-regression-checks) and
  [hardware documentation](docs/HARDWARE_WIRING.md).
- Let the automatic PR workflow run the fast checks. Use a manual full-matrix
  run when build coverage or stress tests are needed. Use ephemeral
  test provisioning and synthetic data in CI.
- Keep formatting changes scoped to eligible files. The polish drafts use
  clang-format 20.1.8, Black 25.1.0, and Prettier 3.6.2 for the files in those
  diffs. Apply the local configuration to named paths. Preserve inherited and
  vendored files, generated outputs, and frozen evidence.
- Keep calibration records identifying specific devices, UART and signing keys,
  wireless pairing material, unreviewed captures and provisioned images in
  private operational storage. Use [SECURITY.md](SECURITY.md) for vulnerability
  reports.
- Document the upstream source and exact license for new code, assets, test
  vectors and generated derivatives. Retain required notices and explain
  modifications. See [third-party notices](THIRD_PARTY_NOTICES.md) and
  [publication status](docs/PUBLIC_RELEASE_READINESS.md).

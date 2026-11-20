# Contributing

Use a topic branch and open a pull request against `main`. When changing behavior,
keep the firmware, replay fixtures and generated metadata consistent.

In the pull request, name the affected hardware and feature flags. List the
tests you ran and identify which results came from host tests, simulations,
target builds or physical boards.

- Use the toolchain versions pinned in [toolchain.lock.json](toolchain.lock.json). See the
  [regression commands](README.md#audit-and-regression-checks) and
  [hardware documentation](docs/HARDWARE_WIRING.md).
- Let the automatic PR workflow run the fast checks. Use a manual full-matrix
  run when build coverage or stress tests are needed. Use ephemeral
  test provisioning and synthetic data in CI.
- Keep calibration records identifying specific devices, UART and signing keys,
  wireless pairing material, unreviewed captures and provisioned images in
  private operational storage. Use [SECURITY.md](SECURITY.md) for vulnerability
  reports.
- Document the upstream source and exact license for new code, assets, test
  vectors and generated derivatives. Retain required notices and explain
  modifications. See [third-party notices](THIRD_PARTY_NOTICES.md) and
  [publication status](docs/PUBLIC_RELEASE_READINESS.md).

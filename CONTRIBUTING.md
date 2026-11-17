# Contributing

Use a topic branch and open a pull request against `main`. Describe the affected
hardware and feature flags. Report the tests you ran and distinguish host,
simulated, target-build and physical-board results. Firmware, replay fixtures,
and generated metadata should agree when a change intentionally affects behavior.

- Use the pins in [toolchain.lock.json](toolchain.lock.json). See the
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

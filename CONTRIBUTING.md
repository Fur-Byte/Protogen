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
- Keep calibration records identifying specific devices, UART and signing keys,
  wireless pairing material, unreviewed captures and provisioned images in
  private operational storage. Use [SECURITY.md](SECURITY.md) for vulnerability
  reports.
- Document the upstream source and exact license for new code, assets, test
  vectors and generated derivatives. Retain required notices and explain
  modifications. See [third-party notices](THIRD_PARTY_NOTICES.md) and
  [publication status](docs/PUBLIC_RELEASE_READINESS.md).

## Check source formatting

Stage new source files, then run `python3 scripts/check-format.py --list-tools` to see
which formatters your changes use. Put the listed tools on `PATH` and run
`python3 scripts/check-format.py`. Both commands compare the working tree to its
common ancestor with `origin/main`. Pass another base branch for a stacked PR.

CI uses Black 25.1.0, clang-format 20.1.8, and Prettier 3.6.2 with the repository
configurations. The check covers new files and edits to files that were already
formatted in the selected first-party source directories. Legacy files enter the
check after a formatting change lands. The [path selection](scripts/check-format.py)
keeps inherited sketch modules, vendored code, generated metadata, and fixtures outside
the check. Fix the named files locally and review the diff before committing.

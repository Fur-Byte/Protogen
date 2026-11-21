# Engineering source inventory

The [file inventory](engineering-inventory.tsv) covers all 538 paths tracked at
`ca70d4d22318570e2f31dec662a3316f5688d48f`. It records one primary decision per
path. The inventory combines ownership and ancestry review, static consistency
checks, and review of the build and test entry points. The classification is a
scope decision for this pass.

| Classification | Files | Decision |
| --- | ---: | --- |
| GOOD | 279 | Keep the existing code, test, or document structure. |
| STYLE | 36 | Improve the presentation where the change has a focused verification path. |
| MAINTAINABILITY | 10 | Simplify a specific responsibility or retain the current boundary where it controls behavior. |
| CORRECTNESS | 20 | Track the confirmed issue in its remediation draft. |
| EXCLUDED | 193 | Preserve upstream, inherited, archived, historical, or generated material. |

## Where changes belong

- **Firmware.** Present the ADC1 recovery, dock timing, and protocol status logic clearly.
  Preserve inherited sketch modules, artwork, initialization, and fixed capacities. The
  wearer worker, UART handling and wireless recovery keep their existing ordering.
- **Host tools and tests.** Format selected compressed entry points and assertion
  files. Preserve command arguments, modeled time, fixture data, and the evidence
  format. S31/v4 preflight files need their own experimental verification scope.
- **Face Studio.** Put the markup, stylesheet, application script, and timeline
  editor in focused files. Verify the HTTP allowlist, preview, and keyboard controls.
- **Onboarding.** Keep historical audits intact. Use the developer guide to describe
  the current targets, qualification boundaries, setup, and build evidence.

## Integrate with remediation

Drafts [#37](https://github.com/Fur-Byte/Protogen/pull/37) through
[#42](https://github.com/Fur-Byte/Protogen/pull/42) own the confirmed storage,
USB, build evidence, soak, cold-start, and rollback findings. The Face Studio source
draft stacks on #38 because both change the HTML and HTTP handler. After #38 merges,
rebase the Studio draft onto that version of `main` and rerun the browser and HTTP
checks. The other polish batches have separate scopes based on `main`.

The formatter configurations and new files introduced by the drafts have clear
single responsibilities. They are classified as GOOD for the proposed layout:

- `.clang-format` and `pyproject.toml` pin the formatting settings used by the
  selected firmware and Python files. `tools/helmet/.prettierrc.json` scopes
  formatting to the Studio assets.
- `tools/helmet/studio.css` contains the existing visual rules.
  `tools/helmet/studio.js` owns the application controls and USB session state.
- `docs/DEVELOPMENT.md` directs contributors to setup and verification.
  This overview and the TSV record review scope and decisions.

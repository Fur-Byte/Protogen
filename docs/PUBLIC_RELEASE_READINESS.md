# Public repository readiness

**NO-GO for making `Fur-Byte/Protogen` public.** Keep the development repository
private. Feronium's original material remains in the active sketch and in Git
history. The model license version is unresolved, the linked Teensy firmware
needs a GPL distribution plan, and retained history and GitHub records include
personal metadata. These publication gates are separate from firmware
qualification and hardware readiness.

## Baseline and scope

Audit date **2026-11-17**. GitHub's default branch was
`2741065bf2ff01a1ca25f9094bce54938a8acd01` (P7) with 11 remote branch heads,
no tags, and 11 advertised pull-request refs. The audit included all local refs
and the fetched P8 synthetic merge ref. Of 91 reachable commits, 80 were
non-merges. GitHub listed nine merged PRs and one open draft at
`b72b805cbe29dcd74e4ee852500d50b723e3b145`. That draft contains the P8
qualification changes and manual virtual soak. It remains separate from this
branch. [P8 draft](https://github.com/Fur-Byte/Protogen/pull/10)

| Tracked area at the baseline | Files | Contents and exposure |
| --- | ---: | --- |
| `firmware/` | 175 | Teensy sketch, legacy C3 helmet/paw, local driver and UART crypto, S31 research and isolated v4 vendor code. Artwork and legacy implementation require provenance review. |
| `scripts/`, `tools/`, `profiles/` | 201 | Test and build code, four configuration presets, deterministic vectors, frozen pixel/behavior fixtures and Face Studio. Keep frame-bearing data in the rights review. |
| `archive/` | 1 | Original PROT_OS7 ZIP with sketch, schematic, three bundled libraries and Mac metadata. |
| `assets/` | 68 | Two original model ZIPs and 66 extracted files. Includes STL geometry, logos, PDF, images and attribution. |
| `docs/` and `hardware/` | 55 | Engineering, licensing and qualification evidence, including two tracked illustrations tied to the archive. The calibration ledger contained headers only. |
| Repository root and `.github/` | 7 | Policies, toolchain lock and firmware workflow. No root license. |
| **Total** | **507** | Includes 3 ZIPs, 1 gzip fixture and 1 Intel HEX fixture containing synthetic EEPROM records. |

## Confirmed findings

| Severity | Finding and evidence | Status |
| --- | --- | --- |
| High | Original sketch, artwork and schematic first imported at `96db7c4`. `Icons.h` preserves original arrays, and `FFT.ino` and the main sketch retain matching source spans. Four resting faces match the archive pixel for pixel. | Publication blocked. Scope and alternatives in [third-party notices](../THIRD_PARTY_NOTICES.md). |
| High | Entropy in the default target and arduinoFFT in microphone builds specify GPL-3.0-or-later. The original rights and inventory of exact corresponding source for distributed builds are unresolved. | Source and binary release review required. |
| High | GitHub reports `main` as unprotected. Classic protection and effective rulesets are unavailable on the current private plan. | Owner action required. See GitHub settings below. |
| Medium | Both Thingiverse ZIPs name ZillionRoss and Creative Commons Attribution. The cached listing and local notices supplied no license version. Direct listing and API requests returned 403. | Confirm the exact terms or exclude the whole asset set and its history. |
| Medium | The historical engineering audit included a personal home path, and the upgrade guide included a concrete-looking USB device path. PR bodies/reviews and commit metadata retain personal or local-environment identifiers. | Current docs use portable paths. Retained Git objects and GitHub records still require a disposition. |
| Medium | Firmware evidence uploads included directory-wide globs, generated headers and raw trace data under the repository's 90-day retention. P8 evidence also includes synthetic compiled timeline blobs. | Narrowed baseline uploads to known reports and logs, set 30-day artifact retention and skipped uploads on ordinary fork PRs under the checked-in workflow. Existing artifacts retain their current visibility and retention. |
| Medium | GitHub allowed mutable action refs even though all active branch tips were SHA-pinned. Historical workflows used `@v4` tags. | Enabled and verified repository-wide full-SHA enforcement. Reruns of historical workflows with tag-pinned actions may fail under this policy. |

## Secret and privacy review

Gitleaks 8.30.1 scanned the 507-file baseline snapshot, Git patches across all
reachable refs, and downloaded Actions evidence with 100% redaction and bounded
archive/decode recursion. A separate content scan walked 1,030 unique reachable
Git blobs, the archived and nested payloads, commit messages, the downloaded
material and available PR/issue metadata. It looked for private-key markers and
key files, literal credential assignments, MAC addresses, device identifiers,
serial paths and personal metadata. The tools did not test credentials against
live services. The reviewed scope produced no confirmed live credentials or
hardware identifiers.

| Location | Result |
| --- | --- |
| `scripts/tests/v4/vectors.json:20` at `2484859b1180` | The one key-shaped test-vector hit is generated by `make_vectors.py` for deterministic handshake authentication. |
| `docs/validation/wireless-v4-core.json:501,510` and `docs/validation/wireless-s31-preflight.json:1595,1604` | Four Gitleaks hits in commits `2484859b1180` and `8e6c0301b1bd` are source SHA-256 hashes. The tree and history scans each produced these five findings. |
| `scripts/tests/uart-fixture/ProtogenUartKey.h`, UART CI build logic and host fixtures | The only tracked UART key header is the regression fixture. Matrix keys are generated in temporary directories. The 25 targeted credential-assignment matches were paths to temporary UART key directories. The byte-array hits were display/font and crypto data. No production key file was found. |
| `archive/PROT_OS7_original.zip` and model/illustration metadata | The original sketch contains an upstream contact address. Archived dependencies include attribution contacts. The model PDF contains authorship/application metadata, and the comparison PNG contains two artwork hashes. Contact and metadata values are omitted here. |
| `docs/INDEPENDENT_CODE_AUDIT.md` and `docs/UPGRADE.md` | Historical blob `070cd2a8db00` contains a workstation path. Eight historical versions of the upgrade guide include a USB port example with an identifier-like suffix. Remove these from any clean public import. |
| Git commit identities and GitHub PR discussions | Git history contains author and committer emails outside the GitHub noreply domain. PR descriptions, comments and submitted reviews contain local paths. Values are omitted here. The audit covered 10 PR bodies, one issue-thread comment and six submitted reviews. |
| GitHub Actions artifacts and logs | All 124 available artifact ZIPs and 70 available log-attempt ZIPs for 69 runs were downloaded and scanned. All 6,193 Gitleaks hits in artifacts were 64-hex source hashes under source-path keys. There were no Gitleaks hits in downloaded log ZIPs. Raw text reports and traces can still reproduce original artwork or other non-secret internal data. |
| Releases, deployment list and caches | No releases, tags, listed deployments, Actions caches or active self-hosted runners were found. There were no configured repository Actions secrets, variables or environments when checked. |

No credential revocation was indicated by the inspected findings. If a real
credential is confirmed later, rotate or revoke it with the issuer first.
Re-pair wireless devices or replace affected UART material and rebuild each
provisioned peer if applicable. Then identify affected refs, PRs, logs,
artifacts and caches, and request explicit authorization for any history rewrite
or deletion. [GitHub sensitive-data cleanup](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)

The inspection cannot establish what exists in unreachable or unadvertised Git
objects, expired records, other clones, off-repository storage, or future runs.
Pattern scans miss unknown formats, encrypted material and credentials embedded
in arbitrary binaries. The two downloaded binary artifacts were named timeline
test data. Firmware images from provisioning, local build output and private
device storage were outside the Git/GitHub scope.

## GitHub protection and CI

| Control | Verified state and next step |
| --- | --- |
| Visibility and main protection | Private, personal account, admin access confirmed. GitHub returned HTTP 403 for protection and rulesets with the message requiring GitHub Pro or a public repository. To configure while private, upgrade, then use **Settings → Rules → Rulesets** to target `main`. Require PRs and the existing fast checks `host-and-c3` and `teensy (all)` from GitHub Actions. Block force pushes and deletion. Confirm owner recovery access and review availability before requiring independent approvals or including administrators in enforcement. |
| Token and private forks | Default token permissions are read-only and Actions may not approve PRs. Private fork workflows are disabled. Their secrets and write-token options are off. GitHub returned 422 for the public fork-approval policy while private. Before admitting public fork work, require approval for all outside collaborators and recheck the policy after the visibility transition. |
| Action policy | Full-length SHA enforcement enabled and verified by API readback. Checkout and artifact actions resolve to verified commits in the official Actions repositories. External reuse of this private repository's Actions workflows is disabled. |
| Dependabot and dependency review | Alerts enabled (HTTP 204) and automated security fixes enabled. The alert list was empty. GitHub dependency review is unavailable without an eligible repository and dependency graph support. At publication, add pinned dependency review where the graph captures dependencies, and separately review `toolchain.lock.json`, PlatformIO/PJRC and downloaded packages. [Dependency review availability](https://docs.github.com/en/code-security/concepts/supply-chain-security/dependency-review) |
| Secret scanning, push protection and reporting | Secret-scanning and private-reporting endpoints returned 404, and code scanning returned 403. There was no verified repository secret-protection entitlement. Obtain a supported plan if enabling scanning while private. In a future eligible public repository, check **Settings → Security → Advanced Security**, enable secret scanning, push protection and private vulnerability reporting, and confirm the **Security** tab exposes a reporting entry point. |
| Runners and retention | Hosted `ubuntu-24.04` only. Repository retention 90 days, with 30 days specified on the revised firmware uploads. No workflow cache, cross-run artifact intake, privileged follow-up workflow, deployment job or `pull_request_target` exists in the inspected active workflows. |

The default workflow runs fast on PRs and pushes to main. Full runs remain on
the weekly schedule, manual dispatch and version tags. The token is
`contents: read`, checkouts disable credential persistence and shell commands
use fixed arguments or quoted environment values. Fork code remains untrusted.
The upload condition is a safeguard in the checked-in workflow. GitHub's fork
approval, read-only permissions and lack of secrets are the trust controls for
PRs that can edit their workflow.

The installer checks the Arduino CLI binary against a locked checksum manifest.
Arduino library/core versions, PlatformIO and ESP-IDF packages are version
pinned, while apt, package indexes and some Python transitive dependencies are
resolved upstream. Existing checks record versions, and the preflights verify
specific source/archive hashes. Full reproducible supply-chain provenance would
require a reviewed per-platform package manifest and verification of the
resolved inputs. Keep dependency installs on disposable runners.

The matrix already passes four compile workers to existing target build
helpers. It builds variants serially within shared PlatformIO/Arduino build
trees, and full Teensy CI separates plain and authenticated groups into jobs.
Preserve that arrangement. GitHub's public standard Ubuntu runner has four CPUs
and 16 GB RAM, with 14 GB local storage. Any increase in parallel build tasks
needs per-job peak RSS, disk and duration measurements first. No new
four-way job fanout was added.
[GitHub-hosted runner specifications](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)

### P8 draft workflow review

The P8 version of `firmware.yml` retains the read-only token, checkout controls
and triggers. The proposed `virtual-soak.yml` is dispatch-only. Typed inputs
flow through environment variables, then the plan parses and bounds the seed
range and duration. At most 50 two-seed shards run, with four jobs in parallel,
bounded wall-clock limits and coverage checking. The executed twin comes from
the build job in the same workflow run. The aggregator reads same-run JSON.
There is no cross-run artifact selector, token input, privileged deployment or
persistent shared runner.

Before merging P8, carry forward the revised firmware upload scope. For the soak
workflow, explicitly bound artifact retention, keep the same-run digest
validation, and set an intentional policy for concurrent dispatches. Preserve
full requested seed coverage and partial evidence on shard failure. Review the
artwork contained in the twin binary and virtual trace data before enabling this
workflow in any public export. No soak was dispatched during this audit.

## Publication choices

| Approach | Assessment |
| --- | --- |
| A. Sanitize the existing repository | Requires authorized removal of the affected material and personal metadata across branches, commits, PR refs, cached views, logs and artifacts. A rewritten branch alone cannot establish that old diffs and objects are inaccessible. GitHub Support generally does not remove non-sensitive data. |
| B. Prepare a separate clean export | Preferred after rights review. Build an allowlisted tree from verified files, exclude both recovery/model archives and their derivatives until cleared, and start new Git history. Do not import private Git refs, PR conversations, Actions runs or artifacts. Review the complete export before creating any public repository. |
| C. Keep the existing repository private | Required now. Preserve recovery and ongoing qualification while the owner obtains permissions or prepares a reviewed clean export. |

A visibility change makes repository code and Actions history/logs public and
enables public forks. Existing branch/tag history, commit identities, PR diffs,
conversations, issues and published release assets should be treated as
repository-readable. Retained Actions artifacts can be downloaded by signed-in
accounts with repository read access. Separate private advisory workflows and
draft release access have their own permissions. Review them through the actual
GitHub access model before any transition.
[GitHub visibility changes](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility)
[Artifact access](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts)

## Verification

- Checksum-verified actionlint 1.7.12 passed on the revised firmware workflow and
  both workflows at the P8 draft SHA. The revised upload patterns retain all
  196 reports and logs from the exact P7 main run while excluding its two
  generated headers. Synthetic key, recovery and executable paths did not match.
- The original archive comparison passed on all four faces, with zero logical and
  physical pixel differences. Both default and recorder-enabled runs passed 57
  frozen frame/SPI checkpoints.
- The pinned local Teensy default build completed with zero warnings.
  Configuration metadata and EEPROM/configuration compatibility checks passed.
  The UART admission scenarios, independent v4 vector check and two installer
  integrity tests passed. The staged tree returned the same five triaged
  Gitleaks matches as the baseline.
- Exact baseline GitHub CI passed on P7 main and the P8 candidate. The audit
  reused that target/host evidence and made no runtime or build-tool changes.
  [P7 baseline](https://github.com/Fur-Byte/Protogen/actions/runs/44169466728)
  [P8 candidate](https://github.com/Fur-Byte/Protogen/actions/runs/44172791595)
- Physical power, RF, wear tests and production S31 qualification were outside
  this audit. See the [firmware readiness evidence](RELEASE_READINESS_2026_10_17.md)
  and [S31/v4 gates](RELEASE_S31_V4_READINESS.md).

## Owner checklist before publication

- [ ] Obtain and record Feronium's applicable permission or replace and remove
      the unlicensed source, art, schematic and affected derived outputs from the
      proposed public tree. Review provenance of the revised animation, audio,
      interaction and preview paths before including them.
- [ ] Confirm the exact Thingiverse asset license and attribution or exclude
      both model ZIPs, extracted geometry, images and instructions from the
      public tree and any history that will accompany it.
- [ ] Approve the owner-controlled file list and scoped license grant. Inventory
      the exact linked dependency sources and notices for each binary intended
      for distribution, including both GPL libraries on the relevant Teensy
      builds and the linked ESP-IDF packages for C3.
- [ ] Decide how to handle retained contact and workstation/device metadata,
      PR discussions and artwork-bearing Actions evidence. Verify the chosen
      publication surface again with redacted scans and review of actual output.
- [ ] Configure branch protection and required checks on an eligible plan.
      Confirm token, fork approval, dependency review, secret scanning, push
      protection and private vulnerability reporting settings on the target.
- [ ] Review the final export and CI eligibility on their own merits and record
      an explicit publication decision before changing any visibility.

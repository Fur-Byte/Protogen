# CI and engineering efficiency

- Prefer focused local tests and compact evidence summaries. Avoid repeated repository scans, API queries, verbose logs, and redundant full-suite executions.
- Feature branches use pull-request CI, including draft PRs. Push CI is for main and release tags.
- Consolidate a stable candidate before pushing. Let one automatic PR run execute.
- Inspect CI once when its result is needed. If pending, report the URL and stop checking. Do not run sleep/poll loops.
- Dispatch one manual full run per stable SHA/configuration after fast CI passes. Reuse completed exact-SHA evidence. Inspect individual logs only to diagnose a failure or discrepancy.
- Concurrency may cancel superseded PR runs, but must preserve intentional manual full runs and unrelated PRs.

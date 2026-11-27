# Claude independent audit: maintainer reconciliation

Date: 2026-11-27. Reviewed baseline: `08a1efa559ae3abb3f77a5343b7c51cfc9cc3a62` (PR #58 merged after three green CI workflows). This record reconciles the user-provided Claude audit summary, not its unavailable full offline report or randomized test artifacts.

## Findings and disposition

| Finding | Independent evidence | Disposition |
| --- | --- | --- |
| Version-only dependency validation | A synthetic arduinoFFT library was accepted before and after a source mutation with its version unchanged. Official archive and installed tree digests verified independently. | [#63](https://github.com/Fur-Byte/Protogen/pull/63): source integrity and release evidence; compiler binaries, ESP-IDF, PlatformIO and transitive Python dependencies remain open. |
| Ambient settle consumes timeline capacity | GCC/ASan/UBSan reproducer using the real Behavior Core: 8 valid overlapping events, 7 started and 1 rejected. | [#59](https://github.com/Fur-Byte/Protogen/pull/59): authored timeline takes precedence over optional settle; integrated regression passes 8/8. |
| Software-reset retry counter | Host policy preserves prior count for exact trusted cause 2. Pinned core exception handler requests SCB_AIRCR reset after recovery interval. Existing physical test reported mixed cause 3, not repeated exact cause 2. | [#62](https://github.com/Fur-Byte/Protogen/pull/62): documented and tested current policy; physical characterization remains open. No policy change or new flash. |
| HTTP request framing and port 80 | Real handler accepted +2 and 0_2 lengths, returned 503 for malformed timeline length, rejected default-port Host without :80. | [#60](https://github.com/Fur-Byte/Protogen/pull/60): strict decimal framing, 400 for malformed input, correct port-80 loopback authority. |
| Brightness approved field | SAVE returns persisted effective base authorization, GET/STATUS runtime authorization. ConfigStore canonicalizes persisted approval. | [#61](https://github.com/Fur-Byte/Protogen/pull/61): document existing contract and regression-test SAVE/GET/STATUS/LOAD without changing wire or brightness policy. |

## Verification and constraints

- #59: `CXX=g++ python3 scripts/check-timelines.py --conformance-only`, 4,000 rounding cases and integrated capacity regression; formatting.
- #60: `python3 scripts/tests/test-helmet-tool.py HandlerChecks`, 10 tests, including malformed framing and loopback authority; formatting.
- #61: `CXX=g++ bash scripts/test-configuration-safety.sh`, simulated ceilings 0/1/15, upgrade/reconnect, bounded JSON, Face Studio. Real hardware ceiling unchanged.
- #62: GCC ASan/UBSan recovery journal host test; prior physical logs reviewed (three watchdog resets, lockout and USB recovery). No new physical qualification.
- #63: dependency mutation tests (2), packaging (38), build tooling (19 with GCC), pinned format check. Official PJRC and Arduino indexes freshly retrieved over HTTPS; matching archives independently parsed to derive installed tree pins. Linux installed core and selected libraries matched. Initial target builds compiled but evidence invalidated by a concurrent test-fixture edit, requiring a clean-source rerun. The rerun passed both target configurations with zero warnings and exact linked asset verification.

The fresh ESP-IDF 6.1 constraints file differed from the November 25 cached copy. Do not pin mutable constraints without a separately verified dependency resolution and reproducible per-platform wheel hashes. The Linux Teensy compiler archive matched the upstream checksum but contains hardlinks requiring a separate installed-tree verification design; do not claim full toolchain content verification.

Claude's reported 200,000 randomized trials were not rerun here. Its report did not cover the ESP32 app, v4 PQ, audio or renderer; no conclusion on those areas follows from its absence of high findings. Full PR CI and assembled hardware qualification are separate gates. No brightness, crypto, electrical or publication gate was relaxed. All new PRs are drafts pending CI and review.

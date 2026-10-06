# Wireless v4 preflight — stopped at the stack resource gate

> **Target plan (2026-10-06):** ESP32-S31 is the intended final wireless MCU
> for helmet and Paw; C3/v3 is retained as legacy development/regression.
> Current production sources remain v3. Public S31 preview builds and a 48 KiB
> ML-KEM-1024 worker have static resource evidence, but the supported-stable SDK
> and its memory-protection implementation block the production migration/v4
> phase. Physical S31 qualification is separately pending. See the
> [S31 preflight](WIRELESS_S31_PREFLIGHT.md) for evidence and remaining work.

Date: 2026-10-06. Baseline repository commit:
`c26464d5529b78d8e03ddafd9830d99ecee7d2d5`.

Follow-up: [dedicated crypto-worker qualification](WIRELESS_PQ_WORKER_QUALIFICATION.md)
now evaluates 24/32 KiB static workers, upstream arena allocation, and wolfCrypt.
The 24 KiB default-stack candidate lacks safe call-chain margin. A 32 KiB worker
is a plausible hardware-test candidate, but MCU heap/high-water/timing/servicing
gates are still open: **ML-KEM-1024 NOT SAFE TO INTEGRATE** at this stage.
The configured main stack is 12,288 bytes; IDF adds 512 bytes to its actual task
creation argument in this configuration. Neither size fixes the original blocker.

**No v4 implementation was installed.** Direct integration of the evaluated
ML-KEM-1024 implementation into the existing main task is unsafe. Both resolved
ESP32-C3 configurations provide a **12,288-byte** main-task stack. An individual
ML-KEM-1024 encryption frame requires **19,056 bytes** with the firmware's actual
`-Og` settings, before accounting for callers, further callees, application
locals, interrupts or a safety margin. Key generation has a **15,536-byte** frame.
The requested resource stop rule was applied before changing the protocol.

This is a compiler-demonstrated blocker for direct integration into the current
task configuration, **not evidence that the ESP32-C3 cannot support ML-KEM-1024
with a different, qualified stack/workspace arrangement**. Increasing a stack or
moving work to an allocator requires a complete memory and scheduling budget;
the measurements here do not approve either solution.

The production firmware, wire version, pairing storage and v3 tests are
unchanged. They do not provide the requested v4 hybrid wireless security claim.
There is no v4 build, classical fallback introduced by this work, ML-KEM-768
substitution, upload, eFuse operation, or hardware modification. The requested
complete-v4 commit and push have not been performed because that implementation
and its validation are incomplete.

## Standards reviewed before the preflight harness

The authoritative sources and downloaded SHA-256 hashes are recorded in
[`mlkem-preflight-pins.json`](../scripts/tests/mlkem-preflight-pins.json).

* [FIPS 203 final](https://csrc.nist.gov/pubs/fips/203/final): final ML-KEM
  interfaces, input checks, randomness, intermediate destruction, parameter sizes,
  implicit rejection, and the differences from pre-standard Kyber.
* [Current FIPS 203 potential corrections](https://csrc.nist.gov/files/pubs/fips/203/final/docs/fips-203-potential-updates.xlsx):
  the retrieved spreadsheet has two entries, clarifying the extra index-zero
  zeta-table entry in Appendix A and correcting the Algorithm 15 line 7 comment
  to reference polynomial `w`. Neither changes the wire format. NIST labels these
  potential corrections rather than new technical requirements.
* [SP 800-227 final, September 2025](https://csrc.nist.gov/pubs/sp/800/227/final):
  KEM key management, validation, key confirmation, ephemeral use, and especially
  sections 4.6.2–4.6.3 on approved combiners and their security limitations.
* [SP 800-56C revision 2](https://csrc.nist.gov/pubs/sp/800/56/c/r2/final):
  the referenced one-step and extraction-then-expansion derivation guidance.

No production hybrid combiner has been implemented or finalized. The next
protocol review must select a precise instantiation of SP 800-227 equation (14)
or another explicitly approved construction, encode its inputs unambiguously,
and bind the complete ordered public exchange and context. For a two-step
construction, extraction must include **both** shared secrets. Existing
HKDF-SHA-256 must remain in the separated key hierarchy. The approved-combiner
discussion does not give every possible instantiation a generic IND-CCA guarantee.
Section 4.6.3 specifically warns about combining only shared secrets and provides
a SHA-3 example binding ciphertexts, encapsulation keys and domain separation,
with a random-oracle assumption. P-256 ECDH itself is not an IND-CCA KEM.
No stronger theorem or certification is claimed here.

## Evaluated implementation and reproducibility

Candidate: [PQ Code Package mlkem-native](https://github.com/pq-code-package/mlkem-native),
release **v2.0.0**, commit
`d1b2fe782888bdb761a50336012923180be7f502`.

The evaluated backend is portable C arithmetic plus the upstream portable C
FIPS 202 implementation. Native assembly backends are disabled. Default upstream
stack allocation and compiler-barrier zeroization are retained. Pairwise
consistency testing is disabled in these preflight configurations; enabling it
adds work and can raise the stack requirement. These are lower-bound feasibility
estimates, not a qualified production cryptographic configuration.

The project is maintained under the PQ Code Package/PQCA umbrella and publishes
formal memory/type-safety work for its C implementation. This assurance is a
reason to evaluate it; it is not proof of correctness of this application's
future integration, MCU timing behavior or physical side-channel resistance.
No ML-KEM internals were written or modified.

Upstream offers `Apache-2.0 OR ISC OR MIT` for the library. The evaluated license
choice is **MIT**, requiring retention of upstream copyright/license notices in
any eventual vendoring. The test-only RNG separately offers MIT among its
license choices. No license conflict was identified for this isolated evaluation;
a complete production dependency/license audit remains pending.

Pinned archive SHA-256:
`7c7a10464ba3c62d5657a70da495539ab7f28e464cff80eb9d8173e2bc91c4d3`.
The pins file also records **41 upstream source/license hashes**, all six vector
file hashes, and the standards snapshot hashes. The preflight downloads and
verifies the archive into ignored `build/pq-preflight/`, then verifies the consumed
source files before compilation. The implementation has **not** been vendored
into production firmware because integration stopped at the resource gate.

Run:

```sh
python scripts/build-esp32.py
python scripts/check-mlkem-preflight.py
```

The second command requires a host C compiler, Python with safe tar extraction,
and the pinned project's installed ESP32 toolchain. `MLKEM_CROSS_CC` can select
the compiler path; `MLKEM_CLANG` can select the analyzer. Downloads use normal
HTTPS/proxy settings, retain TLS verification, and fail on hash mismatches.
All compiled upstream libraries, test executables and public test material remain
inside ignored `build/`. The deterministic upstream RNG is used **only** in host
test executables, never production firmware. The preflight script succeeding
means its measurements/tests ran; the recorded integration gate remains NO-GO.

## Cryptographic validation completed

The selected implementation was compared directly with the final-FIPS-203
[NIST ACVP-Server vectors](https://github.com/usnistgov/ACVP-Server/tree/975de31eb83d87039ec88934fdc47d8c312b892d/gen-val/json-files),
tag **v1.1.0.43**, commit
`975de31eb83d87039ec88934fdc47d8c312b892d`.
Every ML-KEM-1024 case in the pinned sets ran; none was skipped.

| Vector function | Cases | Result |
| --- | ---: | --- |
| KeyGen | 25 | Exact public/private output match |
| Encaps | 50 | Exact ciphertext/shared-secret match |
| Decaps, expanded and seed formats | 30 | Exact shared-secret match |
| Encapsulation-key validation | 20 | Exact expected accept/reject result |
| Decapsulation-key validation | 20 | Exact expected accept/reject result |
| **Total** | **145** | **PASS** |

The full portable C implementation and upstream ACVP/functional adapters were
compiled with **ASan, UBSan, and warnings as errors**. An additional **1,000**
upstream functional iterations passed, covering successful establishment,
unaligned buffers, invalid public/private keys, and modified ciphertexts. Modified
ciphertexts exercise ML-KEM's implicit rejection: the API can return a replacement
secret rather than an externally distinguishable failure. Future protocol
Finished/SAS verification must reject the resulting key disagreement.

Invalid-size fixtures are checked by upstream's ACVP adapter. The primitive's
fixed-buffer API does not accept a runtime length; a production wrapper must
enforce exact lengths before calling it. Full application error-path, allocation
failure, entropy failure, PCT failure, secure-zero and fault-injection testing
remains pending. Host vector passes are not CMVP certification or MCU assurance.

Clang Static Analyzer **19.1.7** ran on the portable C monolithic implementation
with no diagnostics. That is preflight library analysis, not the requested fresh
adversarial review of a completed v4 protocol.

## Resource measurements

Compiler: `riscv32-esp-elf-gcc`, Espressif GCC **15.2.0**, crosstool-NG
`esp-15.2.0_20251204`. Target flags come from the resolved helmet build:
`-march=rv32imc_zicsr_zifencei`, `-mtune=esp-base`, and its picolibc specification.
The preflight preserves the firmware's relevant stack-affecting compiler flags.
No ML-KEM backend intended for RV64 or another MCU was substituted.

| Parameter/optimization | Key-generation inner frame | Encryption inner frame | Decaps wrapper frame | Object text |
| --- | ---: | ---: | ---: | ---: |
| ML-KEM-1024, actual `-Og` | 15,536 B | 19,056 B | 1,792 B | 15,298 B |
| ML-KEM-1024, comparison `-Os` | 15,568 B | 19,104 B | 1,808 B | 12,900 B |
| ML-KEM-768, comparison `-Og` | 10,160 B | 13,168 B | 1,312 B | 14,814 B |
| ML-KEM-768, comparison `-Os` | 10,192 B | 13,200 B | 1,328 B | 12,622 B |

These are GCC `.su` **individual static frames**, not measured whole-stack
high-water values. The encryption routine is called by Encaps and by Decaps's
re-encryption check. Caller/callee frames increase the simultaneous requirement.
Even the single 19,056-byte frame exceeds the configured stack by **6,768 bytes**.
Both helmet and paw require this path. ML-KEM-768 was measured only as requested
comparison evidence and also fails direct integration into the current stack.

The compiled primitive objects have zero `.data`/`.bss` bytes because their
workspace is stack allocated. This does **not** imply zero RAM cost. Caller-owned
ML-KEM-1024 material is 1,568 B public key, 3,168 B private key, 1,568 B ciphertext
and 32 B shared secret; holding those four buffers alone requires **6,336 B**,
in addition to workspace, ECDH, reassembly, transcript and state data. Exact v4
Link/Handshake sizes and lifetimes do not exist yet.

Fresh unchanged v3 helmet/paw builds passed with zero warnings. Their image sizes
were **751,952 B / 678,640 B**, against a 1,536,000 B application partition.
PlatformIO reported **34,916 B / 32,996 B** statically used RAM; those figures
exclude dynamic Wi-Fi/PSA/task allocations and cannot establish runtime headroom.
The helmet's existing linked `wirelessLink` object occupies **2,664 B** on RV32.
Primitive object text is not a linked firmware-size delta. No v4 firmware was
linked, so its flash/static-RAM increase and remaining margin are unknown.

An additional 64-iteration host portable-C `-O2` primitive benchmark measured:

| Operation | Host mean | Host maximum |
| --- | ---: | ---: |
| KeyGen | 0.06923 ms | 0.21577 ms |
| Encaps | 0.06410 ms | 0.12178 ms |
| Decaps | 0.07624 ms | 0.12896 ms |

Total host process CPU time was **13.521 ms**. These measurements include Python
FFI overhead, use internal deterministic APIs with random test inputs, disable
PCT, and contain no radio, P-256 or protocol processing. They are **not** ESP32
timings or total reconnect CPU time. Host RSS is not used as MCU RAM evidence.

Actual MCU heap peaks, complete stack high-water, KEM times, total reconnect CPU
time, watchdog/service-loop responsiveness, radio airtime, reconnect latency and
local-safe behavior during crypto work are **still required**. There is no v4
fragment layout or frame count to measure, no emulator measurement, and no
completed MCU go/no-go qualification.

## Next step and outstanding implementation

The recommended next step is to retain **ML-KEM-1024** and qualify a narrowly
scoped memory/scheduling solution before restarting protocol integration:

1. Measure the current firmware's worst-case free heap, largest free block and
   task stacks with Wi-Fi, sensors, UART authentication and local controls active.
2. Evaluate a separately bounded crypto-task stack/workspace, or upstream-supported
   fixed allocation. A 32 KiB stack is only a planning candidate, not an approved
   safe value. Include complete call-chain peaks and PCT, interrupts, concurrency,
   allocation failures and required safety margins. Do not rewrite KEM internals.
   The upstream custom allocator is marked experimental and requires its own
   review and fault testing; changing to unchecked general heap allocation is
   not a solution.
3. Measure actual MCU operation times and ensure crypto work cannot starve the
   helmet service loop or watchdog. Then rerun the resource gate. If the target
   still lacks a safe margin, choose a better-resourced target rather than silently
   reducing the production parameter set.

Only after that qualification can the requested pairing/reconnect combiner,
canonical transcript, bounded authenticated fragmentation, fresh ephemeral
lifetimes, Finished/Commit gates, storage provenance/migration, telemetry, DoS
budgets and all six required production documents be completed. First-pairing
commit/reveal requires particular review: adding a contribution chosen after
nonce reveal must not let an attacker adaptively search the SAS. KEM objects
must be frozen into the exchange before the final random reveals/SAS decision.

There is no v4 migration behavior yet: existing records remain v3 records and are
not relabeled as hybrid pairs. The future implementation must explicitly require
fresh physical v4 re-pairing and reject v3 radio packets without downgrade.
AES-256-GCM, HKDF/HMAC-SHA-256, encrypted ESP-NOW unicast, Teensy ownership and
the established v3 safeguards must remain intact during any subsequent work.

The v4 combiner tests, fragmentation fuzzing, million-transition property suite,
updated storage faults, all UART/Teensy build matrices, regression suites,
protocol adversarial review, final dependency/secret/manifest audit and complete
implementation commit/push are **pending**, not passed. Existing v3 test sources
and their scales were not reduced. Mathematical hybrid wireless forward secrecy
would still depend on component/combiner security, CSPRNG, effective secret
destruction and uncompromised endpoints. Firmware secure boot remains dependent
on hardware/ROM-supported classical mechanisms; physical Teensy security remains
unchanged. No new post-quantum claim applies to the current v3 firmware.

## Complete created-file manifest

| File | Purpose |
| --- | --- |
| `docs/WIRELESS_V4_PREFLIGHT.md` | This stop report, standards review, measurements and remaining gates |
| `docs/validation/wireless-v4-preflight.json` | Non-sensitive recorded preflight results |
| `scripts/check-mlkem-preflight.py` | Reproducible isolated vector/sanitizer/analyzer/target-resource preflight |
| `scripts/tests/mlkem-preflight-pins.json` | Exact implementation/vector pins, licenses and source/input hashes |

No production source file was modified. Ignored build outputs and synthetic
cryptographic test material are not part of this manifest.

# Formal verification

The firmware's correctness evidence is layered: native regressions and
sanitizers, bounded implementation checks (CBMC), mathematical models (Lean),
the deterministic twin, and physical qualification. These layers complement
one another. A checked Lean theorem does not prove a C++ implementation, and
a CBMC success proves only the modeled input domain and stated assumptions.
Nothing here changes the F3 physical qualification requirements or qualifies
the experimental wireless protocol.

## Run locally

On x86_64 Linux, with GCC, Python 3, curl, dpkg-deb, tar and zstd:

    bash verification/install.sh
    python3 verification/run.py --tier fast
    python3 verification/run.py --tier extended
    bash verification/test-native.sh

The installer checks pinned release digests and extracts CBMC 6.11.0 and
Lean 4.36.0 under `~/.local/share/protogen-verification`. Set
`PROTOGEN_VERIFY_TOOLS` to relocate them. No Mathlib, target SDK, global
package changes, or firmware runtime dependency is required.

The C harnesses use CBMC's i386/Linux ILP32 model, not an ARM or RISC-V
instruction model. They assert the relevant integer, pointer and size_t
widths. GCC needs 32-bit preprocessing headers. Ubuntu CI installs
gcc-multilib. On Debian 13 without administrative rights, the matching
libc6-dev-i386 package can be downloaded and extracted locally; for example
with the Debian 13 package version 2.41-12+deb13u5:

    cd ~/.local/share/protogen-verification
    apt download libc6-dev-i386=2.41-12+deb13u5
    dpkg-deb -x libc6-dev-i386_*.deb ilp32-headers

Use the version corresponding to your installed libc and remove the package
archive after extraction. The runner detects the extracted headers. Do not
substitute host pointer widths or fake target headers if preprocessing fails.

The Lean project is independent of the firmware:

    cd verification/lean
    ~/.local/share/protogen-verification/lean-4.36.0-linux/bin/lake build

The runner checks every named theorem directly, audits its transitive axioms
against the standard Lean foundations, and rejects proof placeholders.
The proof module uses only the pinned Lean standard library.

## Results and CI

Fast runs cover replay, packet/command fields, CRC bytes, nonce serialization,
COBS short round trips and full-sized cursor steps, integer encoding, timers,
sensor sign extension, display, easing, audio and queue indices, EEPROM
layout, brightness, and Lean. Extended adds three-record replay, larger
COBS end-to-end cases, and arbitrary prior framebuffer contents. The
pull-request workflow is fast; extended is manual. Each case is capped at
180 seconds except the 12-byte COBS round trip (900 seconds, following the
successful longer retry on the original kernel). The runner records source
commit, pre/post source hashes, outcome, property count, loop/input bounds,
time budget, counterexamples and theorem axioms in build/formal/. The CBMC
JSON output is retained in case logs. Source changes during a run invalidate
the summary.

The separate native suite compiles the actual wrappers under AddressSanitizer,
UndefinedBehaviorSanitizer and float-cast-overflow checks. It tests queue
backpressure, all packet lengths, sensor raw axes, audio samples, easing
durations and EEPROM/NVS fault paths. Its evidence is regression evidence,
not symbolic proof; storage mocks do not establish real flash durability.

- VERIFIED: all generated properties and unwinding assertions pass.
- COUNTEREXAMPLE: a concrete modeled property violation.
- INCONCLUSIVE: incomplete unwinding, invalid output, tool error, or other
  failure not establishing a counterexample.
- TIMEOUT: time budget expired; no correctness claim.
- UNSUPPORTED: missing/wrong tool or unsupported analysis.
- NOT_RUN: case excluded by the selected tier.
- MUTATION_DETECTED: isolated deliberate defect triggers a harness assertion.

Only VERIFIED and MUTATION_DETECTED are positive results; NOT_RUN is a scope
marker, not a successful verification. Unexpected outcomes fail CI. The
negative controls mutate temporary copies of actual production kernels,
never the firmware tree. Replay-bit loss, corrupt COBS delimiter, zero pixel mask, role-free nonce,
expired retry, stalled ring counter and overlong action duration must be
detected by harness assertions. Their
counterexamples also establish that the corresponding assertion paths
are reachable. They do not establish coverage of every branch.

## Maintaining the checks

Use real production functions, not copied implementations. CBMC 6.11.0's
C++ front end cannot parse the repository's modern constexpr, aggregate
and range-for constructs. Small pure kernels shared by C and C++ avoid
changing the firmware interfaces or substituting a reimplementation.
Keep their arithmetic, ownership and callers unchanged unless correcting a
demonstrated defect. A proof of a kernel is not a proof of its wrapper,
call-site preconditions, target compiler, interrupts, or peripherals.

For new cases, document the caller preconditions, pointer allocation,
initial state, ABI, input bounds, loop limits, and intended modular
arithmetic. Add the harness to the runner, keep unwinding assertions
enabled, and test negative cases. Explicitly distinguish intentional
wire truncation/unsigned bitmap shifts from errors. The present checks
enable bounds, pointer, signed-overflow, division and undefined-shift
checks; COBS additionally checks unsigned arithmetic overflow.
Uninitialized-state/lifetime and concurrent memory behavior are not
claimed beyond the modeled local objects and explicit cursor invariants.

To reproduce a failure, rerun the same tier and inspect the named case log.
The trace contains the concrete nondeterministic inputs and violated
property. An unwinding violation calls for a justified higher bound or
an invariant proof, not disabling unwinding assertions. Review model
assumptions with the corresponding implementation and its existing tests.

See [coverage.md](coverage.md) for the traceability inventory.

## References

- CBMC releases: https://github.com/diffblue/cbmc/releases/tag/cbmc-6.11.0
- CBMC manual: https://diffblue.github.io/cbmc/man/cbmc.html
- Lean release: https://github.com/leanprover/lean4/releases/tag/v4.36.0
- Lean proof validation: https://lean-lang.org/doc/reference/latest/find/?domain=Verso.Genre.Manual.section&name=validating-proofs

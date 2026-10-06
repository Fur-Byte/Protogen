# Future S31 integration and physical qualification gates

**EXPERIMENTAL / HARDWARE-INDEPENDENT / NOT ACTIVE PRODUCTION FIRMWARE.**
Core completion does not activate S31 or certify wearable production use.

**S31 PRODUCTION INTEGRATION STATUS: BLOCKED ON OFFICIALLY SUPPORTED SDK.**
**PHYSICAL HARDWARE QUALIFICATION STATUS: PENDING.**

## Supported SDK gate

The pinned ESP-IDF v6.1 S31 support is preview. The official FAQ identifies
v6.1.1 onward as supported tags; that release was not published at the recorded
qualification date. More importantly, the pinned S31 region-protection function
has unresolved IDF-15238, and bootloader permissions have temporary IDF-14620
behavior. Configuration flags do not prove these policies are implemented.
Exact source revisions/hashes and evidence are in
[WIRELESS_S31_PREFLIGHT.md](WIRELESS_S31_PREFLIGHT.md).

Do not patch `cpu_region_protect.c`, substitute master, emulate missing PMP/APM
policies, or silently select unpublished releases. Before removing the v4
platform activation guard:

1. Pin an officially supported stable SDK/tag and official compiler by revision
   and source hashes, without a private/unreleased dependency.
2. Review actual region/peripheral protection implementations, bootloader policy,
   storage and debug mechanisms. Demonstrate preservation of existing hardening.
3. Re-run the unchanged v3 S31 software baseline and the isolated v4 validation.
4. Implement and separately review the supported platform adapters and board
   definition, then re-run full target linking and resource analysis.

This gate is a concrete SDK implementation blocker, not the absence of boards.
It does not prevent completion of the hardware-independent core.

## Adapter checklist

`protocol_v4/S31Adapter.hpp` returns only
`NOT_IMPLEMENTED_UNTIL_SUPPORTED_S31_SDK`; requesting activation is a compile
error. Its future adapter must supply:

* **Transport:** exact-length ingress, bounded RX/TX queues, encrypted ESP-NOW
  unicast with session LMK/PMK, successful transmit completions, trusted CCMP
  attestation, temporary MAC/channel changes and flushing of old peer/frame state.
* **Entropy/crypto:** trusted entropy before any generation, reviewed PSA P-256
  handles, GCM/HMAC/HKDF, secure wipe and pinned final ML-KEM behavior. Replace
  host TLS RNG plumbing with a supported embedded adapter; no copied KEM internals.
* **Worker:** one fixed request/result slot, serialized lifecycle, generation
  invalidation, a dedicated statically allocated worker stack, safe retirement
  after cancellation and priority below essential local/radio/UART service.
* **Storage:** recoverable development storage and a separately gated production
  encrypted/hardware-rooted implementation. Missing required production material
  must prevent core startup; no ordinary-NVS fallback in a production build.
* **Board/timers:** monotonic time, module/flash/PSRAM/antenna selection, provisional
  GPIO/UART/USB/LED/strapping/power settings centralized outside protocol code.
* **Provisioning:** independently check Secure Boot, flash encryption, secure
  storage, signed updates, anti-rollback and debug policy before production use.
  Boot authenticity remains dependent on supported hardware classical signatures.

No production task, GPIO map, eFuse writer or insecure security stub is supplied.
Teensy remains the rendering/behavior controller and optional authenticated UART
is preserved. No PQ operation is required on Teensy.

## Static evidence and intended worker configuration

`python3 scripts/check-v4-resources.py` compiles **unlinked** core/native objects
with the existing official S31 GCC 15.2.0 ILP32F response files. This is compiler
analysis, not an activated ESP-IDF firmware target. It makes no SDK change.
The resource report accompanies [WIRELESS_V4_VALIDATION.md](WIRELESS_V4_VALIDATION.md).

A future **49,152 B (48 KiB)** worker is the recommended candidate, not a
physically qualified allocation. The native maximum known path is 26,336 B
including the C dispatch wrapper, plus a 6,144 B platform/spill/interrupt allowance,
leaving **16,672 B**. The future task wrapper, locks and RNG callback must be
included in refreshed whole-call-chain analysis. The allowance is an estimate,
not a measured RTOS overhead. Main/control task stacks are not inflated.

The core+one worker+candidate stack reserve is approximately 86 KiB. Even when
conservatively adding all unlinked code to internal SRAM and an extra 6 KiB
allowance on top of the unchanged v3 linker baseline, the compiler budget has
over 220 KiB of contiguous tail. This is meaningful static headroom, but **not
free heap after Wi-Fi**, nor proof about runtime fragmentation or watchdogs.
There is no ML-KEM-768 substitution.

## Diagnostics prepared now

```sh
bash scripts/test-v4.sh --qualification
python3 scripts/check-v4-resources.py
```

The first command exercises the real host worker and reports non-secret
KeyGen/Encaps/Decaps and one end-to-end reconnect CPU sample, simulator frame/time
counts and object sizes. Its output says HOST ONLY and hardware metrics are null.
The second produces static S31 compiler evidence. Neither command uploads anything.

The `HardwareMetrics` interface defines validity bits and bounded fields for:
worker allocation/minimum unused stack; current/minimum/largest free heap;
KeyGen/Encaps/Decaps and queue timing; handshake time/frame count;
maximum radio/UART service latency; watchdog and reset counters. An unset validity
bit must never be presented as a successful zero reading. Future qualification
firmware should expose read-only commands `PQ QUALIFY`, `PQ METRICS`,
`PQ RESET_METRICS` and `SECURITY STATUS`, using this structure. They are a
documented future adapter contract, **not implemented hardware commands today**.
Do not print keys, shared secrets, full nonces, tags or private workspaces.

## Real-board release checklist

**HARDWARE MEASUREMENT REQUIRED** for each item, on both final modules:

1. Confirm datasheet/module revision, on-chip RAM and linker regions, flash/PSRAM
   properties, antenna, pins/strapping, regulator and debug/USB choices.
2. Record heap before/after Wi-Fi, normal objects and worker creation; minimum
   internal free heap and largest block for each KEM operation under stress.
   Measure fragmentation over repeated reconnect/rekey and power cycles.
3. Enable stack overflow detection/canaries and record worker minimum free stack
   through KeyGen/PCT, Encaps, Decaps, failures and cancellation. Measure important
   system/UART/main task stacks. Require comfortable margin, not the minimum that boots.
4. Measure KEM worst-case timing, queue latency and complete reconnect duration.
   Test repeated authenticated requests and unauthenticated garbage. Check idle
   and task watchdogs, resets, radio callbacks and UART service maxima while KEM
   runs. Never insert unsupported yields inside ML-KEM internals.
5. Verify actual ESP-NOW CCMP, native peer install/remove, temporary MAC changes,
   loss/duplication/reordering, channel recovery, simultaneous reboot and key renewal.
   Old fragments, completions, identities and records must remain invalid.
6. Verify local buttons/rendering, safe fallback and optional UART authentication
   under every wireless failure. The model's independent local-service counter
   is not a physical UART latency measurement.
7. Validate RF and antenna near the wearer, range/interference/jamming recovery,
   current consumption, brownout and thermal behavior under maximum crypto work.
8. Inject real flash power cuts around every pairing/storage operation; verify
   valid v4, explicit v3 repair or safe recovery after reboot, never new authority
   from corrupt data. Host byte-cut tests cannot establish flash atomicity.
9. Rehearse future supported signed update/Secure Boot/flash-encryption/storage
   and debug workflows on expendable test boards with explicit production-key
   custody. This task authorizes none of their irreversible operations.

Do not wear/use future boards as production hardware until these measurements
and the supported SDK/adapter review pass. If margins or service latency fail,
stop platform integration and report the blocker; do not silently reduce the
production KEM parameter set.

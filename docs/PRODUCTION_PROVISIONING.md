# Explicit ESP32-C3 production provisioning

**Irreversible operations are deliberately outside firmware, build and test
scripts.** Do not burn eFuses on the development units used by this repository.
This document is the factory procedure specification; actual locking requires a
reviewed board-specific manufacturing station, secure key custody and recovery
qualification. No connected hardware was provisioned here.

## Recoverable development

Default `CONFIG_PROTOGEN_PRODUCTION=n`, `CONFIG_SECURE_BOOT=n`,
`CONFIG_SECURE_FLASH_ENC_ENABLED=n`, `CONFIG_NVS_ENCRYPTION=n`. Pair secrets are
in ordinary NVS and a physically obtained flash dump can recover them. Do not
use development units to promise physical key protection. Prototype upload and
reflash remain possible. There is no automatic security-key or eFuse generation
in the application. Do not copy production sdkconfigs into development builds.

The development build wrapper refuses irreversible production settings and records SDK settings and hashes. Production artifacts must be built through the separately reviewed signing/provisioning workflow, not the development wrapper. After changing sdkconfig
defaults, archive the generated `build/sdkconfig.{helmet,paw}` and regenerate;
PlatformIO otherwise retains existing selections.

## Production settings to enable manually

Use the pinned SDK's `menuconfig` and its ESP32-C3 Secure Boot v2, Flash Encryption,
HMAC and anti-rollback documentation. Keep the compiler pinning and protocol
options enforced by `scripts/build-esp32.py`.

| Setting | Required selection/purpose |
| --- | --- |
| `CONFIG_PROTOGEN_PRODUCTION` | `y`: compile-time and runtime guard |
| `CONFIG_SECURE_BOOT` / `CONFIG_SECURE_BOOT_V2_ENABLED` | `y`: verified signed boot chain |
| `CONFIG_SECURE_SIGNED_APPS` | signed application firmware; use the C3-supported signing scheme |
| `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` | signed build output using factory signing workflow; private keys outside repository/build logs |
| `CONFIG_SECURE_FLASH_ENC_ENABLED` | `y` |
| `CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE` | `y`; qualify loss of download/debug access first |
| `CONFIG_NVS_ENCRYPTION` | `y`: XTS-encrypted secret record |
| `CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC` | preferred C3 hardware-backed derivation |
| `CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID` | factory-assigned key-block ID, already programmed `HMAC_UP`; never reuse signing/flash-encryption blocks |
| `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` | `y`; define update/version policy before provisioning |
| `CONFIG_BOOTLOADER_APP_SECURE_VERSION` | reviewed monotonically increasing security version within device eFuse budget |
| `CONFIG_LOG_DEFAULT_LEVEL_WARN` | no secret/nonce/identifier logging; qualify boot logs as well |

Protocol requirements already enabled by development defaults:
`CONFIG_MBEDTLS_GCM_C=y`,
`CONFIG_MBEDTLS_ECDH_C=y`, `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y`,
`CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n`; helmet UART console versus paw USB Serial/JTAG
selections are enforced separately. No manual protocol option is needed for the
stock targets. HKDF/HMAC/SHA-256 are enabled by the SDK PSA configuration and checked at compilation; ESP-IDF 6.1 has no `CONFIG_MBEDTLS_HKDF_C` option. Legacy DTLS options stay enabled for compatibility with the old
build/test tooling, though DTLS is no longer linked into either target.

If the production build omits encrypted NVS/Secure Boot v2/flash encryption/
anti-rollback/release flash mode, compilation fails. Runtime storage also checks
that flash encryption and Secure Boot are actually active. The application uses
`nvs_flash_read_security_cfg_v2` then `nvs_flash_secure_init`; it **never** calls
security-key generation. ESP-IDF's ordinary `nvs_flash_init()` would generate
missing HMAC keys and may burn an eFuse; this application deliberately avoids
that path. Missing key material disables wireless safely. The registered IDF
security provider's initialization itself only registers the selected scheme.

## Manufacturing sequence and release blockers

1. Qualify the board revision, antenna, mandatory buffered pairing LED, power
   isolation and strap levels. Test the matrix in WIRELESS_VALIDATION.md.
2. Define the signed update/recovery process. The current single-factory-app
   partition and local development flashing workflow **are not a production OTA
   rollout/rollback implementation**. Before lockdown, supply and test a suitable
   factory/OTA partition layout and authorized signed update process. Flash the
   layout before any one-way debug/download restrictions. Do not silently reuse
   this prototype partition table for field updates.
3. Assign non-overlapping hardware key blocks. Generate unique NVS HMAC and flash
   encryption keys in the trusted factory environment; keep signing private keys
   in controlled signing infrastructure. Never share pairing secrets across pairs.
4. Use the SDK's reviewed `espefuse.py`/`espsecure.py` manufacturing workflow to
   program key purposes, write/read protection, Secure Boot trust digests, flash
   encryption and debug/download restrictions. Require operator/device identity
   checks and an explicit confirmation for **each** irreversible step. This is
   intentionally not an executable copy/paste burn recipe: allocation, ordering
   and restrictions depend on actual eFuse state and board revision.
5. Install the signed encrypted release and verify boot and encrypted NVS using
   non-secret diagnostics. Physically pair both devices wirelessly only after
   trust and storage are ready. Erase any development flash contents before
   putting production secrets on a unit. Reject incomplete provisioning.
6. Test permitted update, rejected unsigned image, rejected downgraded security
   version, power loss during storage/update and authorized recovery **before**
   permanently disabling the remaining recovery interfaces. Preserve a viable
   local helmet-control path even if wireless storage cannot initialize.

Physical erasure overwrites the active pairing record, invalidating the retired
peer. NVS is log structured; old development secrets may remain in flash pages
until erase/garbage collection. It is not forensic secure erasure. For disposal,
use the reviewed flash sanitization/key-destruction process appropriate to locked
hardware. Do not expose keys or full identifiers in reports or factory logs.

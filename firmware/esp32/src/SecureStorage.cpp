#include "SecureStorage.h"
#include "WirelessCrypto.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "Efficiency.h"
#if defined(CONFIG_PROTOGEN_PRODUCTION)
#include "esp_flash_encrypt.h"
#include "esp_secure_boot.h"
#if !defined(CONFIG_NVS_ENCRYPTION) ||                                         \
    !defined(CONFIG_SECURE_BOOT_V2_ENABLED) ||                                 \
    !defined(CONFIG_SECURE_FLASH_ENC_ENABLED) ||                               \
    !defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK) ||                           \
    !defined(CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE)
#error                                                                         \
    "Production requires encrypted NVS, Secure Boot v2, flash encryption and anti-rollback"
#endif
#endif
namespace SecureStorage {
namespace { Metrics counters; }
Metrics metrics(){return counters;}
bool begin(nvs_handle_t &handle) {
#if defined(CONFIG_PROTOGEN_PRODUCTION)
  if (!esp_flash_encryption_enabled() || !esp_secure_boot_enabled())
    return false;
#endif
#ifdef CONFIG_NVS_ENCRYPTION
  // IDF's nvs_flash_init() generates keys when none exist. With the HMAC
  // scheme that can BURN an eFuse. Read-only security configuration here.
  nvs_sec_cfg_t security{};
  auto *scheme = nvs_flash_get_default_security_scheme();
  if (!scheme || nvs_flash_read_security_cfg_v2(scheme, &security) != ESP_OK) {
    Wireless::Crypto::wipe(&security, sizeof(security));
    return false;
  }
  const bool initialized = nvs_flash_secure_init(&security) == ESP_OK;
  Wireless::Crypto::wipe(&security, sizeof(security));
#else
  const bool initialized = nvs_flash_init_partition("nvs") == ESP_OK;
#endif
  return initialized && nvs_open("protogen", NVS_READWRITE, &handle) == ESP_OK;
}
bool load(nvs_handle_t handle, Device::Config &config) {
  config = Device::Config{};
  uint8_t blob[Device::ConfigBytes]{};
  size_t n = sizeof(blob);
  const bool okay = nvs_get_blob(handle, "config", blob, &n) == ESP_OK &&
                    Device::decode(blob, n, config);
  Wireless::Crypto::wipe(blob, sizeof(blob));
  return okay;
}
bool save(nvs_handle_t handle, const Device::Config &config) {
  uint8_t blob[Device::ConfigBytes];
  Device::encode(config, blob);
  Device::Config checked;
  const bool valid = Device::decode(blob, sizeof(blob), checked);
  Efficiency::increment(counters.attempts);
  const bool okay =
      valid && nvs_set_blob(handle, "config", blob, sizeof(blob)) == ESP_OK &&
      nvs_commit(handle) == ESP_OK;
  bool verified=false;
  if(okay) {
    Efficiency::increment(counters.commits);
    uint8_t readback[Device::ConfigBytes]{};size_t size=sizeof(readback);
    verified=nvs_get_blob(handle,"config",readback,&size)==ESP_OK && size==sizeof(blob) &&
        memcmp(readback,blob,sizeof(blob))==0;
    Wireless::Crypto::wipe(readback,sizeof(readback));
  }
  Efficiency::increment(verified?counters.verified:counters.failures);
  Wireless::Crypto::wipe(blob, sizeof(blob));
  Wireless::Crypto::wipe(checked.psk,sizeof(checked.psk));
  return verified;
}
} // namespace SecureStorage

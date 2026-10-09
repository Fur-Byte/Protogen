#pragma once
#include "DeviceConfig.h"
#include "nvs.h"
namespace SecureStorage {
struct Metrics { uint32_t attempts=0, commits=0, verified=0, failures=0; };
Metrics metrics();
bool begin(nvs_handle_t &handle);
bool load(nvs_handle_t handle, Device::Config &config);
bool save(nvs_handle_t handle, const Device::Config &config);
} // namespace SecureStorage

#pragma once
#include "DeviceConfig.h"
#include "nvs.h"
namespace SecureStorage {
bool begin(nvs_handle_t &handle);
bool load(nvs_handle_t handle, Device::Config &config);
bool save(nvs_handle_t handle, const Device::Config &config);
} // namespace SecureStorage

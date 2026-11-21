#pragma once
#include <stddef.h>
#include <stdint.h>
using nvs_handle_t = unsigned;
constexpr int ESP_OK=0, NVS_READWRITE=1;
constexpr int ESP_ERR_NVS_NOT_ENOUGH_SPACE=0x1105, ESP_ERR_NVS_REMOVE_FAILED=0x1108;
constexpr int ESP_ERR_NVS_INVALID_HANDLE=0x1107, ESP_ERR_NVS_READ_ONLY=0x1104;
constexpr int ESP_ERR_NVS_INVALID_NAME=0x1106, ESP_ERR_NVS_VALUE_TOO_LONG=0x110e;
int nvs_open(const char *, int, nvs_handle_t *);
int nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
int nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
int nvs_commit(nvs_handle_t);

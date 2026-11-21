#pragma once
#include <cstddef>
#include <cstdint>

inline unsigned failStep, step;
inline bool queueLive, eventLoopLive, wifiLive, radioLive;
inline bool entropyEnabled;
inline constexpr int ESP_OK = 0, WIFI_STORAGE_RAM = 0, WIFI_MODE_STA = 0;
inline constexpr int WIFI_IF_STA = 0, WIFI_PS_NONE = 0, WIFI_SECOND_CHAN_NONE = 0;
inline constexpr int pdTRUE = 1;

inline int startupStep() { return ++step == failStep ? -1 : ESP_OK; }

using QueueHandle_t = void *;
using TaskHandle_t = void *;
using portMUX_TYPE = int;
inline constexpr int portMUX_INITIALIZER_UNLOCKED = 0;
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline QueueHandle_t xQueueCreate(size_t, size_t) {
  if (startupStep() != ESP_OK) return nullptr;
  queueLive = true;
  return &queueLive;
}
inline void vQueueDelete(QueueHandle_t) { queueLive = false; }
inline unsigned uxQueueMessagesWaiting(QueueHandle_t) { return 0; }
inline void xQueueReset(QueueHandle_t) {}
inline int xQueueReceive(QueueHandle_t, void *, int) { return 0; }
inline int xQueueSend(QueueHandle_t, const void *, int) { return pdTRUE; }

inline int esp_event_loop_create_default() {
  const int result = startupStep();
  if (result == ESP_OK) eventLoopLive = true;
  return result;
}
inline int esp_event_loop_delete_default() { eventLoopLive = false; return ESP_OK; }
inline void bootloader_random_enable() { entropyEnabled = true; }
inline void bootloader_random_disable() { entropyEnabled = false; }
inline void esp_fill_random(void *data, size_t size) {
  auto *bytes = static_cast<uint8_t *>(data);
  for (size_t i = 0; i < size; ++i) bytes[i] = 1;
}
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
inline int esp_wifi_init(const wifi_init_config_t *) {
  const int result = startupStep();
  if (result == ESP_OK) wifiLive = true;
  return result;
}
inline int esp_wifi_deinit() { wifiLive = false; return ESP_OK; }
inline int esp_wifi_set_storage(int) { return startupStep(); }
inline int esp_wifi_set_mode(int) { return startupStep(); }
inline int esp_wifi_set_mac(int, const uint8_t *) { return startupStep(); }
inline int esp_wifi_start() {
  radioLive = true;
  return startupStep();
}
inline int esp_wifi_stop() { radioLive = false; return ESP_OK; }
inline int esp_wifi_set_ps(int) { return ESP_OK; }
inline int esp_wifi_set_channel(uint8_t, int) { return ESP_OK; }

using psa_key_id_t = unsigned;
struct esp_now_recv_info_t { const uint8_t *src_addr, *des_addr; };
struct esp_now_send_info_t {};
using esp_now_send_status_t = int;
inline constexpr int ESP_NOW_SEND_SUCCESS = 0;
struct esp_now_peer_info_t { uint8_t peer_addr[6], lmk[16]; int ifidx, channel; bool encrypt; };
inline int esp_now_unregister_recv_cb() { return ESP_OK; }
inline int esp_now_unregister_send_cb() { return ESP_OK; }
inline int esp_now_del_peer(const uint8_t *) { return ESP_OK; }
inline int esp_now_set_pmk(const uint8_t *) { return ESP_OK; }
inline int esp_now_deinit() { return ESP_OK; }
inline int esp_now_init() { return ESP_OK; }
inline int esp_now_add_peer(const esp_now_peer_info_t *) { return ESP_OK; }
inline int esp_now_register_recv_cb(void (*)(const esp_now_recv_info_t *, const uint8_t *, int)) { return ESP_OK; }
inline int esp_now_register_send_cb(void (*)(const esp_now_send_info_t *, esp_now_send_status_t)) { return ESP_OK; }
inline bool esp_now_is_peer_exist(const uint8_t *) { return true; }
inline int esp_now_send(const uint8_t *, const uint8_t *, size_t) { return ESP_OK; }

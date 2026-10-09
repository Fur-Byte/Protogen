#include "EspNowTransport.h"
#include "WirelessCrypto.h"
#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include <string.h>
namespace Wireless {
namespace {
EspNowTransport *instance = nullptr;
const Mac Broadcast{{255, 255, 255, 255, 255, 255}};
void rx(const esp_now_recv_info_t *i, const uint8_t *b, int n) {
  if (instance && i)
    instance->received(i->src_addr, i->des_addr, b, n);
}
void tx(const esp_now_send_info_t *, esp_now_send_status_t s) {
  if (instance)
    instance->sent(s == ESP_NOW_SEND_SUCCESS);
}
} // namespace
bool EspNowTransport::begin() {
  queue = xQueueCreate(RxSlots, sizeof(Received));
  if (!queue)
    return false;
  if (esp_event_loop_create_default() != ESP_OK)
    return false;
  // First pseudonym needs real entropy before Wi-Fi is running. This routine
  // is called before WearerIO starts any ADC and before initializing Wi-Fi.
  Mac initial;
  bootloader_random_enable();
  esp_fill_random(initial.bytes, 6);
  bootloader_random_disable();
  initial.bytes[0] = (initial.bytes[0] & 0xfc) | 2;
  wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&wifi) != ESP_OK ||
      esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    return false;
  // No default STA/AP netif, IP/DHCP services, credentials, Wi-Fi connect,
  // scans or SSID. SoftAP support is disabled at compile time.
  if (esp_wifi_set_mac(WIFI_IF_STA, initial.bytes) != ESP_OK ||
      esp_wifi_start() != ESP_OK)
    return false;
  // Start with a pseudonym, so PSA/session randomness subsequently has the
  // continuously active Wi-Fi hardware entropy source. No network connect.
  started = true;
  initialized = true;
  instance = this;
  return true;
}
bool EspNowTransport::configure(const Mac &own, const Mac *peer,
                                const uint8_t *lmk, const uint8_t *pmk,
                                uint8_t c) {
  if (!initialized || !unicast(own) || !channelAllowed(c))
    return false;
  if (started) {
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    if (native && unicast(destination))
      esp_now_del_peer(destination.bytes);
    const uint8_t clearedPmk[16]{};
    esp_now_set_pmk(clearedPmk);
    esp_now_deinit();
    esp_wifi_stop();
    started = false;
  }
  transmitting.store(false);
  xQueueReset(queue);
  native = false;
  if (esp_wifi_set_mac(WIFI_IF_STA, own.bytes) != ESP_OK ||
      esp_wifi_start() != ESP_OK)
    return false;
  started = true;
  if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK ||
      esp_wifi_set_channel(c, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
      esp_now_init() != ESP_OK)
    return false;
  currentChannel = c;
  if (pmk && esp_now_set_pmk(pmk) != ESP_OK)
    return false;
  esp_now_peer_info_t p{};
  p.ifidx = WIFI_IF_STA;
  p.channel = 0;
  if (peer) {
    if (!lmk || !pmk || !unicast(*peer) || same(*peer, own))
      return false;
    memcpy(p.peer_addr, peer->bytes, 6);
    memcpy(p.lmk, lmk, 16);
    p.encrypt = true;
    destination = *peer;
    native = true;
  } else
    memcpy(p.peer_addr, Broadcast.bytes, 6);
  const bool okay = esp_now_add_peer(&p) == ESP_OK &&
                    esp_now_register_recv_cb(rx) == ESP_OK &&
                    esp_now_register_send_cb(tx) == ESP_OK;
  Crypto::wipe(&p, sizeof(p));
  return okay;
}
bool EspNowTransport::channel(uint8_t c) {
  if (!started || !channelAllowed(c) || busy())
    return false;
  if (c == currentChannel)
    return true;
  if (esp_wifi_set_channel(c, WIFI_SECOND_CHAN_NONE) != ESP_OK)
    return false;
  currentChannel = c;
  return true;
}
bool EspNowTransport::send(const Frame &f, const Mac *peer) {
  if (!started || busy() ||
      (native && (!peer || !same(*peer, destination) || f.bytes[2] != Data)) ||
      (!native && f.bytes[2] == Data))
    return false;
  const Mac target = peer ? *peer : Broadcast;
  if (!native && peer) {
    // Bound transient control peer count to one. It never carries commands.
    if (!same(destination, target) && unicast(destination))
      esp_now_del_peer(destination.bytes);
    if (!esp_now_is_peer_exist(target.bytes)) {
      esp_now_peer_info_t p{};
      memcpy(p.peer_addr, target.bytes, 6);
      p.ifidx = WIFI_IF_STA;
      p.channel = 0;
      if (esp_now_add_peer(&p) != ESP_OK)
        return false;
    }
    destination = target;
  }
  transmitting.store(true);
  ++txAttempts;
  if (esp_now_send(target.bytes, f.bytes, PacketBytes) != ESP_OK) {
    transmitting.store(false);
    ++drops;
    return false;
  }
  return true;
}
bool EspNowTransport::receive(Received &out) {
  return xQueueReceive(queue, &out, 0) == pdTRUE;
}
void EspNowTransport::received(const uint8_t *source, const uint8_t *dest,
                               const uint8_t *b, int n) {
  if (!source || !dest || !b || n != int(PacketBytes)) {
    ++drops;
    return;
  }
  Received item{};
  memcpy(item.source.bytes, source, 6);
  item.broadcast = memcmp(dest, Broadcast.bytes, 6) == 0;
  memcpy(item.frame.bytes, b, PacketBytes);
  ++rxPackets;
  if (xQueueSend(queue, &item, 0) != pdTRUE)
    ++drops;
  notify();
}
} // namespace Wireless

#pragma once
#include "WirelessTransport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>
namespace Wireless {
class EspNowTransport final : public Transport {
  QueueHandle_t queue = nullptr;
  bool initialized = false, started = false, native = false;
  uint8_t currentChannel = 0;
  std::atomic<bool> transmitting{false};
  std::atomic<uint32_t> drops{0};
  Mac destination{};

public:
  bool begin();
  bool configure(const Mac &own, const Mac *peer, const uint8_t *lmk,
                 const uint8_t *pmk, uint8_t channel) override;
  bool channel(uint8_t c) override;
  bool send(const Frame &f, const Mac *peer) override;
  bool receive(Received &out) override;
  bool busy() const override { return transmitting.load(); }
  uint32_t dropped() const { return drops.load(); }
  void received(const uint8_t *source, const uint8_t *destination,
                const uint8_t *bytes, int size);
  void sent(bool okay) {
    transmitting.store(false);
    if (!okay)
      ++drops;
  }
};
} // namespace Wireless

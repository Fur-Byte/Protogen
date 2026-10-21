#pragma once
#include "WirelessTransport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
namespace Wireless {
class EspNowTransport final : public Transport {
  QueueHandle_t queue = nullptr;
  bool initialized = false, started = false;
  std::atomic<bool> native{false};
  uint8_t currentChannel = 0;
  std::atomic<bool> transmitting{false};
  std::atomic<uint32_t> drops{0};
  std::atomic<uint32_t> txAttempts{0}, txCompletions{0}, rxPackets{0};
  std::atomic<TaskHandle_t> waiter{nullptr};
  Mac destination{};
  portMUX_TYPE ingressMux = portMUX_INITIALIZER_UNLOCKED;
  IngressIdentity ingress{};
  void publishIngress(bool enabled, bool operational, const Mac &peer);
  IngressIdentity ingressSnapshot();

public:
  bool begin();
  bool configure(const Mac &own, const Mac *peer, const uint8_t *lmk,
                 const uint8_t *pmk, uint8_t channel) override;
  bool channel(uint8_t c) override;
  bool send(const Frame &f, const Mac *peer) override;
  bool receive(Received &out) override;
  bool busy() const override { return transmitting.load(); }
  uint32_t dropped() const { return drops.load(); }
  uint32_t sends() const { return txAttempts.load(); }
  uint32_t completions() const { return txCompletions.load(); }
  uint32_t receives() const { return rxPackets.load(); }
  bool pending() const { return queue && uxQueueMessagesWaiting(queue)>0; }
  void notifyTask(TaskHandle_t task) { waiter.store(task); }
  void notify() { const auto task = waiter.load(); if (task) xTaskNotifyGive(task); }
  void received(const uint8_t *source, const uint8_t *destination,
                const uint8_t *bytes, int size);
  void sent(bool okay) {
    transmitting.store(false);
    if (!okay)
      ++drops;
    ++txCompletions;
    notify();
  }
};
} // namespace Wireless

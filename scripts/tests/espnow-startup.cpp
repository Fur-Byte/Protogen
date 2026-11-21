#include "EspNowTransport.h"
#include <cassert>
#include <iostream>

namespace Wireless::Crypto {
void wipe(void *data, size_t size) {
  auto *bytes = static_cast<uint8_t *>(data);
  for (size_t i = 0; i < size; ++i) bytes[i] = 0;
}
}

int main() {
  for (failStep = 1; failStep <= 7; ++failStep) {
    Wireless::EspNowTransport transport;
    step = 0;
    assert(!transport.begin());
    assert(!queueLive && !eventLoopLive && !wifiLive && !radioLive && !entropyEnabled);
    assert(!transport.pending());
    assert(!transport.send(Wireless::Frame{}, nullptr));
  }
  Wireless::EspNowTransport transport;
  step = 0;
  assert(transport.begin());
  assert(queueLive && eventLoopLive && wifiLive && radioLive && !entropyEnabled);
  std::cout << "PASS: ESP-NOW startup failures release owned SDK resources and stay offline\n";
}

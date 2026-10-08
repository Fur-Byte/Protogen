#include "sketch.h"

static void run(uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) {
    advanceTime(1000);
    loop();
  }
}

static void detach() {
  pilotInput = HIGH;
  run(2);
  assert(!connectorReady && digitalValues[4] == LOW && digitalValues[6] == LOW);
}

static void faultAndRecover() {
  voltageInput = 500;
  run(10);
  assert(!connectorReady && Dock::stage == 0 && (sensorFaults & 1));
  const uint32_t faultAt = Dock::changedAt;
  voltageInput = 640;
  run(499 - (millis() - faultAt));
  assert(Dock::stage == 0 && digitalValues[6] == LOW);
  run(1);
  assert(Dock::stage == 1 && digitalValues[6] == HIGH && digitalValues[4] == LOW);
  run(50);
  assert(connectorReady && Dock::stage == 2 && !(sensorFaults & 1));
}

int main() {
  setup();
  config.naturalBlinks = 0;
  run(200);
  assert(connectorReady);
  detach();
  fakeUs = uint64_t(0x80000010u) * 1000;
  pilotInput = LOW;
  run(100);
  assert(connectorReady); // No fault has scheduled a retry at this uptime.
  faultAndRecover();
  detach();
  fakeUs += uint64_t(0x80000010u) * 1000;
  pilotInput = LOW;
  run(100);
  assert(connectorReady); // An expired retry never becomes active again.
  detach();
  const uint64_t wrap = ((fakeUs / 1000 >> 32) + 1) << 32;
  fakeUs = (wrap - 100) * 1000;
  pilotInput = LOW;
  run(80);
  assert(connectorReady);
  faultAndRecover(); // The 500 ms fault cooldown crosses millis() wrap.
  delete sensorAdc;
  std::cout << "PASS: actual visor reconnect after half-range uptime, expired fault retry, 500 ms cooldown and millis wrap\n";
}

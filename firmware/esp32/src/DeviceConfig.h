#pragma once
#include "HelmetProtocol.h"
#include <stdint.h>
#include <string.h>
namespace Device {
#ifdef HELMET_RECEIVER
constexpr uint8_t Role = 1;
constexpr int UartRx = 18, UartTx = 19, I2cSda = 0, I2cScl = 1;
constexpr int ScreenSck = 4, ScreenMosi = 6, ScreenCs = 7, ScreenDc = 5,
              ScreenReset = 10;
constexpr int TemperatureAdc = 3, UnlockPin = 9;
#else
constexpr uint8_t Role = 2;
constexpr int NextButton = 4, PreviousButton = 5, BoopButton = 10,
              BatteryAdc = 3;
#endif
// Mandatory external pairing indicator via high-impedance buffer; GPIO2 is a
// boot strap. Do not load it directly with an LED. See HARDWARE_WIRING.md.
constexpr int PairingLed = 2;
enum Feature : uint8_t {
  Environment = 1,
  EnergyMonitor = 2,
  Screen = 4,
  BatteryCaseTemperature = 8
};
struct Config {
  bool paired = false;
  uint8_t channel = 6, features = 0, psk[32]{};
  uint32_t usableEnergyMWh = 0;
};
constexpr size_t ConfigBytes = 46;
inline void encode(const Config &c, uint8_t b[ConfigBytes]) {
  memset(b, 0, ConfigBytes);
  b[0] = 2;
  b[1] = Role;
  b[2] = c.paired;
  b[3] = c.channel;
  b[4] = c.features;
  memcpy(b + 8, c.psk, 32);
  HelmetLink::put32(b + 40, c.usableEnergyMWh);
  HelmetLink::put16(b + 44, HelmetLink::crc16(b, 44));
}
inline bool decode(const uint8_t *b, size_t n, Config &c) {
  if (!b || n != ConfigBytes || b[0] != 2 || b[1] != Role || b[2] > 1 ||
      (b[3] != 1 && b[3] != 6 && b[3] != 11) ||
      (b[4] & ~(Role == 1 ? 15 : 2)) || b[5] || b[6] || b[7] ||
      HelmetLink::u16(b + 44) != HelmetLink::crc16(b, 44))
    return false;
  Config value;
  value.paired = b[2];
  value.channel = b[3];
  value.features = b[4];
  memcpy(value.psk, b + 8, 32);
  value.usableEnergyMWh = HelmetLink::u32(b + 40);
  uint8_t key = 0;
  for (auto x : value.psk)
    key |= x;
  if ((value.paired && !key) || (!value.paired && key) ||
      value.usableEnergyMWh > 1000000)
    return false;
  c = value;
  return true;
}
} // namespace Device

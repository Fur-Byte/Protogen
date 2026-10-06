#include "DeviceConfig.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
using Blob = std::array<uint8_t, Device::ConfigBytes>;
namespace {
Blob encode(const Device::Config &c) {
  Blob b{};
  Device::encode(c, b.data());
  return b;
}
bool same(const Device::Config &a, const Device::Config &b) {
  return encode(a) == encode(b);
}
// Model NVS's one-blob atomic commit contract. Torn pages are tested separately
// as invalid whole records, never modeled as a second metadata/secret commit.
struct Store {
  Blob durable{}, pending{};
  bool present = false, staged = false;
  bool write(const Blob &b, int cut) {
    if (cut == 0)
      return false;
    pending = b;
    staged = true;
    if (cut == 1)
      return false; // loss before publishing the new blob
    durable = pending;
    present = true;
    staged = false;
    return cut != 2; // loss after durable commit but before acknowledgement
  }
  bool reboot(Device::Config &out) {
    staged = false;
    return present && Device::decode(durable.data(), durable.size(), out);
  }
};
} // namespace
int main() {
  Device::Config oldPair, newPair, unpaired, config;
  oldPair.paired = newPair.paired = true;
  for (unsigned i = 0; i < 32; ++i) {
    oldPair.psk[i] = 11 + i;
    newPair.psk[i] = 100 + i;
  }
  config = oldPair;
  config.features = 2;
  config.usableEnergyMWh = 15000;
  const Device::Config *before[] = {&unpaired, &oldPair, &oldPair, &oldPair};
  const Device::Config *after[] = {&newPair, &newPair, &unpaired, &config};
  uint64_t cases = 0;
  for (unsigned op = 0; op < 4; ++op) {
    for (int stage = 0; stage < 4; ++stage) {
      Store store;
      store.durable = encode(*before[op]);
      store.present = true;
      store.write(encode(*after[op]), stage);
      Device::Config boot;
      assert(store.reboot(boot));
      assert(same(boot, *before[op]) || same(boot, *after[op]));
      ++cases;
      // Every byte boundary of a hypothetical torn/corrupt read must be
      // rejected or exactly one complete old/new valid record.
      for (size_t cut = 0; cut <= Device::ConfigBytes; ++cut) {
        Blob mixed = encode(*before[op]), next = encode(*after[op]);
        memcpy(mixed.data(), next.data(), cut);
        bool valid = Device::decode(mixed.data(), mixed.size(), boot);
        assert(!valid || same(boot, *before[op]) || same(boot, *after[op]));
        if (!valid)
          assert(!boot.paired);
        ++cases;
        for (size_t length = 0; length < Device::ConfigBytes; ++length) {
          boot = oldPair;
          assert(!Device::decode(mixed.data(), length, boot));
          assert(!boot.paired);
          ++cases;
        }
      }
    }
  }
  Blob valid = encode(oldPair);
  for (size_t i = 0; i < valid.size(); ++i)
    for (unsigned bit = 0; bit < 8; ++bit) {
      Blob bad = valid;
      bad[i] ^= uint8_t(1 << bit);
      Device::Config boot = oldPair;
      assert(!Device::decode(bad.data(), bad.size(), boot));
      assert(!boot.paired);
      ++cases;
    }
  // Future schema is rejected even with a recomputed CRC. No migration guesses.
  for (unsigned version = 0; version < 256; ++version)
    if (version != 2) {
      Blob bad = valid;
      bad[0] = version;
      HelmetLink::put16(bad.data() + 44, HelmetLink::crc16(bad.data(), 44));
      Device::Config boot;
      assert(!Device::decode(bad.data(), bad.size(), boot));
      ++cases;
    }
  std::mt19937 rng(0x53544f52);
  for (unsigned i = 0; i < 100000; ++i) {
    Blob bad;
    for (auto &b : bad)
      b = rng();
    Device::Config boot = oldPair;
    assert(!Device::decode(bad.data(), bad.size(), boot));
    assert(!boot.paired);
    ++cases;
  }
  std::cout << "PASS: " << cases
            << " storage/schema/power-loss fault cases (atomic NVS model, not "
               "flash hardware)\n";
}

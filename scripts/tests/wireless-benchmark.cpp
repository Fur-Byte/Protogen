#include "SessionHandshake.h"
#include "WirelessLink.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <sys/resource.h>
int main() {
  using namespace Wireless;
  assert(Crypto::init());
  uint8_t secret[32]{};
  assert(Crypto::random(secret, 32));
  Mac paw{{2, 3, 4, 5, 6, 1}}, helmet{{2, 3, 4, 5, 6, 0}};
  constexpr unsigned Sessions = 64;
  double total = 0, maximum = 0;
  const auto cpuStart = std::clock();
  for (unsigned i = 0; i < Sessions; ++i) {
    Handshake p, h;
    const uint32_t now = 100 + i * 1000;
    const auto start = std::chrono::steady_clock::now();
    assert(p.start(secret, Paw, paw, now) &&
           h.start(secret, Helmet, helmet, now));
    Frame f;
    Mac dest;
    bool broadcast = false;
    assert(p.tick(now, f, dest, broadcast) && h.receive(f, paw, now));
    assert(h.tick(now, f, dest, broadcast) && p.receive(f, helmet, now));
    assert(p.tick(now, f, dest, broadcast) && h.receive(f, paw, now));
    assert(h.tick(now, f, dest, broadcast) && p.receive(f, helmet, now));
    assert(p.tick(now, f, dest, broadcast));
    p.sent(f);
    assert(h.receive(f, paw, now));
    assert(p.ready() && h.ready());
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
    total += ms;
    if (ms > maximum)
      maximum = ms;
  }
  const double cpu = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
  struct rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  Crypto::wipe(secret, 32);
  std::cout << "{\"measurement\":\"host, sanitizer-instrumented application; "
               "no RF hardware\",\"sessions\":"
            << Sessions << ",\"mean_ms\":" << total / Sessions
            << ",\"max_ms\":" << maximum << ",\"cpu_ms_total\":" << cpu
            << ",\"protocol_frames\":5,\"control_bytes\":640,\"ecdh_operations_"
               "per_peer\":2,\"handshake_object_bytes\":"
            << sizeof(Handshake) << ",\"link_object_bytes\":" << sizeof(Link)
            << ",\"process_peak_rss_kib\":" << usage.ru_maxrss << "}\n";
}

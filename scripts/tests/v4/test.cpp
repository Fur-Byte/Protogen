#include "Simulator.hpp"
#include <cassert>
#include <cstdio>
void checks();
void vectors();
void qualification();
void uartIntegration();
int main(int argc, char **argv) {
  setbuf(stdout, nullptr);
  if (argc == 2 && std::strcmp(argv[1], "--qualification") == 0) {
    qualification();
    return 0;
  }
  assert(argc == 1);
  vectors();
  V4Test::Simulation sim;
  const bool ok = sim.pair();
  std::printf("pair=%d states=%u,%u frames=%llu transitions=%llu\n", ok,
              unsigned(sim.paw.core.state()), unsigned(sim.helmet.core.state()),
              (unsigned long long)sim.radio.sends,
              (unsigned long long)sim.transitions);
  assert(ok);
  sim.paw.core.requestRekey();
  sim.step();
  sim.step();
  assert(sim.reconnect());
  std::puts("v4 pairing and rekey passed");
  uartIntegration();
  checks();
}

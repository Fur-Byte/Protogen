#include "Core.hpp"
#include "HostCrypto.hpp"
#include "Simulator.hpp"
#include "Worker.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
using namespace ProtogenV4;
void qualification() {
  HostCrypto c;
  assert(c.init());
  NativeKem kem(c);
  SlotWorker w(c, kem);
  KemBuffers inputs;
  Result result;
  uint64_t durations[3]{};
  constexpr unsigned iterations = 32;
  for (unsigned i = 0; i < iterations; ++i)
    for (unsigned op = 0; op < 3; ++op) {
      Job job;
      job.operation = Operation(op);
      job.generation = i + 1;
      if (op == 1) {
        job.input = inputs.pk;
        job.inputSize = 1568;
      }
      if (op == 2) {
        job.input = inputs.sk;
        job.inputSize = 3168;
        job.ciphertext = inputs.ct;
        job.ciphertextSize = 1568;
      }
      Ticket ticket;
      assert(w.submit(job, ticket));
      auto before = std::chrono::steady_clock::now();
      assert(w.advance());
      auto after = std::chrono::steady_clock::now();
      durations[op] += uint64_t(
          std::chrono::duration_cast<std::chrono::nanoseconds>(after - before)
              .count());
      assert(w.poll(result) && same(ticket, result.ticket) &&
             result.status == 0);
      if (op == 0) {
        std::memcpy(inputs.pk, result.buffers.pk, 1568);
        std::memcpy(inputs.sk, result.buffers.sk, 3168);
      }
      if (op == 1) {
        std::memcpy(inputs.ct, result.buffers.ct, 1568);
        std::memcpy(inputs.ss, result.buffers.ss, 32);
      }
      if (op == 2) {
        assert(equal(inputs.ss, result.buffers.ss, 32));
        c.wipe(&inputs, sizeof(inputs));
      }
      c.wipe(&result, sizeof(result));
    }
  c.wipe(&inputs, sizeof(inputs));
  V4Test::Simulation simulation;
  assert(simulation.pair());
  simulation.paw.core.reset();
  simulation.helmet.core.reset();
  simulation.radio.clear();
  const auto framesBefore = simulation.radio.sends,
             clockBefore = simulation.time.ms;
  const auto cpuBefore = std::clock();
  assert(simulation.reconnect());
  const double cpuMs = 1000.0 * (std::clock() - cpuBefore) / CLOCKS_PER_SEC;
  std::printf(
      "{\"scope\":\"HOST ONLY; sanitizer build; not S31 timing or "
      "RAM\",\"iterations\":%u,"
      "\"mean_keygen_us\":%.3f,\"mean_encaps_us\":%.3f,\"mean_decaps_us\":%.3f,"
      "\"sizeof_core_host\":%zu,\"sizeof_worker_host\":%zu,\"sizeof_result_"
      "host\":%zu,"
      "\"sizeof_exchange_host\":%zu,\"sizeof_material_host\":%zu,\"sizeof_"
      "reassembly_host\":%zu,"
      "\"host_one_reconnect_cpu_ms\":%.3f,\"simulated_reconnect_ms\":%llu,"
      "\"simulated_reconnect_frames\":%llu,"
      "\"hardware_valid_mask\":0,\"stack_high_water\":null,\"heap_minimum\":"
      "null,"
      "\"largest_free_block\":null,\"radio_latency\":null,\"uart_latency\":"
      "null,\"watchdog\":null}\n",
      iterations, double(durations[0]) / iterations / 1000,
      double(durations[1]) / iterations / 1000,
      double(durations[2]) / iterations / 1000, sizeof(Core),
      sizeof(SlotWorker), sizeof(Result), sizeof(Exchange), sizeof(Material),
      sizeof(Reassembly), cpuMs,
      (unsigned long long)(simulation.time.ms - clockBefore),
      (unsigned long long)(simulation.radio.sends - framesBefore));
}

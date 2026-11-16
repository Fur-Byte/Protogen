#include "FlightInstrumentation.h"
#include <cassert>
#include <thread>
#include <iostream>

void checkRing() {
  Flight::Record storage[4]{};
  Flight::Ring<4> owner(storage, UINT32_MAX - 1);
  for (unsigned i = 0; i < 6; ++i) owner.append(UINT32_MAX - 2 + i, Flight::Expression, i, 16 + i);
  assert(owner.oldest() == 0 && owner.next() == 4 && owner.size() == 4 && owner.lost() == 2);
  Flight::Record current{};
  assert(!owner.read(UINT32_MAX, current) && !owner.read(4, current));
  for (unsigned i = 0; i < 4; ++i) {
    assert(owner.read(i, current));
    assert(current.sequence == i && current.timeUs == i - 1 && current.detail == i + 2 && current.value == i + 18);
  }
  auto copy = current;
  owner.append(3, Flight::Behavior, 0, 0);
  assert(copy.sequence == 3 && copy.value == 21); // Owner copy remains independent.
  Flight::Ring<4> fresh(storage);
  assert(!fresh.read(0, current) && fresh.size() == 0 && fresh.lost() == 0);
}

void checkFlood(unsigned repetitions) {
  Flight::Flood flood;
  Flight::Record storage[8]{};
  Flight::Ring<8> owner(storage);
  const auto emit = [&owner](uint32_t at, Flight::Event event, uint16_t detail, uint32_t value) {
    owner.append(at, event, detail, value);
  };
  const uint32_t start = UINT32_MAX - 7;
  flood.notify(start, 1, 1);
  flood.flush(start, Flight::SensorFault, emit);
  owner.append(start + 1, Flight::Expression, 4, 5);
  owner.append(start + 2, Flight::Behavior, 3, 0);
  owner.append(start + 3, Flight::RadioLink, 1, 0);
  owner.append(start + 4, Flight::ReplayReject, 0, 1);
  for (unsigned i = 0; i < repetitions; ++i) {
    flood.notify(start + 5, 1, 2);
    flood.flush(start + 5, Flight::SensorFault, emit);
  }
  flood.notify(start + 9, 1, 0);
  flood.flush(start + Flight::ReportIntervalUs, Flight::SensorFault, emit);
  assert(owner.size() == 6 && !owner.lost() && flood.merged == repetitions);
  Flight::Record record{};
  assert(owner.read(0, record) && record.timeUs == start && record.detail == 1);
  assert(owner.read(1, record) && record.event == Flight::Expression);
  assert(owner.read(2, record) && record.event == Flight::Behavior);
  assert(owner.read(3, record) && record.event == Flight::RadioLink);
  assert(owner.read(4, record) && record.event == Flight::ReplayReject);
  assert(owner.read(5, record) && record.timeUs == start + 9 && record.detail == 0 && record.value == repetitions + 1);
  flood.notify(start + 10, UINT32_MAX, 1);
  flood.notify(start + 11, 10, 2);
  assert(flood.pending == UINT32_MAX);
}

void checkIsrCounter(unsigned count) {
  auto &notifications = P3Teensy::audioDrops;
  notifications.store(UINT32_MAX - 5, std::memory_order_relaxed);
  uint32_t previous = notifications.load(std::memory_order_relaxed);
  std::atomic<bool> done{false};
  std::thread producer([&] {
    for (unsigned i = 0; i < count; ++i) P3Teensy::notifyAudioDrop();
    done.store(true, std::memory_order_release);
  });
  uint64_t seen = 0;
  auto drain = [&] {
    const auto current = notifications.load(std::memory_order_relaxed);
    seen += current - previous;
    previous = current;
  };
  while (!done.load(std::memory_order_acquire)) drain();
  producer.join();
  drain();
  assert(seen == count);
}

void checkTiming() {
  Flight::Timing timing;
  for (auto value : {0u, 10u, 50u, 200u, 1000u, 5000u}) timing.observe(value);
  assert(timing.count == 6 && timing.totalUs == 6260 && timing.maxUs == 5000);
  for (auto count : timing.buckets) assert(count == 1);
  timing.count = UINT32_MAX;
  timing.totalUs = UINT64_MAX - 10;
  timing.buckets[5] = UINT32_MAX;
  timing.observe(UINT32_MAX);
  assert(timing.count == UINT32_MAX && timing.totalUs == UINT64_MAX && timing.buckets[5] == UINT32_MAX);
  Flight::Rate rate;
  rate.sample(UINT32_MAX - 100, UINT32_MAX - 7);
  rate.sample(999999, 5);
  assert(rate.peakEvents == 13 && rate.peakWindowUs == 1000100);
  rate.sample(2999999, 7);
  assert(rate.peakEvents == 13);
}

int main(int argc, char **) {
  checkRing();
  checkTiming();
  const unsigned iterations = argc > 1 ? 1000000 : 50000;
  checkFlood(iterations);
  checkIsrCounter(iterations);
  std::cout << "PASS: ring/reset/wrap, saturation, fault-flood isolation, concurrent notification accounting\n";
}

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef PROTOGEN_FLIGHT_RECORDER
#define PROTOGEN_FLIGHT_RECORDER 0
#endif
static_assert(PROTOGEN_FLIGHT_RECORDER == 0 || PROTOGEN_FLIGHT_RECORDER == 1,
              "Flight recorder must be 0 or 1.");

namespace Flight {
constexpr uint16_t Version = 1;
constexpr uint32_t ReportIntervalUs = 1000000;

enum Event : uint16_t {
  Boot = 1, Expression = 2, Superseded = 3, Completed = 4, Delayed = 5,
  Expired = 6, Behavior = 7, SensorFault = 8, MicrophoneFault = 9,
  AudioDrops = 10, UartReject = 11, MainGap = 12, RadioLink = 13,
  RadioDrops = 14, ReplayReject = 15, AeadReject = 16, UartQueueFull = 17,
  HeapMinimum = 18, MainStackMinimum = 19, WearerStackMinimum = 20,
  WearerWorkMaximum = 21, OledPages = 22, SensorReads = 23
};
enum Stage : unsigned {
  LoopWork, LoopInterval, Render, Compose, DisplayWrite, FFT, Sensors,
  UART, USB, Audio, Radio, Append, StageCount
};

struct Record {
  uint32_t timeUs;
  uint32_t sequence;
  uint32_t value;
  uint16_t event;
  uint16_t detail;
};
static_assert(sizeof(Record) == 16 && offsetof(Record, detail) == 14, "Version 1 record layout");

inline void add(uint32_t &value, uint32_t amount = 1) {
  value += amount > UINT32_MAX - value ? UINT32_MAX - value : amount;
}

// All calls, including read, belong to the ring owner. Copies never span
// callbacks, interrupts or task handoffs. RAM holding records needs no startup
// initialization because only committed slots within size() are observable.
template <size_t Capacity> class Ring {
  static_assert(Capacity && Capacity <= UINT16_MAX, "Invalid ring capacity");
  Record (&records)[Capacity];
  uint16_t write = 0, used = 0;
  uint32_t nextSequence, overwrites = 0;

public:
  explicit constexpr Ring(Record (&storage)[Capacity], uint32_t initialSequence = 0)
      : records(storage), nextSequence(initialSequence) {}
  void append(uint32_t at, Event event, uint16_t detail, uint32_t value) {
    records[write] = {at, nextSequence, value, event, detail};
    if (++write == Capacity) write = 0;
    ++nextSequence;
    if (used < Capacity) ++used;
    else add(overwrites);
  }
  uint32_t next() const { return nextSequence; }
  uint32_t oldest() const { return nextSequence - used; }
  uint32_t lost() const { return overwrites; }
  uint16_t size() const { return used; }
  bool read(uint32_t sequence, Record &result) const {
    const uint32_t age = nextSequence - sequence;
    if (!age || age > used) return false;
    result = records[(write + Capacity - age) % Capacity];
    return true;
  }
};

struct Timing {
  uint64_t totalUs = 0;
  uint32_t count = 0, maxUs = 0;
  uint32_t buckets[6] = {};
  void observe(uint32_t elapsed) {
    totalUs += elapsed > UINT64_MAX - totalUs ? UINT64_MAX - totalUs : elapsed;
    add(count);
    if (maxUs < elapsed) maxUs = elapsed;
    const unsigned bucket = elapsed < 10 ? 0 : elapsed < 50 ? 1 : elapsed < 200 ? 2
                            : elapsed < 1000 ? 3 : elapsed < 5000 ? 4 : 5;
    add(buckets[bucket]);
  }
};

// Sampled at owner service boundaries. Keep the duration beside its count
// since modeled idle, target delays and scheduling may stretch the window.
struct Rate {
  uint32_t startedAt = 0, startedSequence = 0;
  uint32_t peakEvents = 0, peakWindowUs = 0;
  bool started = false;
  void sample(uint32_t now, uint32_t next) {
    if (started) {
      const auto elapsed = now - startedAt;
      if (elapsed < ReportIntervalUs) return;
      const auto events = next - startedSequence;
      if (!peakWindowUs || uint64_t(events) * peakWindowUs > uint64_t(peakEvents) * elapsed) {
        peakEvents = events;
        peakWindowUs = elapsed;
      }
    }
    startedAt = now;
    startedSequence = next;
    started = true;
  }
};

class Scope {
  Timing &timing;
  uint32_t (*clock)();
  uint32_t start;
  bool active = true;
public:
  Scope(Timing &timing, uint32_t (*clock)()) : timing(timing), clock(clock), start(clock()) {}
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
  void stop() {
    if (active) { active = false; timing.observe(clock() - start); }
  }
  ~Scope() { stop(); }
};

// Each source gets an immediate first record and at most one admission per
// interval thereafter. A delayed summary carries the most recent timestamp
// and reason in its window; sequence, rather than time, defines ring order.
struct Flood {
  uint32_t previous = 0, pending = 0, merged = 0;
  uint32_t lastAt = 0, lastReport = 0;
  uint16_t lastDetail = 0;
  bool reported = false;

  void notify(uint32_t at, uint32_t count, uint16_t detail) {
    if (!count) return;
    lastAt = at;
    lastDetail = detail;
    add(pending, count);
  }
  void count(uint32_t at, uint32_t current, uint16_t detail = 0) {
    notify(at, current - previous, detail);
    previous = current;
  }
  void state(uint32_t at, uint16_t current) {
    if (previous != current) notify(at, 1, current);
    previous = current;
  }
  template <class Emit> void flush(uint32_t now, Event event, Emit emit) {
    if (!pending || (reported && now - lastReport < ReportIntervalUs)) return;
    emit(lastAt, event, lastDetail, pending);
    add(merged, pending - 1);
    pending = 0;
    lastReport = now;
    reported = true;
  }
};
}

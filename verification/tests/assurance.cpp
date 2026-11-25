#include "ActionQueue.h"
#include "PawCommand.h"
#include "ControlArming.h"
#include "SensorMath.h"
#include "Configuration.h"
#include "src/AudioV2/AudioV2.h"
#include "src/BehaviorCore/AnimationV2.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <deque>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {
using Wireless::Actions;
using Wireless::Intent;
using HelmetLink::Packet;
std::mt19937 rng(0x50524f54);

bool same(const Intent &a, const Intent &b) {
  return a.action == b.action && a.value == b.value &&
         a.duration == b.duration && a.at == b.at;
}

void actions() {
  for (unsigned seed = 0; seed < 32; ++seed) {
    rng.seed(seed);
    Actions queue;
    std::deque<Intent> model;
    uint32_t now = UINT32_MAX - 150;
    for (unsigned i = 0; i < 10000; ++i) {
      now += rng() % 80;
      switch (rng() % 5) {
      case 0: case 1: {
        const Intent item{uint8_t(1 + rng() % 7), uint8_t(rng()),
                          uint16_t(rng()), now};
        const bool expected = model.size() < Wireless::TxSlots;
        assert(queue.push(item.action, item.value, item.duration, now) == expected);
        if (expected) model.push_back(item);
        break;
      }
      case 2: case 3: {
        while (!model.empty() && now - model.front().at >= HelmetLink::FreshMs)
          model.pop_front();
        const bool allow = rng() % 2;
        unsigned calls = 0;
        const bool result = queue.dispatch(now, [&](const Intent &item) {
          ++calls;
          assert(!model.empty() && same(item, model.front()));
          return allow;
        });
        assert(calls == !model.empty());
        assert(result == (allow && !model.empty()));
        if (result) model.pop_front();
        break;
      }
      default:
        queue.clear();
        model.clear();
      }
      assert(queue.pending() == !model.empty());
    }
  }
  Wireless::HelmetStatus status;
  status.available = true;
  status.bridge = 7;
  status.at = UINT32_MAX - 100;
  status.packet.epoch = 3;
  status.packet.ticket = 9;
  status.packet.payload[4] = 5;
  HelmetLink::put32(status.packet.payload, 7);
  uint32_t sequence = 0, age = 0;
  Packet sent;
  Actions q;
  assert(q.push(Actions::Next, 0, 0, status.at));
  assert(q.push(HelmetLink::Boop, 0, 1200, status.at + 1));
  auto reject = [](const Packet &) { return false; };
  using Wireless::ActionSend;
  assert(Wireless::tryQueueAction(q, status, 10, sequence, sent, age, reject) == ActionSend::Blocked);
  assert(sequence == 0 && q.pending() && !sent.sequence);
  auto accept = [](const Packet &) { return true; };
  assert(Wireless::tryQueueAction(q, status, 11, sequence, sent, age, accept) == ActionSend::Sent);
  assert(sequence == 1 && age == 112 && sent.payload[4] == HelmetLink::Expression && sent.payload[5] == 0);
  assert(Wireless::tryQueueAction(q, status, 12, sequence, sent, age, accept) == ActionSend::Sent);
  assert(sequence == 2 && sent.payload[4] == HelmetLink::Boop && HelmetLink::u16(sent.payload+6) == 1200);
  assert(!q.pending());
  assert(q.push(Actions::Next, 0, 0, 100));
  assert(Wireless::tryQueueAction(q, status, 399, sequence, sent, age, reject) == ActionSend::Blocked);
  assert(Wireless::tryQueueAction(q, status, 400, sequence, sent, age, accept) == ActionSend::None);
  assert(sequence == 2 && !q.pending());
  assert(q.push(Actions::Previous, 0, 0, 500));
  sequence = UINT32_MAX;
  assert(Wireless::tryQueueAction(q, status, 500, sequence, sent, age, accept) == ActionSend::Exhausted);
  assert(sequence == UINT32_MAX && q.pending());

  // Only the eligible UART operation can shorten the scheduler wait.
  const uint32_t start = UINT32_MAX - 100;
  for (bool connected : {false, true})
    for (bool available : {false, true})
      for (bool matching : {false, true})
        for (unsigned elapsed : {0u, 99u, 100u, 199u, 200u, 500u}) {
          status.available = available;
          status.at = start;
          HelmetLink::put32(status.packet.payload, matching ? 7 : 8);
          const auto wait = Wireless::uartServiceIn(status, connected, start+elapsed, start, start);
          const unsigned interval = matching ? 500 : 100;
          const unsigned expected = !connected || !available || elapsed >= Wireless::FreshStatusMs
            ? UINT32_MAX : elapsed >= interval ? 0 : interval - elapsed;
          assert(wait == expected);
        }
  std::cout << "PASS: 320000 queue operations against independent deque, backpressure, expiration, sequence exhaustion, UART deadline eligibility\n";
}

void packets() {
  std::array<uint8_t, HelmetLink::MaxRaw> raw{};
  std::array<uint8_t, HelmetLink::MaxEncoded> frame{};
  std::array<uint8_t, HelmetLink::MaxRaw> decoded{};
  uint64_t count = 0;
  for (unsigned length = 0; length <= HelmetLink::MaxPayload; ++length)
    for (unsigned pattern = 0; pattern < 100; ++pattern) {
      Packet p;
      p.kind = uint8_t(1 + pattern % 5);
      p.length = uint8_t(length);
      p.sequence = rng();
      p.epoch = rng();
      p.ticket = rng();
      for (unsigned i = 0; i < length; ++i)
        p.payload[i] = pattern == 0 ? 0 : pattern == 1 ? 255 : pattern == 2 ? uint8_t(i % 2) : uint8_t(rng());
      const size_t n = HelmetLink::pack(p, raw.data(), raw.size());
      assert(n == length + 18);
      for (size_t cap : {size_t(0), n - 1})
        assert(!HelmetLink::pack(p, raw.data(), cap));
      assert(HelmetLink::cobsEncode(raw.data(), n, frame.data(), n + 1) == 0);
      const size_t used = HelmetLink::cobsEncode(raw.data(), n, frame.data(), frame.size());
      assert(used == n + 2 && frame[used - 1] == 0);
      assert(HelmetLink::cobsDecode(frame.data(), used - 1, decoded.data(), decoded.size()) == n);
      assert(std::equal(raw.begin(), raw.begin() + n, decoded.begin()));
      Packet result;
      HelmetLink::Decoder decoder;
      for (size_t i = 0; i < used; ++i)
        assert(decoder.feed(frame[i], UINT32_MAX - 30 + uint32_t(i), result) == (i + 1 == used));
      assert(result.kind == p.kind && result.length == p.length &&
             result.sequence == p.sequence && result.epoch == p.epoch && result.ticket == p.ticket &&
             std::equal(p.payload, p.payload + p.length, result.payload));
      // Corrupt each single bit of a complete frame once for each length.
      if (pattern == 0)
        for (size_t bit = 0; bit < n * 8; ++bit) {
          raw[bit / 8] ^= uint8_t(1u << (bit % 8));
          Packet untouched = p;
          assert(!HelmetLink::unpack(raw.data(), n, untouched));
          assert(untouched.sequence == p.sequence && untouched.epoch == p.epoch);
          raw[bit / 8] ^= uint8_t(1u << (bit % 8));
        }
      ++count;
    }
  std::cout << "PASS: " << count << " full-length packet/COBS/streaming round trips, every single-bit raw corruption at every packet length\n";
}

void sensorsAndAudio() {
  uint8_t sht[6]{0,0,0,0x80,0,0};
  unsigned temperatures = 0;
  for (unsigned value = 0; value <= UINT16_MAX; ++value) {
    sht[0] = uint8_t(value >> 8); sht[1] = uint8_t(value);
    sht[2] = SensorMath::shtCrc(sht, 2);
    sht[5] = SensorMath::shtCrc(sht + 3, 2);
    float t = 0, h = 0;
    const bool okay = SensorMath::sht40(sht, t, h);
    const double reference = -45.0 + 175.0 * value / 65535.0;
    assert(std::fabs(t - reference) < 0.00003);
    assert(okay == (t >= -10 && t <= 70));
    assert(std::isfinite(t) && h >= 0 && h <= 100);
    temperatures += okay;
    sht[2] ^= 1;
    t = 999; h = 999;
    assert(!SensorMath::sht40(sht, t, h) && t == 999 && h == 999);
  }
  sht[0]=0x66; sht[1]=0x66; sht[2]=SensorMath::shtCrc(sht,2);
  for (unsigned value = 0; value <= UINT16_MAX; ++value) {
    sht[3]=uint8_t(value>>8); sht[4]=uint8_t(value);
    sht[5]=SensorMath::shtCrc(sht+3,2);
    float t=0,h=0;
    assert(SensorMath::sht40(sht,t,h));
    const double ref=std::max(0.0,std::min(100.0,-6.0+125.0*value/65535.0));
    assert(std::fabs(h-ref)<0.00003);
  }
  for (int mv = -100; mv <= 3400; ++mv) {
    float t = 0;
    if (SensorMath::ntc(mv, t)) assert(std::isfinite(t) && t >= 0 && t <= 65);
  }
  for (double used : {-1., 0., 1., 999., 1000., 1001., 1e300,
                     std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    int result = SensorMath::percent(used, 1000);
    assert((!std::isfinite(used) || used < 0) ? result == -1 : result >= 0 && result <= 100);
    assert(SensorMath::percent(used,0)==-1);
  }
  for (unsigned request = 0; request <= UINT16_MAX; ++request) {
    RuntimeConfig config;
    config.brightness = uint16_t(request);
    for (unsigned approval : {0u, 1u, 15u, 65535u}) {
      config.brightnessApproval = uint16_t(approval);
      assert(config.effectiveBrightness() <= Settings::MaxBrightness &&
             config.effectiveBrightness() <= request && config.effectiveBrightness() <= approval);
    }
  }
  AudioV2::Detector detector;
  for (unsigned trial = 0; trial < 64; ++trial) {
    AudioV2::Settings settings{uint16_t(rng()%1024),uint16_t(1+rng()%1023),
      uint16_t(rng()%1024),uint16_t(1+rng()%2000),uint16_t(1+rng()%2000),100};
    detector.reset(settings);
    unsigned phase = 0;
    for (unsigned i = 0; i < 16384; ++i) {
      uint16_t sample = uint16_t(rng()%1024);
      if (i % 997 == 0) sample = 1024;
      const bool complete = detector.sample(sample, settings);
      if (sample > 1023) { phase=0; assert(!complete && !detector.snapshot().speech); }
      else { ++phase; assert(complete == (phase == AudioV2::WindowSamples)); if (complete) phase=0; }
      const auto f=detector.snapshot();
      assert(f.envelope <= 1023 && f.noise <= 1023 && f.gate <= 3069 &&
             f.aperture <= 3 && f.energy <= 1024u*1024u);
      assert(f.speech || f.aperture == 0);
    }
  }
  std::cout << "PASS: 131072 sensor raw-axis cases (" << temperatures << " valid temperatures), NTC/energy boundaries, 262144 brightness combinations, 1048576 audio samples\n";
}

void easing() {
  using namespace Behavior::Animation;
  // All admitted durations, all elapsed values, all five curves.
  for (unsigned curve=0; curve<5; ++curve)
    for (uint16_t duration=1; duration<=MaxDurationMs; ++duration) {
      uint16_t previous=0;
      for (uint32_t elapsed=0; elapsed<=duration; ++elapsed) {
        const auto value=ease(static_cast<Easing>(curve),elapsed,duration);
        assert(value>=previous && value<=256);
        previous=value;
      }
      assert(previous==256);
      assert(ease(static_cast<Easing>(curve),UINT32_MAX,duration)==256);
    }
  std::cout << "PASS: all 5 easing curves at every elapsed/duration pair up to 5000 ms\n";
}
}

int main() {
  actions();
  packets();
  sensorsAndAudio();
  easing();
}


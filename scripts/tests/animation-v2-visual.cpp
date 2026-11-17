#include "sketch.h"

#include <array>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>

using Behavior::Animation::Motion;
using Behavior::Animation::Region;
using Behavior::Animation::Easing;
using Behavior::Animation::Admission;

using Frame = std::array<uint8_t, 112>;
std::array<Frame, 6> rest;
const Face::AudioSnapshot silence{};
const char *testName = "startup";

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << testName << " at " << millis() << "ms: " << message
              << "\nReproduce: python3 scripts/check-animation-v2.py\n";
    std::abort();
  }
}

void readRest(const char *path) {
  const char *keys[] = {"rest.happy", "rest.surprised", "rest.angry",
                        "rest.vwv", "rest.sleepy", "rest.wink"};
  std::ifstream input(path);
  std::string line;
  unsigned seen = 0;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string key, discarded, frame;
    row >> key;
    for (unsigned expression = 0; expression < 6; ++expression) {
      if (key != keys[expression]) continue;
      for (unsigned i = 0; i < 4; ++i) row >> discarded;
      row >> frame;
      require(frame.size() == 224, "invalid archived logical frame");
      for (unsigned i = 0; i < 112; ++i)
        rest[expression][i] = static_cast<uint8_t>(std::stoul(frame.substr(i * 2, 2), nullptr, 16));
      seen |= 1u << expression;
    }
  }
  require(seen == 63, "all six independent P1 frames are required");
}

bool pixel(const Frame &frame, int x, int y) {
  return (frame[y * 14 + x / 8] & (128 >> (x % 8))) != 0;
}

void set(Frame &frame, int x, int y, bool value) {
  auto &byte = frame[y * 14 + x / 8];
  const auto bit = static_cast<uint8_t>(128 >> (x % 8));
  byte = value ? byte | bit : byte & ~bit;
}

// Reference uses inverse destination lookup into the frozen P1 eye masks.
// The non-target regions remain exactly as in the archived rest frame.
Frame transformed(unsigned base, unsigned sprite, int origin, int horizontal,
                  int vertical, int closure) {
  Frame expected = rest[base];
  for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) {
    const int oldX = x - horizontal, oldY = y - vertical;
    const bool lit = oldX >= 0 && oldX < 16 && oldY >= closure && oldY < 8 &&
        pixel(rest[sprite], origin + oldX, oldY);
    set(expected, origin + x, y, lit);
  }
  return expected;
}

Frame mouth(unsigned base, unsigned opening) {
  Frame expected = rest[base];
  for (int x : {16, 64}) for (int col = 0; col < 32; ++col) {
    const unsigned widen = col < 5 || col >= 27 ? opening / 2 : opening;
    for (unsigned y = 0; y < 8; ++y)
      set(expected, x + col, y, y == 3 - widen || y == 4 + widen);
  }
  return expected;
}

void compare(const Face::Frame &actual, const Frame &expected) {
  for (int y = 0; y < 8; ++y) for (int x = 0; x < 112; ++x) {
    const bool wanted = pixel(expected, x, y), observed = Face::pixel(actual, x, y);
    if (wanted != observed) {
      std::cerr << "first divergent pixel (" << x << ',' << y << ") expected "
                << wanted << " actual " << observed << '\n';
      require(false, "visual mismatch");
    }
  }
}

uint64_t worstComposeNs = 0;

void clipping() {
  testName = "all six sprites, signed offsets and closure";
  for (unsigned id = 0; id < 6; ++id) for (bool left : {false, true}) {
    for (int x = -2; x <= 2; ++x) for (int y = -1; y <= 1; ++y) for (int closure : {0, 3, 8}) {
      Face::Pose pose;
      pose.expression = id;
      auto &eye = left ? pose.leftEye : pose.rightEye;
      eye.x = x; eye.y = y; eye.closure = closure;
      Face::Frame actual;
      const auto start = std::chrono::steady_clock::now();
      Face::compose(actual, pose, silence);
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
      if (uint64_t(ns) > worstComposeNs) worstComposeNs = ns;
      compare(actual, transformed(id, id, left ? 96 : 0, x, y, closure));
    }
  }
}

uint32_t spiBytes = 0, spiTransactions = 0;
uint64_t worstRenderNs = 0;

void renderExpected(const Frame &expected, uint32_t modelNow) {
  const auto last = SPI.display;
  const auto bytes = SPI.bytes;
  const auto transactions = SPI.transactions;
  unsigned digits = 0;
  for (int y = 0; y < 8; ++y) {
    bool changed = false;
    for (int tile = 0; tile < 14; ++tile) changed |= last[tile][y] != expected[y * 14 + tile];
    digits += changed;
  }
  const auto start = std::chrono::steady_clock::now();
  renderFace(modelNow);
  const auto duration = std::chrono::steady_clock::now() - start;
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  if (uint64_t(ns) > worstRenderNs) worstRenderNs = ns;
  compare(displayedFrame, expected);
  for (int tile = 0; tile < 14; ++tile) for (int digit = 0; digit < 8; ++digit)
    require(SPI.display[tile][digit] == expected[digit * 14 + tile], "physical register mismatch");
  require(SPI.transactions - transactions == digits, "wrong dirty register count");
  require(SPI.bytes - bytes == 28 * digits, "wrong daisy-chain bytes");
  spiBytes += SPI.bytes - bytes;
  spiTransactions += SPI.transactions - transactions;
}

void trackCase(const char *name, Region region, Motion motion, int8_t value, const Frame &expected,
               bool visible = true) {
  testName = name;
  behavior = Behavior::Core{};
  config.transitionMs = config.naturalBlinks = config.idleMotion = 0;
  speakingActive = false;
  if (!visible) behavior.expression = ExpressionWink;
  const auto &base = rest[behavior.expression];
  renderExpected(base, 100);
  require((expected != base) == visible, "independent case has wrong visibility expectation");
  const auto initial = renderMetrics.compositions;
  const auto start = behavior.animations.start(100, {region, motion, 4, Easing::Linear, 100, value, value});
  if (start.status != Admission::Started)
    std::cerr << "track region " << unsigned(region) << " motion " << unsigned(motion) << '\n';
  require(start.status == Admission::Started, "track admission failed");
  if (visible) require(!visualCache.unchanged(behavior.poseAt(116, config, false, 0, false), silence),
                       "cache said unchanged before a visible update");
  renderExpected(expected, 116);
  require(renderMetrics.compositions > initial, "new resolved field skipped composition");
  const auto composed = renderMetrics.compositions;
  renderExpected(expected, 132);
  require(renderMetrics.compositions == composed, "same resolved field missed cache");
  renderExpected(base, 201);
}

void cacheAndSpi() {
  for (bool left : {false, true}) {
    const auto region = left ? Region::LeftEye : Region::RightEye;
    const int x = left ? 96 : 0;
    trackCase(left ? "left x" : "right x", region, Motion::Gaze, 2, transformed(0,0,x,2,0,0));
    trackCase(left ? "left y" : "right y", region, Motion::Vertical, -1, transformed(0,0,x,0,-1,0));
    trackCase(left ? "left lid" : "right lid", region, Motion::Lid, 8, transformed(0,0,x,0,0,8));
    trackCase(left ? "left squint" : "right squint", region, Motion::Squint, 4, transformed(0,0,x,0,0,4));
    trackCase(left ? "left expression" : "right expression", region, Motion::Expression, 3, transformed(0,3,x,0,0,0));
  }
  trackCase("mouth aperture", Region::FullFace, Motion::MouthAperture, 3, mouth(0,3));
  trackCase("clipped eyelid, same pixels", Region::RightEye, Motion::Lid, 2, rest[ExpressionWink], false);

  testName = "FFT snapshot and eye motion";
  behavior = Behavior::Core{};
  speakingActive = true;
  fftIcon1[0] = 0x55;
  fftIcon2L[0] = 0xaa;
  Face::AudioSnapshot before{}, after{};
  captureSpectrum(before, true, 0);
  auto pose = behavior.poseAt(300, config, true, 0, false);
  pose.leftEye.x = -2;
  Face::Frame first, unchanged, fresh;
  Face::compose(first, pose, before);
  fftIcon1[0] ^= 0xff;
  captureSpectrum(after, true, 0);
  Face::compose(unchanged, pose, before);
  Face::compose(fresh, pose, after);
  require(!memcmp(first.bytes, unchanged.bytes, 112), "old immutable snapshot changed");
  require(memcmp(first.bytes, fresh.bytes, 112), "new FFT bytes failed to alter appearance");
  Face::VisualCache cache;
  cache.remember(pose, before);
  require(!cache.unchanged(pose, after), "FFT snapshot suppressed by cache");

  Frame expected = transformed(0,0,96,-2,0,0);
  // Spectrum is only in the mouth rectangles, in the original bitmap order.
  for (unsigned y = 0; y < 8; ++y) for (unsigned i = 0; i < 2; ++i) {
    expected[y*14+2+i] = after.second[y*2+i];
    expected[y*14+4+i] = after.first[y*2+i];
    expected[y*14+8+i] = after.firstLeft[y*2+i];
    expected[y*14+10+i] = after.secondLeft[y*2+i];
  }
  compare(fresh, expected);
  pose.boop = true;
  Face::compose(fresh, pose, after);
  compare(fresh, rest[ExpressionVwv]);

  testName = "override cannot claim requested artwork is already visible";
  behavior = Behavior::Core{};
  speakingActive = false;
  responseTiming = {};
  const auto override = behavior.animations.start(400, {
      Region::FullFace, Motion::Expression, 7, Easing::Linear, 160, 3, 3});
  require(override.status == Admission::Started && override.handle.generation == 1, "override admission failed");
  const auto right = transformed(0,3,0,0,0,0);
  auto both = transformed(0,3,96,0,0,0);
  for (unsigned y = 0; y < 8; ++y) {
    both[y*14] = right[y*14];
    both[y*14+1] = right[y*14+1];
  }
  renderExpected(both, 410);
  config.transitionMs = 160;
  selectExpressionCommand(ExpressionAngry, 416, ResponseTiming::Usb, 1, micros());
  selectExpressionCommand(ExpressionHappy, 416, ResponseTiming::Uart, 2, micros());
  require(!responseTiming.targetAlreadyVisible && !responseTiming.onset,
          "causal reversal incorrectly treated an override as requested artwork");

  testName = "remote boop observes shifted Vwv rather than skipping";
  behavior = Behavior::Core{};
  behavior.expression = ExpressionVwv;
  responseTiming = {};
  const auto gaze = behavior.animations.start(500, {
      Region::RightEye, Motion::Gaze, 4, Easing::Linear, 100, 2, 2});
  require(gaze.status == Admission::Started, "boop fixture gaze failed");
  renderExpected(transformed(3,3,0,2,0,0), 500);
  matrix.shutdown(false);
  displayBlanked = false;
  PawLink::gate.epoch = 1;
  PawLink::gate.bridge = 42;
  PawLink::gate.issue(7, 505);
  HelmetLink::Packet command;
  command.kind = HelmetLink::Command; command.length = 8;
  command.sequence = 1; command.epoch = 1; command.ticket = 7;
  HelmetLink::put32(command.payload, 42);
  command.payload[4] = HelmetLink::Boop; command.payload[5] = 0;
  HelmetLink::put16(command.payload + 6, 100);
  PawLink::receive(command, 505);
  require(PawLink::lastResult == HelmetLink::Accepted && behavior.boopActive && responseTiming.pending,
          "changed boop appearance was not admitted for observation");
  renderExpected(rest[ExpressionVwv], 505);
  require(responseTiming.feedback && responseTiming.completed == 1, "boop feedback was not observed");
}

int main(int argc, char **argv) {
  require(argc == 2, "expected P1 fixture path");
  readRest(argv[1]);
  setup();
  clipping();
  cacheAndSpi();
  pauseMicrophone();
  delete sensorAdc;
  std::cout << "{\"clipping_cases\":540,\"spi_bytes\":" << spiBytes
            << ",\"spi_transactions\":" << spiTransactions
            << ",\"host_worst_render_ns\":" << worstRenderNs
            << ",\"host_worst_compose_ns\":" << worstComposeNs << "}\n";
}

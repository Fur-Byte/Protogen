#include <sketch.h>

#include <iomanip>

namespace {
static_assert(Face::Width == 112 && Face::Height == 8);
static_assert(ExpressionHappy == 0 && ExpressionSurprised == 1 && ExpressionAngry == 2 &&
              ExpressionVwv == 3 && ExpressionSleepy == 4 && ExpressionWink == 5 &&
              ExpressionCount == 6);

FakeSPI::Frame previous(14);
unsigned previousTransactions = 0, previousBytes = 0, previousShutdown = 0, previousWake = 0;

void checkpoint(const char *name) {
  // Independent rotation-3 wiring rule. Tile index increases with logical X.
  // A tile's digit is Y (zero based), with X=0 in its high bit.
  FakeSPI::Frame mapped(14);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 112; ++x) {
      const auto pixelMask = 128u >> (x % 8);
      if (displayedFrame.bytes[y * 14 + x / 8] & pixelMask)
        mapped[x / 8][y] |= pixelMask;
    }
  }
  assert(mapped == SPI.display);

  unsigned dirtyDigits = 0;
  for (int digit = 0; digit < 8; ++digit) {
    bool dirty = false;
    for (int tile = 0; tile < 14; ++tile)
      dirty = dirty || mapped[tile][digit] != previous[tile][digit];
    dirtyDigits += dirty;
  }
  const unsigned shutdowns = SPI.shutdownCommands - previousShutdown;
  const unsigned wakes = SPI.wakeCommands - previousWake;
  const unsigned transactions = SPI.transactions - previousTransactions;
  if (transactions != dirtyDigits + shutdowns + wakes) {
    std::cerr << name << ": " << transactions << " SPI transactions, " << dirtyDigits
              << " changed digits, " << shutdowns << " shutdowns, " << wakes << " wakes\n";
    assert(false);
  }
  assert(SPI.bytes - previousBytes == 28 * transactions);
  if (wakes)
    assert(SPI.wakeFrames.back() == mapped);

  std::cout << name << '\t' << SPI.shutdown << '\t' << dirtyDigits << '\t'
            << shutdowns << '\t' << wakes << '\t' << std::hex << std::setfill('0');
  for (auto byte : displayedFrame.bytes)
    std::cout << std::setw(2) << unsigned(byte);
  std::cout << '\t';
  for (const auto &tile : SPI.display)
    for (auto byte : tile)
      std::cout << std::setw(2) << unsigned(byte);
  std::cout << std::dec << '\n';

  previous = SPI.display;
  previousTransactions = SPI.transactions;
  previousBytes = SPI.bytes;
  previousShutdown = SPI.shutdownCommands;
  previousWake = SPI.wakeCommands;
}

void clockAt(uint64_t ms) {
  assert(ms * 1000 >= fakeUs);
  advanceTime(ms * 1000 - fakeUs);
}

void rest(Expression expression) {
  config.transitionMs = 0;
  selectExpression(expression, millis());
  renderFace(millis());
}

void transition() {
  rest(ExpressionHappy);
  checkpoint("transition.source");
  config.transitionMs = 160;
  // Call the sketch renderer at explicit boundary stamps. No target optical
  // timing or wall-clock assumptions are embedded in the fixture.
  const uint32_t start = UINT32_MAX - 75;
  selectExpression(ExpressionAngry, start);
  for (const auto &point : {std::pair{0u, "transition.0"}, {1u, "transition.1"},
                            {30u, "transition.30"}, {59u, "transition.59"},
                            {60u, "transition.60"}, {79u, "transition.79"},
                            {80u, "transition.80"}, {99u, "transition.99"},
                            {100u, "transition.100"}, {130u, "transition.130"},
                            {159u, "transition.159"}, {160u, "transition.160"}}) {
    renderFace(start + point.first);
    checkpoint(point.second);
  }
  assert(!transitionActive && displayedPose.expression == ExpressionAngry);

  selectExpression(ExpressionHappy, start + 200);
  renderFace(start + 230);
  checkpoint("retarget.closing");
  const auto previousPose = displayedPose;
  selectExpression(ExpressionSurprised, start + 231);
  renderFace(start + 231);
  assert(displayedPose.closure == previousPose.closure);
  checkpoint("retarget.new-target");
  selectExpression(ExpressionAngry, start + 233); // Reverse to the last visible expression.
  renderFace(start + 233);
  checkpoint("retarget.reverse");
  renderFace(start + 393);
  assert(!transitionActive && displayedPose.expression == ExpressionAngry);
  checkpoint("retarget.settled");
}

void blink() {
  rest(ExpressionHappy);
  checkpoint("blink.source");
  // Exercise the fixed blink path with its start near rollover.
  resetAnimations(UINT32_MAX - 100);
  nextBlinkInterval = 40;
  const uint32_t blinkAt = UINT32_MAX - 60;
  for (const auto &point : {std::pair{0u, "blink.0"}, {35u, "blink.35"}, {69u, "blink.69"},
                            {70u, "blink.70"}, {94u, "blink.94"}, {95u, "blink.95"},
                            {172u, "blink.172"}, {249u, "blink.249"}, {250u, "blink.250"}}) {
    const uint32_t now = blinkAt + point.first;
    Blink(now);
    renderFace(now);
    checkpoint(point.second);
  }
  assert(!blinkState && Step == 0);
  // Feed fixed cosmetic entropy words through the existing sampler and verify
  // the sampled double-blink scheduling without any physical entropy dependency.
  blinkTiming = BlinkTiming::sample(config, 0x20401234u, 0x00100020u);
  blinkDoublePending = blinkTiming.doubleBlink;
  nextBlinkInterval = blinkTiming.pauseMs;
  uint32_t now = blinkTime + nextBlinkInterval;
  Blink(now);
  renderFace(now);
  checkpoint("blink.sampled-start");
  Blink(now + blinkTiming.closeMs);
  renderFace(now + blinkTiming.closeMs);
  checkpoint("blink.sampled-closed");
  now += blinkTiming.duration();
  Blink(now);
  renderFace(now);
  assert(!blinkState && !blinkDoublePending && nextBlinkInterval == blinkTiming.followupMs);
  checkpoint("blink.sampled-finished");
  now += nextBlinkInterval;
  Blink(now);
  Blink(now + blinkTiming.closeMs);
  renderFace(now + blinkTiming.closeMs);
  checkpoint("blink.followup-closed");
  Blink(now + blinkTiming.duration());
  renderFace(now + blinkTiming.duration());
  assert(!blinkState && !blinkDoublePending);
  checkpoint("blink.followup-finished");
}

void precedence() {
  rest(ExpressionAngry);
  checkpoint("mouth.source");
  const uint32_t start = millis();
  glitch(start + Settings::GlitchIntervalMs);
  assert(counter == 1);
  renderFace(start + Settings::GlitchIntervalMs);
  checkpoint("mouth.glitch-1");
  glitch(start + Settings::GlitchIntervalMs + Settings::GlitchStepMs);
  assert(counter == 2);
  renderFace(start + Settings::GlitchIntervalMs + Settings::GlitchStepMs);
  checkpoint("mouth.glitch-2");

  // These bytes are controlled renderer inputs, as though written by the FFT.
  memset(fftIcon1, 0x55, sizeof(fftIcon1));
  memset(fftIcon2, 0x01, sizeof(fftIcon2));
  memset(fftIcon1L, 0x80, sizeof(fftIcon1L));
  memset(fftIcon2L, 0xaa, sizeof(fftIcon2L));
  speakingActive = true;
  renderFace(millis()); // Deliberate overlap with the previous glitch flag.
  checkpoint("mouth.speech-over-glitch");
  glitch(millis());
  assert(counter == 0);
  renderFace(millis());
  checkpoint("mouth.speech-alone");
  config.mouthMode = 1;
  mouthAperture = 3;
  renderFace(millis());
  checkpoint("mouth.amplitude");
  speakingActive = false;
  config.mouthMode = 0;
}

void visibility() {
  const uint64_t roll = uint64_t(UINT32_MAX) + 1;
  clockAt(roll - 240);
  rest(ExpressionAngry);
  checkpoint("visibility.source");
  setSleepingLocal(true);
  loop();
  assert(SPI.shutdown);
  checkpoint("visibility.sleep");
  selectExpression(ExpressionSleepy, millis());
  loop();
  checkpoint("visibility.selection-while-asleep");
  setSleepingLocal(false);
  loop();
  assert(!SPI.shutdown && displayedPose.expression == ExpressionSleepy);
  checkpoint("visibility.wake");

  // Model the accepted boop event at the same boundary used by PawLink.
  const uint64_t boopAt = fakeUs / 1000;
  boopTime = millis();
  remoteBoopDuration = 500;
  boopActive = boopFromRemote = true;
  speakingActive = true;
  counter = 2;
  Step = 8;
  renderFace(millis());
  assert(displayedPose.boop && displayedPose.speaking);
  checkpoint("boop.overrides-speech-glitch-closure");
  visorInput = HIGH;
  clockAt(roll - 220);
  loop();
  checkpoint("visibility.visor-debounce-start");
  clockAt(roll - 221 + Settings::VisorDebounceMs);
  loop();
  assert(!SPI.shutdown);
  checkpoint("visibility.visor-debounce-before");
  clockAt(roll - 220 + Settings::VisorDebounceMs);
  loop();
  assert(SPI.shutdown);
  checkpoint("visibility.visor-hidden");

  const auto expiry = boopAt + remoteBoopDuration;
  clockAt(expiry - 1);
  loop();
  assert(boopActive);
  checkpoint("boop.before-expiry-hidden-wrap");
  clockAt(expiry);
  loop();
  assert(!boopActive && !boopFromRemote);
  checkpoint("boop.expired-hidden-wrap");
  visorInput = LOW;
  clockAt(expiry + 2);
  loop();
  checkpoint("visibility.visor-show-debounce");
  clockAt(expiry + 2 + Settings::VisorDebounceMs);
  loop();
  assert(!SPI.shutdown && !displayedPose.boop && displayedPose.expression == ExpressionSleepy);
  checkpoint("visibility.visor-reopened");
}
}

int main() {
  assert(!Settings::EarlyExpressionReveal);
  setup();
  config.naturalBlinks = 0;
  config.idleMotion = 0;
  microphoneEnabled = false;
  // Exclude driver initialization/scan configuration from frame deltas.
  previous = SPI.display;
  previousTransactions = SPI.transactions;
  previousBytes = SPI.bytes;
  previousShutdown = SPI.shutdownCommands;
  previousWake = SPI.wakeCommands;
  loop();
  checkpoint("rest.happy");
  renderFace(millis());
  checkpoint("rest.unchanged");
  for (const auto &item : {std::pair{ExpressionSurprised, "rest.surprised"},
                          {ExpressionAngry, "rest.angry"}, {ExpressionVwv, "rest.vwv"},
                          {ExpressionSleepy, "rest.sleepy"}, {ExpressionWink, "rest.wink"}}) {
    rest(item.first);
    checkpoint(item.second);
  }
  transition();
  blink();
  precedence();
  visibility();
}

#include "sketch.h"

// These steps exercise loop servicing, not instantaneous edges. ADC/SPI/FFT
// duration remains a hardware-only measurement.
static void runMs(uint32_t duration) {
  for (uint32_t i = 0; i < duration; ++i) { fakeUs += 1000; loop(); }
}
static void stableButton(int value) {
  buttonInput = value;
  runMs(Settings::ButtonDebounceMs + 2);
}
static void shortPress() { stableButton(LOW); stableButton(HIGH); }
static void stableVisor(int value) {
  visorInput = value;
  runMs(Settings::VisorDebounceMs + 2);
}
static bool indicatorOff() {
  return pwmValues[redPin] == 0 && pwmValues[greenPin] == 0 && pwmValues[bluePin] == 0;
}
static bool sameMouth(const FakeSPI::Frame &left, const FakeSPI::Frame &right) {
  for (const int tile : {2, 3, 4, 5, 8, 9, 10, 11})
    if (left[tile] != right[tile]) return false;
  return true;
}

int main() {
  assert(!SPI.begun); // Constructors must not access Teensy SPI hardware.
  setup(); loop();
  assert(pinModes[VisorPin] == INPUT_PULLUP);
  assert(state == Settings::StartupExpression && !sleeping && !SPI.shutdown);
  auto happy = SPI.display;
  auto bytes = SPI.bytes;
  for (int i = 0; i < 100; ++i) loop();
  assert(SPI.bytes == bytes);
  runMs(Settings::IndicatorMs + 1);
  assert(indicatorOff());
  auto writes = pwmWrites;
  auto digitalReads = digitalReadsByPin;
  for (int i = 0; i < 100; ++i) loop();
  assert(pwmWrites == writes);
  assert(digitalReadsByPin == digitalReads); // Controls poll at most once per ms.

  // Independently calculated tile mappings verify all 3,584 rotated pixels.
  for (int rot = 0; rot < 4; ++rot) {
    for (int tile = 0; tile < 14; ++tile) {
      matrix.setRotation(tile, rot);
      for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        matrix.fillScreen(0); matrix.write();
        int col = 0, bit = 0;
        switch (rot) {
          case 0: col = x; bit = y; break;
          case 1: col = 7-y; bit = x; break;
          case 2: col = 7-x; bit = 7-y; break;
          case 3: col = y; bit = 7-x; break;
        }
        bytes = SPI.bytes;
        matrix.drawPixel(tile*8+x, y, 1); matrix.write();
        assert(SPI.bytes-bytes == 28);
        for (int t = 0; t < 14; ++t) for (int c = 0; c < 8; ++c)
          assert(SPI.display[t][c] == ((t == tile && c == col) ? (1u << bit) : 0));
        bytes = SPI.bytes;
        matrix.drawPixel(tile*8+x, y, 1); matrix.write();
        assert(SPI.bytes == bytes);
      }
      matrix.setRotation(tile, 3);
    }
  }
  rising = true; loop(); assert(SPI.display == happy);
  fakeUs += (Settings::BlinkIntervalMs + 1) * 1000ULL;
  loop(); assert(Step == 1);
  for (int i = 0; i < 15; ++i) { fakeUs += (Settings::BlinkStepMs + 1) * 1000ULL; loop(); }
  assert(Step == 0 && !blinkState && SPI.display == happy);

  // Contact chatter never becomes a press. Real presses act on release only.
  const auto initialState = state;
  buttonInput = LOW; runMs(Settings::ButtonDebounceMs - 1);
  buttonInput = HIGH; runMs(Settings::ButtonDebounceMs + 2);
  assert(state == initialState && !sleeping);
  Step = 5; blinkState = true;
  stableButton(LOW); assert(state == initialState);
  stableButton(HIGH); assert(state == 1 && Step == 0 && !blinkState);
  const auto surprised = SPI.display;

  // A long hold sleeps once; continuing or releasing it cannot cycle/wake.
  auto shutdowns = SPI.shutdownCommands;
  auto wakes = SPI.wakeCommands;
  stableButton(LOW); runMs(Settings::ButtonHoldMs);
  assert(sleeping && displayBlanked && SPI.shutdown && state == 1);
  assert(indicatorOff() && !microphoneSampling && !speakingActive);
  assert(SPI.shutdownCommands == shutdowns + 1 && SPI.wakeCommands == wakes);
  bytes = SPI.bytes; auto reads = adcReads; writes = pwmWrites;
  runMs(Settings::ButtonHoldMs * 2);
  assert(sleeping && SPI.bytes == bytes && adcReads == reads && pwmWrites == writes && state == 1);
  stableButton(HIGH);
  assert(sleeping && SPI.bytes == bytes && adcReads == reads && pwmWrites == writes);
  shortPress();
  assert(!sleeping && !displayBlanked && !SPI.shutdown && state == 1);
  assert(SPI.display == surprised && SPI.wakeFrames.back() == surprised);
  assert(SPI.wakeCommands == wakes + 1);

  // Holding can wake too, but one held contact toggles only once.
  stableButton(LOW); runMs(Settings::ButtonHoldMs); stableButton(HIGH);
  assert(sleeping);
  stableButton(LOW); runMs(Settings::ButtonHoldMs);
  assert(!sleeping && state == 1);
  runMs(Settings::ButtonHoldMs * 2); stableButton(HIGH);
  assert(!sleeping && state == 1);

  shortPress(); assert(state == 2);
  const auto angry = SPI.display;
  visorInput = HIGH; runMs(Settings::VisorDebounceMs - 1);
  visorInput = LOW; runMs(Settings::VisorDebounceMs + 2);
  assert(!SPI.shutdown); // Brief visor chatter is ignored.
  stableVisor(HIGH);
  assert(SPI.shutdown && displayBlanked && indicatorOff());
  bytes = SPI.bytes; reads = adcReads; shutdowns = SPI.shutdownCommands;
  runMs(Settings::GlitchIntervalMs + 1);
  assert(SPI.bytes == bytes && adcReads == reads && SPI.shutdownCommands == shutdowns);
  stableVisor(LOW);
  assert(!SPI.shutdown && !displayBlanked && counter == 0 && Step == 0);
  assert(SPI.display == angry && SPI.wakeFrames.back() == angry);

  // Change the selection while hidden: the new frame must exist before enable,
  // otherwise matching an unchanged retained frame would hide ordering bugs.
  stableVisor(HIGH);
  bytes = SPI.bytes; reads = adcReads; writes = pwmWrites;
  auto hiddenWrites = SPI.hiddenDigitWrites;
  shortPress(); assert(state == 3 && SPI.shutdown);
  assert(SPI.bytes == bytes && adcReads == reads && pwmWrites == writes);
  stableVisor(LOW);
  assert(!SPI.shutdown && SPI.display != angry && SPI.wakeFrames.back() == SPI.display);
  assert(SPI.hiddenDigitWrites > hiddenWrites);
  shortPress(); shortPress(); shortPress();
  assert(state == 2 && SPI.display == angry);

  // Boop hysteresis rejects threshold jitter; timeouts require a real release.
  boopInput = Settings::BoopThreshold; runMs(Settings::BoopPollMs + 1);
  assert(boopActive);
  const auto boop = SPI.display;
  boopInput = (Settings::BoopThreshold + Settings::BoopReleaseThreshold) / 2;
  runMs(Settings::BoopPollMs + 1); assert(boopActive);
  stableVisor(HIGH); stableVisor(LOW);
  assert(SPI.display == boop && SPI.wakeFrames.back() == boop);
  boopInput = Settings::BoopReleaseThreshold; runMs(Settings::BoopPollMs + 1);
  assert(!boopActive && boopArmed);
  boopInput = (Settings::BoopThreshold + Settings::BoopReleaseThreshold) / 2;
  runMs(Settings::BoopPollMs + 1); assert(!boopActive);
  boopInput = Settings::BoopThreshold; runMs(Settings::BoopPollMs + 1);
  assert(boopActive);
  runMs(Settings::BoopDurationMs + 2);
  assert(!boopActive && !boopArmed);
  runMs(Settings::BoopPollMs * 3); assert(!boopActive);
  boopInput = Settings::BoopReleaseThreshold; runMs(Settings::BoopPollMs + 1);
  assert(boopArmed);
  boopInput = Settings::BoopThreshold; runMs(Settings::BoopPollMs + 1); assert(boopActive);
  boopInput = 1023; runMs(Settings::BoopPollMs + 1); assert(!boopActive);

  // Speech owns the mouth and postpones angry glitches.
  fakeUs += (Settings::GlitchIntervalMs + 1) * 1000ULL;
  speakingActive = true; speakingDelay = millis();
  loop(); assert(counter == 0);
  speakingActive = false;
  fakeUs += (Settings::GlitchIntervalMs + 1) * 1000ULL;
  loop(); assert(counter == 1);
  fakeUs += (Settings::GlitchStepMs + 1) * 1000ULL;
  loop(); assert(counter == 2);
  fakeUs += (Settings::GlitchStepMs + 1) * 1000ULL;
  loop(); assert(counter == 0 && sameMouth(SPI.display, angry));

  // Hold/debounce still work through the millis() wrap at 49.7 days.
  fakeUs = (static_cast<uint64_t>(UINT32_MAX) - Settings::ButtonHoldMs / 2) * 1000ULL;
  loop(); stableButton(LOW); runMs(Settings::ButtonHoldMs);
  assert(sleeping && state == 2);
  stableButton(HIGH); shortPress(); assert(!sleeping && state == 2);
  fakeUs = (static_cast<uint64_t>(UINT32_MAX) * 2 + 1 - Settings::VisorDebounceMs / 2) * 1000ULL;
  loop(); stableVisor(HIGH); assert(SPI.shutdown);
  stableVisor(LOW); assert(!SPI.shutdown);

  // One sample per deadline, none before it; missed periods restart a frame.
  pauseMicrophone(); fakeUs = 100000000;
  reads = adcReads; microphone(); assert(adcReads == reads + 1);
  microphone(); assert(adcReads == reads + 1);
  fakeUs += 250; microphone(); assert(microphoneSampleCount == 2);
  fakeUs += 1000; microphone(); assert(microphoneSampleCount == 1);
  fakeUs += samplingPeriodUs * 2 - 1; microphone();
  reads = adcReads;
  fakeUs += 1; microphone(); assert(adcReads == reads); // No catch-up burst.
  fakeUs += samplingPeriodUs - 1; microphone(); assert(adcReads == reads + 1);
  pauseMicrophone(); assert(microphoneSampleCount == 0 && !microphoneSampling);
  fakeUs = UINT32_MAX - 100; microphone();
  fakeUs += 250; microphone(); assert(microphoneSampleCount == 2);

  // Loud synthetic input produces a spectrum, then silence restores the mouth.
  pauseMicrophone();
  for (int i = 0; i < 64; ++i) {
    micInput = (i & 1) ? 310 : 710;
    fakeUs += 250; microphone();
  }
  assert(speakingActive && microphoneSampleCount == 0);
  micInput = 510;
  for (int i = 0; i < 1200; ++i) { fakeUs += 250; microphone(); }
  assert(!speakingActive);

  // Bias changes during a pause must not trigger speech when input is silent.
  for (const int bias : {900, 100, 510}) {
    pauseMicrophone(); micInput = bias;
    for (int i = 0; i < 128; ++i) { fakeUs += 250; microphone(); }
    assert(!speakingActive && average == 0);
  }

  // Float + cached window matches an independently DC-corrected double FFT.
  double real64[64], imag64[64] = {};
  for (int i = 0; i < 64; ++i) {
    vReal[i] = real64[i] = 510 + 100*sin(6.28318530718*7*i/64);
    vImag[i] = 0;
  }
  ArduinoFFT<double> reference(real64, imag64, 64, 4000);
  reference.dcRemoval();
  reference.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  reference.compute(FFTDirection::Forward); reference.complexToMagnitude();
  fft(); real64[0] = real64[4]; real64[1] = real64[8];
  for (int i = 0; i < 32; ++i) assert(fabs(vReal[i]-real64[i]) < 0.02);

  // No non-DC energy from a constant input, at any valid ADC bias.
  for (const float bias : {0.0f, 100.0f, 510.0f, 1023.0f}) {
    for (int i = 0; i < 64; ++i) { vReal[i] = bias; vImag[i] = 0; }
    fft();
    for (int i = 0; i < 32; ++i) assert(fabs(vReal[i]) < 0.001f);
  }

  // Every FFT bar height stays in bounds and mirrors the corresponding half.
  // An oversized count may not read beyond the 32 available source bins.
  float magnitudes[32];
  for (const float level : {-100.0f, 0.0f, 400.0f, 800.0f, 65536.0f}) {
    for (auto &magnitude : magnitudes) magnitude = level;
    renderFFTBars(magnitudes, UINT16_MAX);
    const int height = level <= 0 ? 1 : (level == 400 ? 4 : 8);
    for (int row = 0; row < 8; ++row) {
      const byte expected = row >= 8 - height ? 0xff : 0;
      for (int col = 0; col < 2; ++col) {
        assert(fftIcon1[row*2+col] == expected && fftIcon2[row*2+col] == expected);
        assert(fftIcon1L[row*2+col] == expected && fftIcon2L[row*2+col] == expected);
      }
    }
  }
  renderFFTBars(nullptr, 0); // Empty input clears all bars without dereferencing.
  for (int i = 0; i < 16; ++i)
    assert(!(fftIcon1[i] | fftIcon2[i] | fftIcon1L[i] | fftIcon2L[i]));

  // Service the whole loop with modeled ADC and 4 MHz SPI cost. Audio must
  // recover from rendering gaps while controls and blinking continue to work.
  fakeUs = 1000000000;
  pauseMicrophone(); state = 0; rising = true;
  adcReadDurationUs = 10; spiByteDurationUs = 2; // Models 4 MHz; shipped rate is 8 MHz (1 µs/byte).
  syntheticMicrophone = recordMicrophoneTimes = true;
  loop();
  blinkTime = millis() - Settings::BlinkIntervalMs;
  bool blinkSeen = false;
  const uint64_t end = fakeUs + 400000;
  while (fakeUs < end) { fakeUs += 20; loop(); blinkSeen |= blinkState; }
  assert(speakingActive && microphoneTimes.size() > 1000 && blinkSeen && Step == 0);
  for (size_t i = 1; i < microphoneTimes.size(); ++i)
    assert(microphoneTimes[i] - microphoneTimes[i-1] >= samplingPeriodUs);
  stableButton(LOW); stableButton(HIGH);
  assert(state == 1);
  syntheticMicrophone = recordMicrophoneTimes = false;
  adcReadDurationUs = spiByteDurationUs = 0;
  std::cout << "PASS: 3584 pixel/rotation cases, idle SPI/PWM/control polling, blink, "
               "button debounce/release/holds, sleep/wake shutdown ordering and quiet ADC, "
               "visor debounce, boop hysteresis/timeout/rearm, speech/glitch priority, "
               "millis/micros rollover, microphone cadence/recovery/bias/silence, "
               "DC-corrected FFT, spectrum bounds, audio with modeled SPI/ADC costs\n";
}

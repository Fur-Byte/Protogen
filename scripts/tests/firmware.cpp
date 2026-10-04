#include "sketch.h"

// These steps exercise loop servicing, not instantaneous edges. ADC/SPI/FFT
// duration remains a hardware-only measurement.
static void runMs(uint32_t duration) {
  for (uint32_t i = 0; i < duration; ++i) { advanceTime(1000); loop(); }
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
  setup();
  config.transitionMs = 0; config.naturalBlinks = 0;
  resetAnimations(millis()); loop();
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

  // Compare the new renderer against independent legacy drawing for all four
  // original expressions, including exact mouth/nose positions and panel order.
  for (uint8_t expression = 0; expression < 4; ++expression) {
    matrix.fillScreen(0);
    matrix.drawBitmap(MawLeft, 0, mawL, 32, 8, 1); matrix.drawBitmap(MawRight, 0, maw, 32, 8, 1);
    matrix.drawBitmap(NoseLeft, 0, noseL, 8, 8, 1); matrix.drawBitmap(NoseRight, 0, nose, 8, 8, 1);
    const uint8_t *left[] = {EyeL, SpookedL, AngryL, vwvL};
    const uint8_t *right[] = {Eye, Spooked, Angry, vwv};
    matrix.drawBitmap(EyeLeft, 0, left[expression], 16, 8, 1);
    matrix.drawBitmap(EyeRight, 0, right[expression], 16, 8, 1); matrix.write();
    const auto expected = SPI.display;
    state = expression; frameValid = false; rising = true; loop();
    assert(SPI.display == expected);
  }
  state = ExpressionHappy; frameValid = false; rising = true; loop();

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
  frameValid = false; rising = true; loop(); assert(SPI.display == happy);
  advanceTime((config.blinkInterval + 1) * 1000ULL);
  loop(); runMs(10); assert(Step == 1);
  runMs(blinkTiming.duration());
  assert(Step == 0 && !blinkState && SPI.display == happy);

  // Contact chatter never becomes a press. Real presses act on release only.
  const auto initialState = state;
  buttonInput = LOW; runMs(Settings::ButtonDebounceMs - 1);
  buttonInput = HIGH; runMs(Settings::ButtonDebounceMs + 2);
  assert(state == initialState && !sleeping);
  Step = 5; blinkState = true;
  stableButton(LOW); assert(state == initialState);
  stableButton(HIGH); runMs(16); assert(state == 1 && Step == 0 && !blinkState);
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
  selectExpression(ExpressionAngry, millis()); runMs(20);
  assert(state == 2 && SPI.display == angry);

  // Boop hysteresis rejects threshold jitter; timeouts require a real release.
  boopInput = Settings::BoopThreshold; runMs(Settings::BoopPollMs + 1);
  assert(boopActive);
  const auto boop = SPI.display;
  boopInput = (Settings::BoopThreshold + Settings::BoopReleaseThreshold) / 2;
  runMs(Settings::BoopPollMs + 1); assert(boopActive);
  stableVisor(HIGH); stableVisor(LOW);
  assert(SPI.display == boop && SPI.wakeFrames.back() == boop);
  boopInput = Settings::BoopReleaseThreshold; runMs(20);
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
  advanceTime((Settings::GlitchIntervalMs + 1) * 1000ULL);
  speakingActive = true; speakingDelay = millis();
  loop(); assert(counter == 0);
  speakingActive = false;
  advanceTime((Settings::GlitchIntervalMs + 1) * 1000ULL);
  loop(); assert(counter == 1);
  advanceTime((Settings::GlitchStepMs + 1) * 1000ULL);
  loop(); assert(counter == 2);
  advanceTime((Settings::GlitchStepMs + 1) * 1000ULL);
  loop(); assert(counter == 0 && sameMouth(SPI.display, angry));

  // Hold/debounce still work through the millis() wrap at 49.7 days.
  fakeUs = (static_cast<uint64_t>(UINT32_MAX) - Settings::ButtonHoldMs / 2) * 1000ULL;
  loop(); stableButton(LOW); runMs(Settings::ButtonHoldMs);
  assert(sleeping && state == 2);
  stableButton(HIGH); shortPress(); assert(!sleeping && state == 2);
  fakeUs = (static_cast<uint64_t>(UINT32_MAX) * 2 + 1 - Settings::VisorDebounceMs / 2) * 1000ULL;
  loop(); stableVisor(HIGH); assert(SPI.shutdown);
  stableVisor(LOW); assert(!SPI.shutdown);

  // Hardware sampling continues during main-loop and display work, with exact
  // nominal cadence in this model. No callback is synthesized by microphone().
  pauseMicrophone(); fakeUs = 100000000;
  microphone(); assert(microphoneSampling);
  reads = adcReadsByPin[A0];
  microphone(); assert(adcReadsByPin[A0] == reads);
  advanceTime(249); assert(adcReadsByPin[A0] == reads);
  advanceTime(1); assert(adcReadsByPin[A0] == reads + 1);
  advanceTime(1000); microphone(); assert(microphoneSampleCount == 5);
  // A stalled consumer overflows a bounded buffer, reports it, and discards the gap.
  advanceTime(200000); assert(audioDroppedSamples > 0);
  microphone(); assert(microphoneSampleCount == 0 && audioRead == audioWrite);
  pauseMicrophone(); assert(!microphoneSampling && sensorAdc->adc0->getTimerFrequency() == 0);
  reads = adcReadsByPin[A0]; advanceTime(5000); assert(adcReadsByPin[A0] == reads);

  // Early and late six-ms sounds must both activate the mouth; frame alignment
  // cannot change the detection outcome. Quiet DC changes do not activate it.
  for (const int burstStart : {8, 40}) {
    pauseMicrophone(); micInput = 510; noiseFloor = 0;
    microphone();
    for (int i = 0; i < 64; ++i) {
      micInput = i >= burstStart && i < burstStart + 24 ? ((i & 1) ? 310 : 710) : 510;
      advanceTime(250); microphone();
    }
    assert(speakingActive && microphoneSampleCount == 0);
    micInput = 510;
    for (int i = 0; i < 1200; ++i) { advanceTime(250); microphone(); }
    assert(!speakingActive);
  }
  for (const int bias : {900, 100, 510}) {
    pauseMicrophone(); micInput = bias; microphone();
    for (int i = 0; i < 128; ++i) { advanceTime(250); microphone(); }
    assert(!speakingActive && average == 0);
  }
  pauseMicrophone(); fakeUs = UINT32_MAX - 100; microphone();
  advanceTime(1000); microphone(); assert(microphoneSampleCount == 4);

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

  // Sampling survives full SPI frames, blinking, expression changes, and
  // simultaneous boop ADC work. SPI wire cost is derived from selected clock.
  pauseMicrophone(); fakeUs = 1000000000;
  state = 0; rising = true; frameValid = false;
  adcReadDurationUs = 10;
  syntheticMicrophone = recordMicrophoneTimes = true;
  loop();
  microphoneTimes.clear();
  blinkTime = millis() - config.blinkInterval;
  bool blinkSeen = false;
  const uint64_t end = fakeUs + 400000;
  while (fakeUs < end) { advanceTime(20); loop(); blinkSeen |= blinkState; }
  assert(speakingActive && microphoneTimes.size() > 1000 && blinkSeen && Step == 0);
  assert(SPI.clock == Settings::SpiFrequencyHz);
  for (size_t i = 1; i < microphoneTimes.size(); ++i)
    assert(microphoneTimes[i] - microphoneTimes[i-1] == samplingPeriodUs);
  // A separate slow-transfer stress case models 1 MHz wire cost while the
  // selected SPI clock stays at 4 MHz. Acquisition must remain independent.
  spiByteDurationUs = 8; microphoneTimes.clear();
  const auto dropsBeforeSlowSPI = audioDroppedSamples;
  matrix.fillScreen(0); matrix.write(); frameValid = false; rising = true;
  loop();
  const uint64_t slowEnd = fakeUs + 120000;
  while (fakeUs < slowEnd) { advanceTime(20); loop(); }
  assert(speakingActive && microphoneTimes.size() > 400 && audioDroppedSamples == dropsBeforeSlowSPI);
  for (size_t i = 1; i < microphoneTimes.size(); ++i)
    assert(microphoneTimes[i] - microphoneTimes[i-1] == samplingPeriodUs);
  stableButton(LOW); stableButton(HIGH); assert(state == 1);
  syntheticMicrophone = recordMicrophoneTimes = false;
  adcReadDurationUs = spiByteDurationUs = 0; micInput = 510;

  // A hidden visor cannot restart the original boop lifetime, including sleep.
  boopInput = 1023; runMs(20);
  boopInput = 100; runMs(20); assert(boopActive);
  const uint32_t originalBoopTime = boopTime;
  runMs(5500); stableVisor(HIGH); runMs(700);
  assert(!boopActive && !boopArmed);
  stableVisor(LOW); assert(!boopActive && boopTime == originalBoopTime);
  stableVisor(HIGH); stableVisor(LOW); assert(!boopActive);
  boopInput = 1023; runMs(20); boopInput = 100; runMs(20);
  assert(boopActive);
  sleeping = true; loop(); runMs(Settings::BoopDurationMs + 1);
  sleeping = false; loop(); assert(!boopActive && !boopArmed);
  boopInput = 1023; runMs(20);

  // Transitions close the old eyes before opening the new expression, end on
  // exact original artwork, and remain interruptible by sleep or another choice.
  config.transitionMs = 160;
  selectExpression(ExpressionHappy, millis()); runMs(200);
  selectExpression(ExpressionAngry, millis()); assert(transitionActive);
  runMs(80); assert(transitionActive);
  for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) {
    assert(!Face::pixel(displayedFrame, Face::EyeLeft+x, y));
    assert(!Face::pixel(displayedFrame, Face::EyeRight+x, y));
  }
  runMs(100); assert(!transitionActive && SPI.display == angry);
  selectExpression(ExpressionSleepy, millis()); runMs(200); assert(state == 4 && SPI.display != happy);
  selectExpression(ExpressionWink, millis()); runMs(200); assert(state == 5);
  shortPress(); assert(state == 0); runMs(200); assert(SPI.display == happy);
  selectExpression(ExpressionAngry, millis()); sleeping = true; loop();
  assert(!transitionActive && SPI.shutdown); sleeping = false; loop();
  assert(!SPI.shutdown && SPI.display == angry);

  // Fresh hardware entropy is requested per ordinary sequence. No entropy must
  // leave controls and rendering responsive, with no fabricated random fallback.
  config.naturalBlinks = 1; config.blinkJitter = 2000;
  Entropy.ready = false;
  scheduleBlink();
  const auto entropyReads = Entropy.reads;
  const auto expressionBeforeWait = state;
  runMs(config.blinkInterval + config.blinkJitter + 1);
  assert(blinkRandomPending && !blinkState && Entropy.reads == entropyReads);
  shortPress(); assert(state == (expressionBeforeWait + 1) % ExpressionCount);
  Entropy.ready = true; Entropy.budget = 1;
  serviceBlinkRandomness(); assert(blinkRandomPending && blinkRandomCount == 1);
  assert(Entropy.reads == entropyReads + 1);
  serviceBlinkRandomness(); assert(blinkRandomPending && Entropy.reads == entropyReads + 1);
  Entropy.budget = -1; loop(); assert(!blinkRandomPending && Entropy.reads == entropyReads + 2);

  // Natural pauses stay bounded and vary, and each sequence consumes fresh bits.
  uint32_t firstDelay = 0; bool varied = false;
  for (int i = 0; i < 100; ++i) {
    const auto readsBeforeSchedule = Entropy.reads;
    scheduleBlink(); serviceBlinkRandomness();
    assert(!blinkRandomPending && Entropy.reads == readsBeforeSchedule + 2);
    assert(nextBlinkInterval >= config.blinkInterval - config.blinkJitter);
    assert(nextBlinkInterval <= config.blinkInterval + config.blinkJitter);
    if (!i) firstDelay = nextBlinkInterval;
    varied |= nextBlinkInterval != firstDelay;
  }
  assert(varied);

  // A deliberately selected entropy packet produces exactly two blinks. The
  // third waits for a new ordinary pause; resets cancel a planned follow-up.
  Entropy.words = {0x80008000, 0};
  state = ExpressionHappy; resetAnimations(millis()); serviceBlinkRandomness();
  assert(blinkDoublePending);
  Blink(blinkTime + nextBlinkInterval); assert(blinkState);
  Blink(blinkSpeed + blinkTiming.closeMs); assert(Step == 8);
  Blink(blinkSpeed + blinkTiming.duration());
  assert(!blinkState && !blinkDoublePending && !blinkRandomPending);
  assert(nextBlinkInterval == blinkTiming.followupMs);
  Blink(blinkTime + nextBlinkInterval); assert(blinkState);
  Blink(blinkSpeed + blinkTiming.duration());
  assert(!blinkState && blinkRandomPending);
  Entropy.words = {0x80008000, 0};
  resetAnimations(millis()); serviceBlinkRandomness(); assert(blinkDoublePending);
  sleeping = true; loop(); assert(!blinkDoublePending && !blinkState);
  sleeping = false; loop();

  // Elapsed-time motion survives millis rollover and returns to an exact face.
  config.naturalBlinks = 0; resetAnimations(0xfffffff0UL);
  Blink(blinkTime + nextBlinkInterval);
  assert(blinkState);
  Blink(blinkSpeed + blinkTiming.closeMs); assert(Step == 8);
  Blink(blinkSpeed + blinkTiming.duration()); assert(!blinkState && Step == 0);
  resetAnimations(millis());

  // Noise calibration learns a steady quiet environment, without speech, and
  // the amplitude style responds with bounded, smoothed mouth openings.
  config.mouthMode = 1; syntheticMicrophone = true;
  assert(calibrateMicrophone()); runMs(550);
  assert(!calibratingAudio && !speakingActive && noiseFloor > 0 && config.noiseFloor > 0);
  assert(saveStoredConfig(config)); RuntimeConfig savedAudio;
  assert(loadStoredConfig(savedAudio) && savedAudio.noiseFloor == config.noiseFloor);
  syntheticMicrophone = false; micInput = 510;
  noiseFloor = 0; pauseMicrophone(); runMs(20);
  for (int i = 0; i < 128; ++i) { micInput = (i & 1) ? 310 : 710; advanceTime(250); loop(); }
  assert(speakingActive && mouthAperture >= 1 && mouthAperture <= 3);
  syntheticMicrophone = false; micInput = 510; pauseMicrophone();
  const auto dropsBeforeWrap = audioDroppedSamples;
  runMs(17000); // SPSC read/write sequence counters wrap at 16.384 seconds.
  assert(audioDroppedSamples == dropsBeforeWrap && !speakingActive);
  std::cout << "PASS: 3584 mapping cases, controls, sleep/wake, boop lifetime while hidden, "
               "buffered hardware audio, burst detection, overruns, calibration, FFT, "
               "animation transitions, new expressions, natural blinking, SPI cadence\n";
}

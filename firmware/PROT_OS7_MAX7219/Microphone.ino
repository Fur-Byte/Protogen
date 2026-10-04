// Service at most one ADC sample per loop; never busy-wait for a 16 ms FFT frame.
void microphone() {
#ifdef Microphone
  const uint32_t now = micros();
  const bool starting = !microphoneSampling;
  if (!microphoneSampling) {
    microphoneSampling = true;
    nextSampleTime = now;
  }
  if (static_cast<int32_t>(now - nextSampleTime) < 0) {
    return;
  }
  // If rendering or another task missed a whole period, discard the partial
  // frame. Do not fabricate equally spaced samples with back-to-back ADC reads.
  if (now - nextSampleTime >= samplingPeriodUs) {
    microphoneSampleCount = 0;
    nextSampleTime = now;
  }
  // Schedule from the actual read time. Advancing an old deadline can cause
  // a near-immediate second sample after a delay of just under one period.
  nextSampleTime = now + samplingPeriodUs;

  const float sample = static_cast<float>(analogRead(micPin));
  if (starting) {
    // A sensor's DC bias can change while asleep/hidden/booped. Start from
    // the current input so that a silent new bias is not treated as speech.
    filterDCComponent = sample;
  }
  filterDCComponent = sample * 0.01f + filterDCComponent * 0.99f;
  const int amplitude = static_cast<int>(fabsf(sample - filterDCComponent));
  total -= readings[readIndex];
  readings[readIndex] = amplitude;
  total += amplitude;
  if (++readIndex >= numReadings) {
    readIndex = 0;
  }
  average = total / numReadings;

  vReal[microphoneSampleCount] = sample;
  vImag[microphoneSampleCount] = 0.0f;
  if (++microphoneSampleCount == samples) {
    microphoneSampleCount = 0;
    if (average > Settings::MicrophoneSensitivity) {
      fft(); // fft() computes magnitudes and calls renderFFTBars() to update fftIcon* buffers
      speakingActive = true;
      matrix.fillRect(MawLeft, 0, 32, 8, LOW);
      matrix.fillRect(MawRight, 0, 32, 8, LOW);
      matrix.drawBitmap(MawLeft + 16, 0, fftIcon2L, 16, 8, HIGH);
      matrix.drawBitmap(MawLeft, 0, fftIcon1L, 16, 8, HIGH);
      matrix.drawBitmap(MawRight + 16, 0, fftIcon1, 16, 8, HIGH);
      matrix.drawBitmap(MawRight, 0, fftIcon2, 16, 8, HIGH);
      speakingDelay = millis();
    }
  }
  // speakingDelay is captured in millis() for the UI hold timer; audio sampling uses micros().
  if (speakingActive && average <= Settings::MicrophoneSensitivity && millis() - speakingDelay >= Settings::SpeakingHoldMs) {
    speakingActive = false;
    drawMouth(mawL, maw);
  }
#endif
}

void pauseMicrophone() {
#ifdef Microphone
  microphoneSampleCount = 0;
  microphoneSampling = false;
  speakingActive = false;
  memset(readings, 0, sizeof(readings));
  readIndex = 0;
  total = 0;
  average = 0;
#endif
}

#ifdef Microphone
// Frequency analyzer
void fft() {
  P3_TEENSY_SCOPE(p3Fft, FFT);
  // Sampling is serviced incrementally by microphone(); this only processes a full frame.
  // Remove the frame's DC bias before windowing; replacing two bins afterward
  // cannot remove bias that the window has spread into neighboring bins.
  float mean = 0.0f;
  for (uint16_t i = 0; i < samples; ++i) {
    mean += vReal[i];
  }
  mean /= samples;
  // Apply the cached symmetric window directly; avoid linking the generic
  // window selector and its unused trigonometric window implementations.
  for (uint16_t i = 0; i < samples / 2; ++i) {
    vReal[i] = (vReal[i] - mean) * hammingWindow[i];
    vReal[samples - 1 - i] = (vReal[samples - 1 - i] - mean) * hammingWindow[i];
  }
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();
  renderFFTBars(vReal, samples >> 1);
}

// Render FFT magnitude bins into the fftIcon* mouth buffers.
void renderFFTBars(float *vData, uint16_t bufferSize) {
  memset(fftIcon1, 0, sizeof(fftIcon1));
  memset(fftIcon1L, 0, sizeof(fftIcon1L));
  memset(fftIcon2, 0, sizeof(fftIcon2));
  memset(fftIcon2L, 0, sizeof(fftIcon2L));

  // Only bins 0-31 are rendered; the Nyquist bin (32) is not used.
  const uint16_t fftBinCount = samples >> 1;
  if (bufferSize > fftBinCount) {
    bufferSize = fftBinCount;
  }
  if (bufferSize > 32) {
    bufferSize = 32;
  }

  // Replace DC/very-low-frequency bins with bins 4 and 8 to keep them from
  // dominating the mouth display. Both source indexes are valid for 32 bins.
  if (bufferSize > 8) {
    vData[0] = vData[4];
    vData[1] = vData[8];
  }

  for (uint16_t i = 0; i < bufferSize; i++) {
    // Keep the existing 0..800 amplitude scale, which maps to 1..8 lit rows.
    int data = static_cast<int>(map(static_cast<long>(vData[i]), 0L, 800L, 1L, 8L));
    if (data < 1) {
      data = 1;
    }
    else if (data > 8) {
      data = 8;
    }

    const uint8_t binInGroup = static_cast<uint8_t>(i & 7);
    uint8_t *rightIcon;
    uint8_t *leftIcon;
    uint8_t rightRowStart;
    uint8_t leftRowStart;

    // Four non-overlapping groups: bins 0-7, 8-15, 16-23, and 24-31.
    if (i < 8) {
      rightIcon = fftIcon2;
      leftIcon = fftIcon2L;
      rightRowStart = 14;
      leftRowStart = 15;
    }
    else if (i < 16) {
      rightIcon = fftIcon2;
      leftIcon = fftIcon2L;
      rightRowStart = 15;
      leftRowStart = 14;
    }
    else if (i < 24) {
      rightIcon = fftIcon1;
      leftIcon = fftIcon1L;
      rightRowStart = 15;
      leftRowStart = 14;
    }
    else if (i < 32) {
      rightIcon = fftIcon1;
      leftIcon = fftIcon1L;
      rightRowStart = 14;
      leftRowStart = 15;
    }
    else {
      continue;
    }

    const uint8_t rightBit = 7 - binInGroup;
    const uint8_t leftBit = binInGroup;
    for (uint8_t row = 0; row < data; row++) {
      const uint8_t rowOffset = row * 2;
      // Mirror each bin horizontally between the left and right mouth halves.
      bitWrite(rightIcon[rightRowStart - rowOffset], rightBit, 1);
      bitWrite(leftIcon[leftRowStart - rowOffset], leftBit, 1);
    }
  }
}

#endif

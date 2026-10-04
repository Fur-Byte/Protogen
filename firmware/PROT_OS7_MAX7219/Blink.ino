// Animation state only; FaceRenderer owns all pixels.
void Blink(uint32_t now) {
  if (transitionActive || state == ExpressionVwv) return;
  if (!blinkState && now - blinkTime >= nextBlinkInterval) {
    blinkState = true; Step = 0; blinkSpeed = now;
  }
  if (!blinkState || now - blinkSpeed < Settings::BlinkStepMs) return;
  // A triangular lid closes and opens without accumulating erased pixels.
  static uint8_t phase = 0;
  if (Step == 0) phase = 0;
  ++phase;
  Step = phase <= 8 ? phase : 16 - phase;
  blinkSpeed = now;
  if (phase == 16) { Step = 0; blinkState = false; blinkTime = now; scheduleBlink(); }
}

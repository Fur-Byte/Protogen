// Animation state only; FaceRenderer owns all pixels.
void Blink(uint32_t now) {
  if (transitionActive || state == ExpressionVwv) return;
  if (!blinkState && !blinkRandomPending && now - blinkTime >= nextBlinkInterval) {
    blinkState = true; Step = 0; blinkSpeed = now;
  }
  if (!blinkState) return;
  const uint32_t elapsed = now - blinkSpeed;
  Step = blinkTiming.closure(elapsed);
  if (elapsed < blinkTiming.duration()) return;
  Step = 0; blinkState = false; blinkTime = now;
  if (blinkDoublePending) {
    nextBlinkInterval = blinkTiming.followupMs;
    blinkDoublePending = false;
  } else scheduleBlink();
}

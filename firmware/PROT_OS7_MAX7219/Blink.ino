// Blink animation

void Blink(uint32_t now) {
  if ((now - blinkTime >= blinkDelay) && (state != ExpressionVwv)) { // The blinking animation begins here
    blinkState = true;
    if (now - blinkSpeed >= Settings::BlinkStepMs) {
      if (Step < 8) {
        Step++;
        matrix.fillRect(EyeRight, -8 + Step, 16, 8, LOW);
        matrix.fillRect(EyeLeft, -8 + Step, 16, 8, LOW);
      }
      else {
        Step++;
        drawExpression();
        matrix.fillRect(EyeRight, (Step - 8) * -1, 16, 8, LOW);
        matrix.fillRect(EyeLeft, (Step - 8) * -1, 16, 8, LOW);
      }
      if (Step == 16) {
        Step = 0;
        blinkState = false;
        blinkTime = now;
      }
      blinkSpeed = now;
    }
  }
}

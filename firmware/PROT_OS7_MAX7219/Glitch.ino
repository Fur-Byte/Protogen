// Glitch animation

void glitch(uint32_t now) {
  // Speech owns the mouth. Do not overwrite FFT frames with an angry glitch.
  if (speakingActive) {
    counter = 0;
    glitchDelay = now;
    return;
  }
  if (state == ExpressionAngry) {
    if (now - glitchDelay >= Settings::GlitchIntervalMs) {
      if (now - glitchSpeed >= Settings::GlitchStepMs) {
        counter++;
        if (counter == 1) {
          drawMouth(Glitch1L, Glitch1);
        }
        else if (counter == 2) {
          drawMouth(Glitch2L, Glitch2);
        }
        glitchSpeed = now;
      }
      if (counter == 3) {
        drawMouth(mawL, maw);
        counter = 0;
        glitchDelay = now;
      }
    }
  }
}

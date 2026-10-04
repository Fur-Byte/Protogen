// Speech has exclusive mouth ownership; glitches resume after its quiet interval.
void glitch(uint32_t now) {
  if (speakingActive || transitionActive) { counter = 0; glitchDelay = now; return; }
  if (state != ExpressionAngry) { counter = 0; return; }
  if (now - glitchDelay >= Settings::GlitchIntervalMs && now - glitchSpeed >= Settings::GlitchStepMs) {
    ++counter; glitchSpeed = now;
    if (counter >= 3) { counter = 0; glitchDelay = now; }
  }
}

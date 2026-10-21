// Poll at most once per millisecond. Stable-state debounce needs no interrupts,
// delays, heap allocation, or repeated events while the button is held.
void updateControls(uint32_t now) {
  // lastPoll initialized to now - 1 ensures updateControls immediately services on the first call.
  static uint32_t lastPoll = now - 1;
  static uint32_t buttonChangedAt = now;
  static uint32_t pressedAt = now;
  static bool buttonCandidate = HIGH;
  static bool buttonStable = HIGH;
  static bool holdHandled = false;
  if (now - lastPoll < 1) {
    return;
  }
  lastPoll = now;
  const bool buttonRaw = digitalRead(ButtonPin);
  if (buttonRaw != buttonCandidate) {
    buttonCandidate = buttonRaw;
    buttonChangedAt = now;
  }
  if (buttonCandidate != buttonStable && now - buttonChangedAt >= Settings::ButtonDebounceMs) {
    buttonStable = buttonCandidate;
    if (buttonStable == LOW) {
      pressedAt = now;
      holdHandled = false;
    }
    else if (!holdHandled) {
      if (sleeping) {
        setSleepingLocal(false); // Wake to the same expression; don't also advance it.
      }
      else {
        selectExpression((state + 1) % ExpressionCount, now);
      }
    }
  }
  if (buttonStable == LOW && buttonCandidate == LOW && !holdHandled &&
      now - pressedAt >= Settings::ButtonHoldMs) {
    setSleepingLocal(!sleeping);
    holdHandled = true;
  }

#ifdef VisorSensor
  static bool visorCandidate = visorVisible;
  static uint32_t visorChangedAt = now;
  const bool visorRaw = digitalRead(VisorPin) == LOW;
  if (visorRaw != visorCandidate) {
    visorCandidate = visorRaw;
    visorChangedAt = now;
  }
  if (visorVisible != visorCandidate && now - visorChangedAt >= Settings::VisorDebounceMs) {
    visorVisible = visorCandidate;
  }
#endif
}

void updateBoop(uint32_t now, bool forceSample) {
#ifdef BoopSensor
  static uint32_t lastSample = now - Settings::BoopPollMs;
  if (!forceSample && now - lastSample < Settings::BoopPollMs) {
    return;
  }
  lastSample = now;
  const int reading = readBoopSensor();
  if (reading < 0 || reading > 1023) { boopState=false;boopArmed=false;boopReading=-1;return; }
  boopReading = reading;
  if (reading >= config.boopRelease) {
    boopState = false;
    boopArmed = true;
  }
  else if (reading <= config.boopThreshold) {
    boopState = true;
  }
  // Between the two thresholds, keep the current state to prevent chatter.
#else
  (void)now;
  (void)forceSample;
#endif
}

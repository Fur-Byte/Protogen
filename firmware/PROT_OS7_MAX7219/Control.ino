// Poll at most once per millisecond. Stable-state debounce needs no interrupts,
// delays, heap allocation, or repeated events while the button is held.
void updateControls(uint32_t now) {
  // Let the first call poll immediately.
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
      if (behavior.sleeping) {
        setSleepingLocal(false); // Wake to the same expression; don't also advance it.
      }
      else {
        selectExpression((behavior.expression + 1) % ExpressionCount, now);
      }
    }
  }
  if (buttonStable == LOW && buttonCandidate == LOW && !holdHandled &&
      now - pressedAt >= Settings::ButtonHoldMs) {
    setSleepingLocal(!behavior.sleeping);
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
  boopReading = reading >= 0 && reading <= 1023 ? reading : -1;
  behavior.observeBoop(reading, config);
#else
  (void)now;
  (void)forceSample;
#endif
}

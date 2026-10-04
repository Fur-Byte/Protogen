// Newline-delimited requests, one JSON response per request. Fixed buffers and
// bounded reads/writes let disconnected or slow USB clients leave the face running.
char usbLine[384] = {}, usbReply[1024] = {};
size_t usbLineLength = 0, usbReplyLength = 0, usbReplySent = 0;
bool usbOverflow = false;
uint32_t usbLineStarted = 0;

void replyError(const char *message) {
  snprintf(usbReply, sizeof(usbReply), "{\"ok\":false,\"error\":\"%s\"}\n", message);
}
void replyConfig() {
  size_t used = snprintf(usbReply, sizeof(usbReply), "{\"ok\":true,\"schema\":%u,\"config\":{", configSchemaVersion);
  for (size_t i = 0; i < configFieldCount; ++i)
    used += snprintf(usbReply + used, sizeof(usbReply) - used, "%s\"%s\":%u",
                     i ? "," : "", configFields[i].name, config.*(configFields[i].member));
  snprintf(usbReply + used, sizeof(usbReply) - used, "}}\n");
}
void replyStatus() {
  unsigned features = 0;
#ifdef VisorSensor
  features |= 1;
#endif
#ifdef BoopSensor
  features |= 2;
#endif
#ifdef Microphone
  features |= 4;
  const unsigned envelope = static_cast<unsigned>(smoothedEnvelope);
  const unsigned floor = static_cast<unsigned>(noiseFloor), gate = static_cast<unsigned>(audioGate);
  const uint32_t drops = audioDroppedSamples, frames = audioFrames;
  const bool calibration = calibratingAudio;
  const uint32_t frequency = sensorAdc ? sensorAdc->adc0->getTimerFrequency() : 0;
#else
  const unsigned envelope = 0, floor = 0, gate = 0;
  const uint32_t drops = 0, frames = 0, frequency = 0;
  const bool calibration = false;
#endif
  snprintf(usbReply, sizeof(usbReply),
    "{\"ok\":true,\"firmware\":\"8.1\",\"schema\":%u,\"features\":%u,\"expression\":%u,"
    "\"sleeping\":%s,\"visible\":%s,\"display_ready\":%s,\"boop\":%s,\"boop_reading\":%d,"
    "\"speaking\":%s,\"envelope\":%u,\"noise_floor\":%u,\"audio_gate\":%u,\"calibrating\":%s,"
    "\"audio_drops\":%lu,\"audio_frames\":%lu,\"sample_rate_hz\":%lu,\"spi_hz\":%lu,"
    "\"blink_random_ready\":%s,\"next_blink_ms\":%lu,\"blink_duration_ms\":%lu}\n",
    configSchemaVersion, features, state, sleeping ? "true" : "false",
    visorVisible ? "true" : "false", displayReady ? "true" : "false",
    boopActive ? "true" : "false", boopReading, speakingActive ? "true" : "false",
    envelope, floor, gate, calibration ? "true" : "false",
    static_cast<unsigned long>(drops), static_cast<unsigned long>(frames),
    static_cast<unsigned long>(frequency), static_cast<unsigned long>(Settings::SpiFrequencyHz),
    blinkRandomPending ? "false" : "true", static_cast<unsigned long>(nextBlinkInterval),
    static_cast<unsigned long>(blinkTiming.duration()));
}
void replyFrame() {
  const bool hidden = sleeping || !visorVisible || !displayReady;
  size_t used = snprintf(usbReply, sizeof(usbReply), "{\"ok\":true,\"width\":112,\"height\":8,\"hidden\":%s,\"frame\":\"",
                         hidden ? "true" : "false");
  const char digits[] = "0123456789abcdef";
  for (const uint8_t byte : displayedFrame.bytes) {
    const uint8_t value = hidden ? 0 : byte;
    usbReply[used++] = digits[value >> 4]; usbReply[used++] = digits[value & 15];
  }
  memcpy(usbReply + used, "\"}\n", 4);
}
void applyConfig(const RuntimeConfig &value, uint32_t now) {
  const bool changed = !sameConfig(config, value);
  if (!changed) return;
  if (config.brightness != value.brightness) matrix.setIntensity(static_cast<uint8_t>(value.brightness));
  config = value;
  resetAnimations(now); rising = true;
#ifdef Microphone
  updateAudioCoefficients();
  noiseFloor = config.noiseFloor;
#endif
  // Re-evaluate a sensor under its new calibration without inventing a boop.
  boopArmed = false; boopState = false; boopActive = false;
}
bool parseNumber(const char *text, unsigned long &value) {
  if (!text || !*text) return false;
  value = 0;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9' || value > 6553 || (value == 6553 && *p > '5')) return false;
    value = value * 10 + (*p - '0');
  }
  return true;
}
void handleUsbCommand(char *line, uint32_t now) {
  char *save = nullptr;
  const char *command = strtok_r(line, " \t", &save);
  if (!command) { replyError("empty_command"); return; }
  if (strcmp(command, "SET") == 0) {
    RuntimeConfig candidate = config;
    size_t fields = 0;
    uint32_t seen = 0;
    for (char *token = strtok_r(nullptr, " \t", &save); token; token = strtok_r(nullptr, " \t", &save)) {
      char *equals = strchr(token, '=');
      unsigned long number;
      if (!equals) { replyError("expected_key_value"); return; }
      *equals = '\0';
      if (!parseNumber(equals + 1, number) || !setConfigField(candidate, token, number)) {
        replyError("invalid_setting"); return;
      }
      for (size_t i = 0; i < configFieldCount; ++i) if (strcmp(token, configFields[i].name) == 0) {
        if (seen & (1UL << i)) { replyError("duplicate_setting"); return; }
        seen |= 1UL << i;
      }
      ++fields;
    }
    if (!fields || !validConfig(candidate)) { replyError("invalid_configuration"); return; }
    applyConfig(candidate, now); replyConfig(); return;
  }
  const char *argument = strtok_r(nullptr, " \t", &save);
  unsigned long number;
  if (strcmp(command, "EXPR") == 0 || strcmp(command, "SLEEP") == 0) {
    if (!parseNumber(argument, number) || strtok_r(nullptr, " \t", &save) ||
        number >= (strcmp(command, "EXPR") == 0 ? ExpressionCount : 2)) {
      replyError("invalid_argument"); return;
    }
    if (strcmp(command, "EXPR") == 0) selectExpression(static_cast<uint8_t>(number), now);
    else sleeping = number != 0;
    strcpy(usbReply, "{\"ok\":true}\n"); return;
  }
  if (argument) { replyError("unexpected_argument"); return; }
  if (strcmp(command, "GET") == 0) replyConfig();
  else if (strcmp(command, "STATUS") == 0) replyStatus();
  else if (strcmp(command, "FRAME") == 0) replyFrame();
  else if (strcmp(command, "SAVE") == 0) {
    pauseMicrophone(); // Flash programming can mask interrupts; restart cleanly afterward.
    if (saveStoredConfig(config)) strcpy(usbReply, "{\"ok\":true,\"saved\":true}\n");
    else replyError("save_failed");
  } else if (strcmp(command, "LOAD") == 0) {
    RuntimeConfig saved;
    if (loadStoredConfig(saved)) { applyConfig(saved, now); replyConfig(); }
    else replyError("no_saved_settings");
  } else if (strcmp(command, "DEFAULTS") == 0) { applyConfig(RuntimeConfig{}, now); replyConfig(); }
  else if (strcmp(command, "CALIBRATE") == 0) {
    if (calibrateMicrophone()) strcpy(usbReply, "{\"ok\":true,\"calibrating\":true}\n");
    else replyError("microphone_unavailable_or_paused");
  } else replyError("unknown_command");
}
void serviceSerial(uint32_t now) {
  if (!Serial) {
    usbLineLength = usbReplyLength = usbReplySent = 0; usbOverflow = false; return;
  }
  if (usbReplySent < usbReplyLength) {
    const int capacity = Serial.availableForWrite();
    if (capacity > 0) {
      size_t count = usbReplyLength - usbReplySent;
      if (count > 64) count = 64;
      if (count > static_cast<size_t>(capacity)) count = capacity;
      usbReplySent += Serial.write(reinterpret_cast<const uint8_t *>(usbReply + usbReplySent), count);
    }
    return;
  }
  if (usbLineLength && now - usbLineStarted >= 2000) {
    usbLineLength = 0; usbOverflow = true; // Discard the whole abandoned line through newline.
  }
  for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (usbOverflow) replyError("line_too_long_or_timed_out");
      else { usbLine[usbLineLength] = '\0'; handleUsbCommand(usbLine, now); }
      usbLineLength = 0; usbOverflow = false;
      usbReplyLength = strlen(usbReply); usbReplySent = 0;
      return;
    }
    if (!usbLineLength) usbLineStarted = now;
    if (c == '\0' || usbLineLength + 1 >= sizeof(usbLine)) usbOverflow = true;
    if (!usbOverflow) usbLine[usbLineLength++] = c;
  }
}

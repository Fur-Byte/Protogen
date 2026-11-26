#if PROTOGEN_FLIGHT_RECORDER
extern "C" {
DMAMEM Flight::Record p3TeensyRecords[P3Teensy::Capacity];
}
static_assert(sizeof(p3TeensyRecords) <= 8192, "P3 RAM2 budget");

namespace P3Teensy {
Flight::Timing timing[Flight::StageCount];
Flight::Ring<Capacity> ring(p3TeensyRecords);
Flight::Flood sensorFault, microphoneFault, overrun, uartReject, mainGap, sensorState, microphoneState;
Flight::Rate rate;
uint32_t priorLoop = 0, audioPeak = 0;
uint16_t priorBehavior = 0;
bool started = false;

uint32_t clockUs() { return micros(); }

void emit(uint32_t at, Flight::Event event, uint16_t detail, uint32_t value) {
  P3_TEENSY_SCOPE(cost, Append);
  ring.append(at, event, detail, value);
}

void beginLoop() {
  const uint32_t now = clockUs();
  if (started) {
    const uint32_t gap = now - priorLoop;
    timing[Flight::LoopInterval].observe(gap);
    if (gap > 20000) mainGap.notify(now, 1, displayBlanked ? 1 : 0);
  } else {
    emit(now, Flight::Boot, 0, 0);
    started = true;
  }
  priorLoop = now;
#ifdef Microphone
  const uint16_t backlog = static_cast<uint16_t>(audioWrite - audioRead);
  if (backlog > audioPeak) audioPeak = backlog;
#endif
}

void finishLoop() {
  const uint32_t now = clockUs();
  const uint16_t flags = (behavior.sleeping ? 1 : 0) | (visorVisible ? 2 : 0) |
      (behavior.boopActive ? 4 : 0) | (displayBlanked ? 8 : 0) | (connectorReady ? 16 : 0);
  if (flags != priorBehavior) {
    emit(now, Flight::Behavior, flags, priorBehavior);
    priorBehavior = flags;
  }
  sensorFault.count(now, sensorFailureCount(), sensorLastFailure());
  microphoneFault.count(now, microphoneFailureCount(), microphoneLastFailure());
  // Track both failures and successful transitions. Each stream is admitted
  // independently so a stuck sensor cannot crowd out link or control events.
  sensorState.state(now, sensorRecoveryState());
  microphoneState.state(now, microphoneRecoveryState());
  sensorFault.flush(now, Flight::SensorFault, emit);
  microphoneFault.flush(now, Flight::MicrophoneFault, emit);
  const auto emitState = [](uint32_t at, Flight::Event event, uint16_t detail, uint32_t value) {
    emit(at, event, detail | 0x8000, value);
  };
  sensorState.flush(now, Flight::SensorFault, emitState);
  microphoneState.flush(now, Flight::MicrophoneFault, emitState);

#ifdef Microphone
  overrun.count(now, audioDrops.load(std::memory_order_relaxed));
#endif
  uartReject.count(now, pawRejectedPackets());
  overrun.flush(now, Flight::AudioDrops, emit);
  uartReject.flush(now, Flight::UartReject, emit);
  mainGap.flush(now, Flight::MainGap, emit);
  rate.sample(now, ring.next());
}

uint32_t mergedCount() {
  uint32_t total = 0;
  const Flight::Flood *sources[] = {&sensorFault, &microphoneFault, &overrun, &uartReject,
                                    &mainGap, &sensorState, &microphoneState};
  for (const auto *source : sources)
    Flight::add(total, source->merged);
  return total;
}

bool cursor(const char *text, uint32_t &value) {
  if (!text || !*text) return false;
  value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const uint32_t digit = *text - '0';
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  return true;
}
}
#endif

// Newline-delimited requests, one JSON response per request. Fixed buffers and
// bounded reads/writes let disconnected or slow USB clients leave the face running.
char usbLine[384] = {}, usbReply[1024] = {};
size_t usbLineLength = 0, usbReplyLength = 0, usbReplySent = 0;
bool usbOverflow = false;
uint32_t usbLineStarted = 0;

#include "UsbReply.h"
// Always replace partial JSON with a complete, fixed error in the shared buffer.
void replyTooLarge() { strcpy(usbReply, "{\"ok\":false,\"error\":\"response_too_large\"}\n"); }
void replyError(const char *message) {
  BoundedReply out(usbReply, sizeof(usbReply));
  out.append("{\"ok\":false,\"error\":");
  out.jsonString(message); out.append("}\n");
  if (!out.okay()) replyTooLarge();
}
bool writeConfigReply(char *buffer, size_t capacity, const ConfigField *fields, size_t count) {
  BoundedReply out(buffer, capacity);
  out.append("{\"ok\":true,\"schema\":%u,\"config\":{", configSchemaVersion);
  for (size_t i = 0; i < count && out.okay(); ++i) {
    out.append("%s", i ? "," : ""); out.jsonString(fields[i].name);
    out.append(":%u", config.*(fields[i].member));
  }
  out.append("},\"brightness\":{\"configured\":%u,\"effective\":%u,\"maximum\":%u,\"approved\":%u}}\n",
             config.brightness, matrix.getIntensity(), Settings::MaxBrightness, config.brightnessApproval);
  return out.okay();
}
void replyConfig() {
  if (!writeConfigReply(usbReply, sizeof(usbReply), configFields, configFieldCount)) replyTooLarge();
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
#ifdef WirelessPaw
  features |= 8;
#endif
#ifdef VisorConnector
  features |= 16;
#endif
  BoundedReply out(usbReply, sizeof(usbReply));
  out.append(
    "{\"ok\":true,\"firmware\":\"8.2\",\"schema\":%u,\"features\":%u,\"expression\":%u,"
    "\"sleeping\":%s,\"visible\":%s,\"display_ready\":%s,\"boop\":%s,\"boop_reading\":%d,"
    "\"speaking\":%s,\"envelope\":%u,\"noise_floor\":%u,\"audio_gate\":%u,\"calibrating\":%s,"
    "\"audio_drops\":%lu,\"audio_frames\":%lu,\"sample_rate_hz\":%lu,\"spi_hz\":%lu,"
    "\"blink_random_ready\":%s,\"blink_entropy_fallbacks\":%lu,\"next_blink_ms\":%lu,\"blink_duration_ms\":%lu,"
    "\"paw_connected\":%s,\"paw_rejected\":%lu,\"microphone_enabled\":%s,"
    "\"sensor_faults\":%u,\"connector_ready\":%s,\"boop_mode\":%u,"
    "\"brightness\":{\"configured\":%u,\"effective\":%u,\"maximum\":%u,\"approved\":%u}}\n",
    configSchemaVersion, features, behavior.expression, behavior.sleeping ? "true" : "false",
    visorVisible ? "true" : "false", displayReady ? "true" : "false",
    behavior.boopActive ? "true" : "false", boopReading, speakingActive ? "true" : "false",
    envelope, floor, gate, calibration ? "true" : "false",
    static_cast<unsigned long>(drops), static_cast<unsigned long>(frames),
    static_cast<unsigned long>(frequency), static_cast<unsigned long>(Settings::SpiFrequencyHz),
    behavior.blinkRandomPending ? "false" : "true", static_cast<unsigned long>(blinkFallbacks),static_cast<unsigned long>(behavior.nextBlinkInterval),
    static_cast<unsigned long>(behavior.blinkTiming.duration()),pawIsOnline(millis())?"true":"false",
    static_cast<unsigned long>(pawRejectedPackets()),microphoneEnabled?"true":"false",
    sensorFaults,connectorReady?"true":"false",config.boopMode,
    config.brightness,matrix.getIntensity(),Settings::MaxBrightness,config.brightnessApproval);
  if (!out.okay()) replyTooLarge();
}
void replyFrame() {
  const bool hidden = behavior.sleeping || !visorVisible || !displayReady || !connectorReady;
  BoundedReply out(usbReply, sizeof(usbReply));
  out.append("{\"ok\":true,\"width\":%u,\"height\":%u,\"hidden\":%s,\"frame\":\"",
             static_cast<unsigned>(Face::Width), static_cast<unsigned>(Face::Height), hidden ? "true" : "false");
  for (const uint8_t byte : displayedFrame.bytes) {
    if (!out.append("%02x", hidden ? 0 : byte)) break;
  }
  out.append("\"}\n");
  if (!out.okay()) replyTooLarge();
}
void applyConfig(const RuntimeConfig &value, uint32_t now) {
  cancelBrightnessBoost();
  const bool changed = !sameConfig(config, value);
  if (!changed) return;
  if (config.brightness != value.brightness || config.brightnessApproval != value.brightnessApproval) matrix.setIntensity(value.effectiveBrightness());
  config = value;
  resetAnimations(now); behavior.rising = true;
#ifdef Microphone
  updateAudioCoefficients();
  noiseFloor = config.noiseFloor;
#endif
  // Re-evaluate a sensor under its new calibration without inventing a boop.
  behavior.boopArmed = false; behavior.boopState = false; behavior.boopActive = false; behavior.boopFromRemote = false;
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
  if (strcmp(command, "BOOST") == 0) {
    unsigned long intensity, duration;
    const char *level = strtok_r(nullptr, " \t", &save);
    const char *length = strtok_r(nullptr, " \t", &save);
    if (!parseNumber(level, intensity) || !parseNumber(length, duration) ||
        strtok_r(nullptr, " \t", &save) || intensity > 15 || !duration || duration > 30000) {
      replyError("invalid_argument"); return;
    }
    brightnessBoostAt = now; brightnessBoostMs = duration; brightnessBoostActive = true;
    matrix.setIntensity(Settings::effectiveBrightness(intensity));
    BoundedReply out(usbReply, sizeof(usbReply));
    out.append("{\"ok\":true,\"temporary\":true,\"brightness\":{\"configured\":%u,\"requested\":%lu,\"effective\":%u,\"maximum\":%u,\"approved\":%u}}\n",
               config.brightness, intensity, matrix.getIntensity(), Settings::MaxBrightness, config.brightnessApproval);
    if (!out.okay()) replyTooLarge();
    return;
  }
  if (strcmp(command, "SET") == 0) {
    RuntimeConfig candidate = config;
    size_t fields = 0;
    ConfigFieldSeen<configFieldCount> seen;
    for (char *token = strtok_r(nullptr, " \t", &save); token; token = strtok_r(nullptr, " \t", &save)) {
      char *equals = strchr(token, '=');
      unsigned long number;
      if (!equals) { replyError("expected_key_value"); return; }
      *equals = '\0';
      size_t fieldIndex;
      if (!parseNumber(equals + 1, number) || !setConfigField(candidate, token, number, &fieldIndex)) {
        replyError("invalid_setting"); return;
      }
      if (!seen.mark(fieldIndex)) { replyError("duplicate_setting"); return; }
      ++fields;
    }
    if (!fields || !validConfig(candidate)) { replyError("invalid_configuration"); return; }
    applyConfig(candidate, now); replyConfig(); return;
  }
#if PROTOGEN_FLIGHT_RECORDER
  if (strcmp(command, "RECINFO") == 0) {
    if (strtok_r(nullptr, " \t", &save)) { replyError("invalid_argument"); return; }
    using namespace P3Teensy;
    BoundedReply out(usbReply, sizeof(usbReply));
    out.append("{\"ok\":true,\"v\":%u,\"oldest\":%lu,\"next\":%lu,"
               "\"count\":%u,\"capacity\":%u,\"overwritten\":%lu,\"audio_peak_at_sample\":%lu,"
               "\"merged\":%lu,\"peak_events\":%lu,\"peak_window_us\":%lu}\n",
               unsigned(Flight::Version), static_cast<unsigned long>(ring.oldest()),
               static_cast<unsigned long>(ring.next()), unsigned(ring.size()), Capacity,
               static_cast<unsigned long>(ring.lost()), static_cast<unsigned long>(audioPeak),
               static_cast<unsigned long>(mergedCount()), static_cast<unsigned long>(rate.peakEvents),
               static_cast<unsigned long>(rate.peakWindowUs));
    if (!out.okay()) replyTooLarge();
    return;
  }
  else if (strcmp(command, "REC") == 0 || strcmp(command, "RECSTAT") == 0) {
    using namespace P3Teensy;
    uint32_t requested = 0;
    if (!cursor(strtok_r(nullptr, " \t", &save), requested) || strtok_r(nullptr, " \t", &save)) {
      replyError("invalid_argument"); return;
    }
    BoundedReply out(usbReply, sizeof(usbReply));
    if (strcmp(command, "RECSTAT") == 0) {
      if (requested >= Flight::StageCount) { replyError("invalid_argument"); return; }
      const auto &stat = timing[requested];
      out.append("{\"ok\":true,\"v\":1,\"stage\":%lu,\"count\":%lu,\"total_us\":%llu,\"max_us\":%lu,\"bins\":[",
                 static_cast<unsigned long>(requested), static_cast<unsigned long>(stat.count),
                 static_cast<unsigned long long>(stat.totalUs), static_cast<unsigned long>(stat.maxUs));
      for (unsigned i = 0; i < 6; ++i)
        out.append("%s%lu", i ? "," : "", static_cast<unsigned long>(stat.buckets[i]));
      out.append("]}\n");
    } else {
      Flight::Record entry{};
      const bool found = ring.read(requested, entry);
      out.append("{\"ok\":%s,\"v\":1,\"cursor\":%lu,\"oldest\":%lu,\"next\":%lu,\"overwritten\":%lu",
                 found ? "true" : "false", static_cast<unsigned long>(requested),
                 static_cast<unsigned long>(ring.oldest()), static_cast<unsigned long>(ring.next()),
                 static_cast<unsigned long>(ring.lost()));
      if (found)
        out.append(",\"record\":[%lu,%lu,%u,%u,%lu]", static_cast<unsigned long>(entry.sequence),
                   static_cast<unsigned long>(entry.timeUs), unsigned(entry.event), unsigned(entry.detail),
                   static_cast<unsigned long>(entry.value));
      else out.append(",\"error\":\"cursor_unavailable\"");
      out.append("}\n");
    }
    if (!out.okay()) replyTooLarge();
    return;
  }
#endif
  const char *argument = strtok_r(nullptr, " \t", &save);
  unsigned long number;
  if (strcmp(command, "EXPR") == 0 || strcmp(command, "SLEEP") == 0 || strcmp(command,"MIC")==0) {
    if (!parseNumber(argument, number) || strtok_r(nullptr, " \t", &save) ||
        number >= (strcmp(command, "EXPR") == 0 ? ExpressionCount : 2)) {
      replyError("invalid_argument"); return;
    }
    if (strcmp(command, "EXPR") == 0)
      selectExpressionCommand(static_cast<uint8_t>(number),now,ResponseTiming::Usb,++usbCommandSequence,micros());
    else if(strcmp(command,"SLEEP")==0) setSleepingCommand(number!=0,ResponseTiming::Usb,++usbCommandSequence,micros());
    else {
#ifdef Microphone
      microphoneEnabled=number!=0;
      if(!microphoneEnabled)pauseMicrophone();
#else
      replyError("microphone_unavailable_or_paused");return;
#endif
    }
    strcpy(usbReply, "{\"ok\":true}\n"); return;
  }
  if (argument) { replyError("unexpected_argument"); return; }
  if (strcmp(command, "LATENCY") == 0) {
    BoundedReply out(usbReply,sizeof(usbReply));
    static const char responseFormat[] PROGMEM = "{\"ok\":true,\"segment\":\"teensy_dispatch_to_causal_spi\",\"physical_clock\":false,"
      "\"accepted\":%lu,\"completed\":%lu,\"skipped\":%lu,\"superseded\":%lu,\"expired\":%lu,\"delayed\":%lu,"
      "\"pending\":%s,\"observation\":%lu,\"sequence\":%lu,\"origin\":%u,\"action\":%u,\"target\":%u,\"status\":%u,"
      "\"feedback_valid\":%s,\"onset_valid\":%s,\"target_already_visible\":%s,\"feedback_us\":%lu,\"target_onset_us\":%lu,\"completion_us\":%lu,"
      "\"max_feedback_us\":%lu,\"max_target_onset_us\":%lu,\"max_completion_us\":%lu,"
      "\"skipped_sequence\":%lu,\"skipped_origin\":%u,\"skipped_observation\":%lu,"
      "\"superseded_sequence\":%lu,\"superseded_origin\":%u,\"superseded_observation\":%lu,\"early_reveal\":%s}\n";
    out.append(responseFormat,
      static_cast<unsigned long>(responseTiming.accepted),static_cast<unsigned long>(responseTiming.completed),
      static_cast<unsigned long>(responseTiming.skipped),static_cast<unsigned long>(responseTiming.superseded),
      static_cast<unsigned long>(responseTiming.expired),static_cast<unsigned long>(responseTiming.delayed),
      responseTiming.pending?"true":"false",static_cast<unsigned long>(responseTiming.observation),static_cast<unsigned long>(responseTiming.sequence),
      responseTiming.origin,responseTiming.action,responseTiming.target,responseTiming.status,
      responseTiming.feedback?"true":"false",responseTiming.onset?"true":"false",responseTiming.targetAlreadyVisible?"true":"false",
      static_cast<unsigned long>(responseTiming.feedbackUs),static_cast<unsigned long>(responseTiming.onsetUs),
      static_cast<unsigned long>(responseTiming.completionUs),static_cast<unsigned long>(responseTiming.maxFeedbackUs),
      static_cast<unsigned long>(responseTiming.maxOnsetUs),static_cast<unsigned long>(responseTiming.maxCompletionUs),
      static_cast<unsigned long>(responseTiming.skippedSequence),responseTiming.skippedOrigin,
      static_cast<unsigned long>(responseTiming.skippedObservation),
      static_cast<unsigned long>(responseTiming.supersededSequence),responseTiming.supersededOrigin,
      static_cast<unsigned long>(responseTiming.supersededObservation),
      Settings::EarlyExpressionReveal?"true":"false");
    if(!out.okay())replyTooLarge();
  }
  else if (strcmp(command, "IDLE") == 0) {
    BoundedReply out(usbReply, sizeof(usbReply));
    static const char responseFormat[] PROGMEM = "{\"ok\":true,\"attempts\":%lu,\"waits\":%lu,\"declined\":%lu,"
      "\"measurement\":\"wfi_call_elapsed\",\"includes_isr\":true,"
      "\"last_guard\":%u,\"over_budget_waits\":%lu,\"total_us\":%llu,\"max_us\":%lu}\n";
    out.append(responseFormat,
      static_cast<unsigned long>(controllerIdle.attempts),static_cast<unsigned long>(controllerIdle.waits),
      static_cast<unsigned long>(controllerIdle.declined),static_cast<unsigned>(controllerIdle.lastGuard),
      static_cast<unsigned long>(controllerIdle.overBudgetWaits),static_cast<unsigned long long>(controllerIdle.totalWaitUs),
      static_cast<unsigned long>(controllerIdle.maxWaitUs));
    if (!out.okay()) replyTooLarge();
  }
  else if (strcmp(command, "SENSORS") == 0) {
    BoundedReply out(usbReply, sizeof(usbReply));
    out.append("{\"ok\":true,\"faults\":%u,"
      "\"adc\":{\"state\":%u,\"reason\":%u,\"failures\":%lu,\"retries\":%lu,\"recoveries\":%lu},"
      "\"microphone\":{\"state\":%u,\"reason\":%u,\"failures\":%lu,\"retries\":%lu,\"recoveries\":%lu}}\n",
      sensorFaults,sensorRecoveryState(),sensorLastFailure(),
      static_cast<unsigned long>(sensorFailureCount()),static_cast<unsigned long>(sensorRetryCount()),
      static_cast<unsigned long>(sensorRecoveryCount()),microphoneRecoveryState(),microphoneLastFailure(),
      static_cast<unsigned long>(microphoneFailureCount()),static_cast<unsigned long>(microphoneRetryCount()),
      static_cast<unsigned long>(microphoneRecoveryCount()));
    if (!out.okay()) replyTooLarge();
  }
#if PROTOGEN_SUPERVISION_OBSERVE
  else if (strcmp(command, "SUPERVISION") == 0) {
    BoundedReply out(usbReply, sizeof(usbReply));
    out.append("{\"ok\":true,\"maximum_save_ms\":%lu,\"long_saves\":%lu",
               static_cast<unsigned long>(supervision.maxSaveMs),
               static_cast<unsigned long>(supervision.longSaves));
    const char *names[] = {"foreground", "render", "audio"};
    const Supervision::Progress *stages[] = {&supervision.foreground, &supervision.render, &supervision.audio};
    for (unsigned i = 0; i < 3; ++i) {
      const auto &stage = *stages[i];
      out.append(",\"%s\":{\"required\":%s,\"stale\":%s,\"late\":%lu,\"max_gap_ms\":%lu}",
                 names[i], stage.required ? "true" : "false", stage.stale ? "true" : "false",
                 static_cast<unsigned long>(stage.lateEpisodes), static_cast<unsigned long>(stage.maxGap));
    }
    out.append("}\n");
    if (!out.okay()) replyTooLarge();
  }
#endif
#if PROTOGEN_CONTROLLER_RECOVERY
  else if (strcmp(command, "RECOVERY") == 0) {
    const auto &r = controllerRecovery;
    BoundedReply out(usbReply, sizeof(usbReply));
    out.append("{\"ok\":true,\"armed\":%s,\"locked\":%s,\"cause\":%lu,\"attempts\":%lu,"
               "\"previous_valid\":%s,\"previous_stage\":%lu,\"previous_uptime_ms\":%lu,"
               "\"previous_passes\":%lu,\"previous_renders\":%lu,\"previous_drained\":%lu}\n",
               r.armed ? "true" : "false", r.locked ? "true" : "false",
               static_cast<unsigned long>(r.cause), static_cast<unsigned long>(r.attempts),
               r.previousValid ? "true" : "false", static_cast<unsigned long>(r.previous.stage),
               static_cast<unsigned long>(r.previous.uptime), static_cast<unsigned long>(r.previous.passes),
               static_cast<unsigned long>(r.previous.renders), static_cast<unsigned long>(r.previous.drained));
    if (!out.okay()) replyTooLarge();
  }
#endif
  else if (strcmp(command, "GET") == 0) replyConfig();
  else if (strcmp(command, "STATUS") == 0) replyStatus();
  else if (strcmp(command, "PERF") == 0) {
    BoundedReply out(usbReply, sizeof(usbReply));
    out.append(
      "{\"ok\":true,\"efficiency_flags\":%u,\"render_attempts\":%lu,\"compositions\":%lu,"
      "\"cache_skips\":%lu,\"changed_frames\":%lu,\"display_wakes\":%lu,"
      "\"render_total_us\":%llu,\"render_max_us\":%lu,\"config_write_attempts\":%lu,"
      "\"config_verified_writes\":%lu,\"config_write_failures\":%lu,\"idle_waits\":%lu,\"idle_total_us\":%llu,"
      "\"idle_max_us\":%lu}\n",
      Efficiency::Flags, static_cast<unsigned long>(renderMetrics.attempts),
      static_cast<unsigned long>(renderMetrics.compositions), static_cast<unsigned long>(renderMetrics.skipped),
      static_cast<unsigned long>(renderMetrics.changed), static_cast<unsigned long>(renderMetrics.wakes),
      static_cast<unsigned long long>(renderMetrics.totalUs), static_cast<unsigned long>(renderMetrics.maxUs),
      static_cast<unsigned long>(ConfigStore::writeAttempts), static_cast<unsigned long>(ConfigStore::verifiedWrites),
      static_cast<unsigned long>(ConfigStore::writeFailures),
      static_cast<unsigned long>(controllerIdle.waits),
      static_cast<unsigned long long>(controllerIdle.totalWaitUs),
      static_cast<unsigned long>(controllerIdle.maxWaitUs));
    if (!out.okay()) replyTooLarge();
  }
  else if (strcmp(command, "FRAME") == 0) replyFrame();
  else if (strcmp(command, "SAVE") == 0) {
#if PROTOGEN_CONTROLLER_RECOVERY
    controllerRecovery.saving(millis(), supervisionRenderEpoch, supervision.audio.marker);
#endif
    pauseMicrophone(); // Flash programming can mask interrupts; restart cleanly afterward.
#if PROTOGEN_SUPERVISION_OBSERVE
    const uint32_t started = millis();
    const bool saved = saveStoredConfig(config);
    supervision.storage(millis() - started);
    if (saved) {
#else
    if (saveStoredConfig(config)) {
#endif
      BoundedReply out(usbReply, sizeof(usbReply));
      out.append("{\"ok\":true,\"saved\":true,\"record_format\":%u,"
                 "\"brightness\":{\"configured\":%u,\"effective\":%u,\"maximum\":%u,\"approved\":%u}}\n",
                 ConfigStore::RecordVersion,config.brightness,matrix.getIntensity(),
                 Settings::MaxBrightness,config.effectiveBrightness());
      if(!out.okay())replyTooLarge();
    }
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

bool usbHasPendingWork() {
  return Serial && (usbReplySent < usbReplyLength
      ? Serial.availableForWrite() > 0 : Serial.available() > 0);
}

namespace AudioRecovery {
#ifdef Microphone
bool failed=false, permanent=false, recovering=false;
uint8_t lastFailure=0;
uint32_t faultAt=0, retryDelay=0, failures=0, retries=0, recoveries=0;
volatile uint16_t healthyRun=0;
void fail(uint32_t now,uint8_t reason,bool isPermanent=false) {
  pauseMicrophone();
  if(sensorAdc){sensorAdc->adc0->stopContinuous();sensorAdc->adc0->resetError();}
  failed=true;permanent=isPermanent;recovering=true;lastFailure=reason;++failures;
  sensorFaults|=4;faultAt=now;
  retryDelay=retryDelay ? (retryDelay < Settings::SensorRetryMaxMs / 2 ?
    retryDelay * 2 : Settings::SensorRetryMaxMs) : Settings::SensorRetryInitialMs;
}
#endif
}
uint32_t microphoneFailureCount() {
#ifdef Microphone
  return AudioRecovery::failures;
#else
  return 0;
#endif
}
uint32_t microphoneRetryCount() {
#ifdef Microphone
  return AudioRecovery::retries;
#else
  return 0;
#endif
}
uint32_t microphoneRecoveryCount() {
#ifdef Microphone
  return AudioRecovery::recoveries;
#else
  return 0;
#endif
}
uint8_t microphoneRecoveryState() {
#ifdef Microphone
  return AudioRecovery::permanent ? 2 : AudioRecovery::failed ? 1 : AudioRecovery::recovering ? 3 : 0;
#else
  return 0;
#endif
}
uint8_t microphoneLastFailure() {
#ifdef Microphone
  return AudioRecovery::lastFailure;
#else
  return 0;
#endif
}
#ifdef Microphone
// QuadTimer4 triggers ADC0 at 4 kHz. The completion ISR only queues samples;
// floating-point DSP, graphics, and USB are always in the main loop.
void microphoneSampleISR() {
  const int sample = sensorAdc->adc0->readSingle(); // Acknowledge every conversion.
  if(sample<=2 || sample>=1021)AudioRecovery::healthyRun=0;
  else if(AudioRecovery::healthyRun<256)++AudioRecovery::healthyRun;
  if(sample<=2 || sample>=1021) {if(audioRailRun<256)++audioRailRun;}
  else audioRailRun=0;
  if (audioPriming) audioPriming = false; // Discard the software setup conversion.
  else if (microphoneSampling) {
    const uint16_t write = audioWrite;
    if (sample < 0 || sample > 1023 || static_cast<uint16_t>(write - audioRead) >= audioBufferSize) {
      ++audioDroppedSamples;
    } else {
      audioBuffer[write & (audioBufferSize - 1)] = static_cast<uint16_t>(sample);
      audioWrite = static_cast<uint16_t>(write + 1);
    }
  }
#if defined(__IMXRT1062__)
  asm volatile("dsb");
#endif
}

void updateAudioCoefficients() {
  attackCoefficient = 1.0f / (samplingFrequencyHz * config.attackMs / 1000.0f);
  releaseCoefficient = 1.0f / (samplingFrequencyHz * config.releaseMs / 1000.0f);
}

void startMicrophone() {
  if(microphoneSampling)return;
  if(!sensorAdc){AudioRecovery::fail(millis(),1,true);return;}
  if(!sensorAdc->adc0->checkPin(micPin)){AudioRecovery::fail(millis(),5,true);return;}
  audioRead = audioWrite = 0; microphoneSampleCount = 0; frameActivity = false;
  audioBiasReady = false; handledAudioDrops = audioDroppedSamples;
  audioPriming = true; AudioRecovery::healthyRun=0;
  audioRailRun=0;lastAudioWrite=audioWrite;audioProgressAt=millis();
  if(!sensorAdc->adc0->startSingleRead(micPin)){AudioRecovery::fail(millis(),4);return;}
  microphoneSampling=true;
  sensorAdc->adc0->enableInterrupts(microphoneSampleISR, 64);
  sensorAdc->adc0->startTimer(samplingFrequencyHz);
}

void processMicrophoneSample(float sample, uint32_t now) {
  if (!audioBiasReady) { filterDCComponent = sample; audioBiasReady = true; }
  filterDCComponent = sample * 0.01f + filterDCComponent * 0.99f;
  const int amplitude = static_cast<int>(fabsf(sample - filterDCComponent));
  total -= readings[readIndex]; readings[readIndex] = amplitude; total += amplitude;
  if (++readIndex >= numReadings) readIndex = 0;
  average = total / numReadings;
  const float coefficient = average > smoothedEnvelope ? attackCoefficient : releaseCoefficient;
  smoothedEnvelope += (average - smoothedEnvelope) * coefficient;
  const float adaptiveGate = noiseFloor * 2.0f + config.noiseMargin;
  audioGate = adaptiveGate > config.sensitivity ? adaptiveGate : config.sensitivity;
  // Latch every threshold crossing, including bursts early in the FFT frame.
  if (!calibratingAudio && average > audioGate) frameActivity = true;
  if (calibratingAudio) {
    calibrationTotal += average;
    if (++calibrationSamples >= samplingFrequencyHz / 2) {
      noiseFloor = calibrationTotal / calibrationSamples;
      config.noiseFloor = static_cast<uint16_t>(noiseFloor);
      calibratingAudio = false; frameActivity = false;
    }
  }
  vReal[microphoneSampleCount] = sample; vImag[microphoneSampleCount] = 0;
  if (++microphoneSampleCount == samples) {
    microphoneSampleCount = 0; ++audioFrames;
    if (frameActivity) {
      if (config.mouthMode == 0) fft();
      speakingActive = true; speakingDelay = now;
    } else if (!speakingActive && !calibratingAudio) {
      // Learn the noise floor only below the activity gate, never during speech.
      noiseFloor += (average - noiseFloor) * 0.01f;
    }
    frameActivity = false;
  }
}
#endif

void microphone() {
#ifdef Microphone
  const uint32_t now = millis();
  if(AudioRecovery::permanent)return;
  if(AudioRecovery::failed) {
    if(now-AudioRecovery::faultAt<AudioRecovery::retryDelay)return;
    AudioRecovery::failed=false;++AudioRecovery::retries;
  }
  startMicrophone();
  if(!microphoneSampling)return;
  if(audioWrite!=lastAudioWrite){lastAudioWrite=audioWrite;audioProgressAt=now;}
  if(audioRailRun>=256 || now-audioProgressAt>=100) {
    // Keep the user's enable preference; a failed microphone provides no speech.
    AudioRecovery::fail(now,audioRailRun>=256 ? 3 : 2);return;
  }
  if(AudioRecovery::recovering) {
    // Require 64 ms of consecutive off-rail samples before clearing the fault.
    // Discard all probe samples rather than interpreting them as speech.
    if(AudioRecovery::healthyRun<256)return;
    noInterrupts();audioRead=audioWrite;interrupts();
    AudioRecovery::recovering=false;AudioRecovery::retryDelay=0;++AudioRecovery::recoveries;
    sensorFaults&=static_cast<uint16_t>(~4u);
  }
  if (audioDroppedSamples != handledAudioDrops) {
    // Queued data straddles a gap: discard it rather than FFT unevenly spaced samples.
    noInterrupts(); audioRead = audioWrite; handledAudioDrops = audioDroppedSamples; interrupts();
    microphoneSampleCount = 0; frameActivity = false; audioBiasReady = false;
    memset(readings, 0, sizeof(readings)); readIndex = total = average = 0;
    if (calibratingAudio) { calibrationSamples = 0; calibrationTotal = 0; }
  }
  // Bounded work (32 ms of queued audio) keeps controls responsive even after a stall.
  for (uint16_t budget = 0; budget < samples * 2 && audioRead != audioWrite; ++budget) {
    const uint16_t read = audioRead;
    const uint16_t sample = audioBuffer[read & (audioBufferSize - 1)];
    audioRead = static_cast<uint16_t>(read + 1);
    processMicrophoneSample(sample, now);
  }
  if (speakingActive && now - speakingDelay >= Settings::SpeakingHoldMs && average <= audioGate) {
    speakingActive = false;
  }
  float level = (smoothedEnvelope - audioGate) / (audioGate * 2.0f + 1.0f);
  if (level < 0) level = 0;
  if (level > 1) level = 1;
  mouthAperture = speakingActive ? static_cast<uint8_t>(1 + level * 2) : 0;
#endif
}

void pauseMicrophone() {
#ifdef Microphone
  if (microphoneSampling) {
    microphoneSampling = false;
    sensorAdc->adc0->stopTimer(); sensorAdc->adc0->disableInterrupts();
  }
  audioRead = audioWrite = 0; microphoneSampleCount = 0;
  frameActivity = false; audioBiasReady = false; speakingActive = false;
  memset(readings, 0, sizeof(readings)); readIndex = total = average = 0;
  smoothedEnvelope = 0; mouthAperture = 0;
  calibratingAudio = false; calibrationSamples = 0; calibrationTotal = 0;
#endif
}

bool calibrateMicrophone() {
#ifdef Microphone
  if (sleeping || !visorVisible || !connectorReady || !microphoneEnabled || boopActive || !sensorAdc || (sensorFaults&4)) return false;
  pauseMicrophone(); calibratingAudio = true;
  calibrationTotal = 0; calibrationSamples = 0;
  return true;
#else
  return false;
#endif
}

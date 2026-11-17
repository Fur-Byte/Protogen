#include "sketch.h"
#if PROTOGEN_ANIMATION_TIMELINES
#include "AudioTestTimeline.h"
#endif

static void run(unsigned ms) {
  while (ms--) { advanceTime(1000); loop(); }
}

#ifdef Microphone
// Calculate expected mouth pixels independently for both 32-column halves.
static void expectAperture(unsigned aperture) {
  for (const int start : {16, 64}) {
    for (int x = 0; x < 32; ++x) {
      const int opening = x >= 5 && x < 27 ? int(aperture) : int(aperture / 2);
      for (int y = 0; y < 8; ++y) {
        if (Face::pixel(displayedFrame, start + x, y) != (y == 3 - opening || y == 4 + opening)) {
          std::cerr << "first divergent mouth pixel at (" << start + x << ',' << y
                    << "), ms " << millis() << ", aperture " << aperture << '\n';
          std::abort();
        }
      }
    }
  }
}

#if PROTOGEN_AUDIO_V2
static void showMouth(const char *label) {
  std::cout << label << " at " << millis() << " modeled ms\n";
  for (int y = 0; y < 8; ++y) {
    for (int x = 16; x < 48; ++x) std::cout << (Face::pixel(displayedFrame, x, y) ? '#' : '.');
    std::cout << '\n';
  }
}
#endif
#endif

int main() {
  setup();
  config.idleMotion = config.naturalBlinks = 0;
  config.mouthMode = 1;
  run(160);
#ifndef Microphone
  assert(!microphoneEnabled && !speakingActive && !calibrateMicrophone());
  assert(microphoneRecoveryState() == 0);
  Serial.input = "MIC 1\n";
  run(10);
  assert(Serial.output.find("microphone_unavailable_or_paused") != std::string::npos);
#else
  assert(microphoneSampling && !speakingActive);
#if PROTOGEN_ANIMATION_TIMELINES
  assert(behavior.startTimeline(ProtogenAsset::clip, sizeof(ProtogenAsset::clip), millis()));
  run(48);
  assert(displayedPose.mouthOverride == 3);
  expectAperture(3);
#endif
  Serial.input = "STATUS\n";
  syntheticMicrophone = true;
  syntheticAmplitude = 200;
  run(260);
  assert(Serial.output.find("\"ok\":true") != std::string::npos);
  assert(speakingActive && audioDroppedSamples == 0 && displayedPose.speaking);
  expectAperture(displayedPose.aperture);
#if PROTOGEN_ANIMATION_TIMELINES
  assert(displayedPose.mouthOverride == -1 && displayedPose.aperture < 3);
#endif
  const auto stableWrites = SPI.transactions;
  run(80);
  assert(SPI.transactions == stableWrites);
  syntheticMicrophone = false;
  run(240);
  assert(!speakingActive);
#if PROTOGEN_ANIMATION_TIMELINES
  assert(displayedPose.mouthOverride == 3);
  expectAperture(3);
#endif

#if PROTOGEN_AUDIO_V2
  assert(calibrateMicrophone());
  micInput = 530;
  run(520);
  assert(!calibratingAudio && !speakingActive && config.noiseFloor == 0);
  assert(microphoneSampleCount < AudioV2::WindowSamples);
  syntheticMicrophone = true;
  syntheticAmplitude = 380;
  run(240);
  assert(speakingActive && displayedPose.aperture == 3);
  showMouth("Wide speech");

  // Conversions continue during a modeled foreground stall. The previous
  // hold and partial FFT window must be discarded when the real loop resumes.
  advanceTime(200000);
  assert(audioDroppedSamples > 0);
  loop();
  assert(!speakingActive && mouthAperture == 0 && microphoneSampleCount == 0);
  assert(static_cast<uint16_t>(audioWrite - audioRead) <= 4);
#if PROTOGEN_FLIGHT_RECORDER
  assert(P3Teensy::audioDrops.load() == audioDroppedSamples);
#endif
  run(16);
  showMouth("Following backlog discard");
  run(112);
  assert(speakingActive);
  showMouth("Recovered speech");

  // Legitimate FFT windows remain local to the renderer. A later window
  // must not mutate the copied spectrum previously used for composition.
  config.mouthMode = 0;
  run(64);
  Face::AudioSnapshot first{};
  captureSpectrum(first, true, 0);
  const auto saved = first;
  syntheticAmplitude = 260;
  run(64);
  assert(speakingActive && memcmp(&first, &saved, sizeof(first)) == 0);
  Face::AudioSnapshot current{};
  captureSpectrum(current, true, 0);
  Face::Frame expected;
  Face::compose(expected, displayedPose, current);
  // Synchronize composition to the current window, independent of loop phase.
  renderFace(millis());
  assert(memcmp(expected.bytes, displayedFrame.bytes, sizeof(expected.bytes)) == 0);

  behavior.glitchFrame = 1;
  renderFace(millis());
  assert(displayedPose.speaking && displayedPose.glitch == 1);
  behavior.beginRemoteBoop(millis(), 96);
  renderFace(millis());
  assert(displayedPose.boop);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      assert(Face::pixel(displayedFrame, 16 + x, y) == bool(maw[y * 4 + x / 8] & (0x80 >> (x % 8))));
  loop();
  assert(!microphoneSampling && !speakingActive);
  run(176);
  assert(microphoneSampling && speakingActive);
  setSleepingLocal(true);
  run(80);
  assert(displayBlanked && !microphoneSampling && !speakingActive);
  setSleepingLocal(false);
  run(128);
  assert(!displayBlanked && microphoneSampling && speakingActive);
#endif
  pauseMicrophone();
#endif
  delete sensorAdc;
  std::cout << "PASS: audio firmware profile\n";
}

#include "sketch.h"

static void run(unsigned ms) {
  while (ms--) {
    advanceTime(1000);
    loop();
  }
}

int main() {
  fakeUs = (uint64_t(UINT32_MAX) - 49) * 1000;
  setup();
  config.naturalBlinks = false;
  syntheticMicrophone = true;
  run(300);
  assert(audioDroppedSamples == 0 && microphoneSampling && speakingActive);
#if PROTOGEN_SUPERVISION_OBSERVE
  assert(supervision.audio.required && supervision.audio.lateEpisodes == 0);
  assert(supervision.foreground.lateEpisodes == 0 && supervision.render.lateEpisodes == 0);
#endif

  syntheticMicrophone = false;
  micInput = 1023;
  run(90);
  assert(microphoneRecoveryState() == 1);
  micInput = 510;
  unsigned wait = 0;
  while (microphoneRecoveryState() == 1 && wait++ < 2000) run(1);
  assert(microphoneRecoveryState() == 3 && microphoneSampling);
#if PROTOGEN_SUPERVISION_OBSERVE
  assert(!supervision.audio.required && supervision.audio.lateEpisodes == 0);
#endif
  run(100);
  assert(microphoneRecoveryState() == 0 && microphoneSampling);

  Serial.input = "MIC 0\nSAVE\n";
  run(256);
  assert(!microphoneSampling && !speakingActive);
#if PROTOGEN_SUPERVISION_OBSERVE
  assert(!supervision.audio.required && !supervision.audio.stale && supervision.audio.lateEpisodes == 0);
#endif
  setSleepingLocal(true);
  run(30);
  advanceTime(350000);
  loop();
#if PROTOGEN_SUPERVISION_OBSERVE
  assert(!supervision.foreground.required && !supervision.render.required && !supervision.audio.required);
  assert(supervision.foreground.lateEpisodes == 0 && supervision.render.lateEpisodes == 0);
#endif

  setSleepingLocal(false);
  run(40);
  advanceTime(300000);
  loop();
  assert(!displayBlanked && audioDroppedSamples == 0);
#if PROTOGEN_SUPERVISION_OBSERVE
  assert(supervision.foreground.lateEpisodes == 1 && supervision.render.lateEpisodes == 1);
  assert(!supervision.audio.required && supervision.audio.lateEpisodes == 0);
#endif
  // Compare legacy observable output across all four configurations. Counters
  // in the explicit opt-in diagnostic reply are exercised separately.
  std::cout << SPI.transactions << ' ' << SPI.bytes << ' ' << audioFrames << ' '
            << audioDroppedSamples << ' ' << Serial.output << '\n';
  for (const uint8_t byte : displayedFrame.bytes) std::cout << unsigned(byte) << ' ';
  std::cout << '\n';
  Serial.output.clear();
  Serial.input = "SUPERVISION\n";
  run(20);
#if PROTOGEN_SUPERVISION_OBSERVE
  assert(Serial.output.find("\"foreground\":{\"required\":true,\"stale\":false,\"late\":1") != std::string::npos);
  assert(Serial.output.find("\"audio\":{\"required\":false") != std::string::npos);
#else
  assert(Serial.output.find("unknown_command") != std::string::npos);
#endif
  pauseMicrophone();
  delete sensorAdc;
}

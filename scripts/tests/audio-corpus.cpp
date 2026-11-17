// Execute the actual microphone consumer and renderer on explicit labeled input.
// The normal ADC timer is disabled ONLY in this host adapter. One conversion is
// injected per 250us sample slot; the loop gets its original 1ms opportunities.
#include "sketch.h"
#include <chrono>

int main() {
  setup();
  config.naturalBlinks = config.idleMotion = 0;
  config.mouthMode = 1;
  loop(); // Finish the initial wake before taking over sample delivery.
  assert(microphoneSampling);
  sensorAdc->adc0->stopTimer();
  timeAdvanceHook = nullptr;
  unsigned raw, index = 0, peak = 0;
  const auto start = std::chrono::steady_clock::now();
  while (std::cin >> raw) {
    assert(raw <= 1023);
    advanceTime(250);
    sensorAdc->adc0->captured = raw;
    microphoneSampleISR();
    const unsigned used = static_cast<uint16_t>(audioWrite - audioRead);
    if (peak < used) peak = used;
    if (++index % 4 == 0) loop();
    if (index % 64 == 0)
      std::cout << index << ' ' << speakingActive << ' ' << unsigned(mouthAperture) << ' '
                << noiseFloor << ' ' << audioGate << ' ' << smoothedEnvelope << '\n';
  }
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  std::cerr << "{\"samples\":" << index << ",\"host_seconds\":" << elapsed
            << ",\"peak_backlog\":" << peak << ",\"dropped\":" << audioDroppedSamples
            << ",\"spi_bytes\":" << SPI.bytes << ",\"frames\":" << renderMetrics.compositions << "}\n";
  pauseMicrophone();
  delete sensorAdc;
}

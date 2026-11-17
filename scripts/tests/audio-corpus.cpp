// Execute the actual microphone consumer and renderer on explicit labeled input.
// The host adapter injects one conversion per sample slot. The target's ADC ISR
// and 1ms foreground opportunities are used without changing target code.
#include "sketch.h"
#include <chrono>

using Clock = std::chrono::steady_clock;

template<class Work>
static double hostNanoseconds(unsigned iterations, Work work) {
  const auto started = Clock::now();
  for (unsigned i = 0; i < iterations; ++i) work(i);
  return std::chrono::duration<double, std::nano>(Clock::now() - started).count() / iterations;
}

static void cost() {
  constexpr unsigned iterations = 64000;
  const double quiet = hostNanoseconds(iterations, [](unsigned i) {
    processMicrophoneSample(510 + (i % 11), i / 4);
  });
  const double fftWindow = hostNanoseconds(iterations / 64, [](unsigned i) {
    for (unsigned j = 0; j < samples; ++j)
      vReal[j] = 510 + ((i * 3 + j * 61) % 300);
    fft();
  });
  Face::Frame frame;
  Face::Pose pose;
  pose.mouthMode = 1;
  pose.speaking = true;
  Face::AudioSnapshot snapshot{};
  unsigned digest = 0;
  const double compose = hostNanoseconds(iterations / 64, [&](unsigned i) {
    pose.aperture = i % 4;
    Face::compose(frame, pose, snapshot);
    digest += frame.bytes[(i >> 2) % sizeof(frame.bytes)];
  });
  std::cout << "{\"quiet_sample_host_ns\":" << quiet << ",\"fft_and_prepare_host_ns\":" << fftWindow
            << ",\"compose_host_ns\":" << compose << ",\"compose_digest\":" << digest << "}\n";
}

int main(int argc, char **argv) {
  setup();
  config.naturalBlinks = config.idleMotion = 0;
  config.mouthMode = 1;
  loop(); // Finish the initial wake before taking over sample delivery.
  assert(microphoneSampling);
  if (argc == 2 && std::string(argv[1]) == "--cost") {
    cost();
  } else {
    assert(argc == 1);
    sensorAdc->adc0->stopTimer();
    timeAdvanceHook = nullptr;
    unsigned raw, index = 0, peak = 0;
    uint32_t previousLoop = micros(), maxGap = 0;
    const unsigned initialBytes = SPI.bytes, initialTransactions = SPI.transactions;
    const auto start = Clock::now();
    while (std::cin >> raw) {
      assert(raw <= 1023);
      advanceTime(250);
      sensorAdc->adc0->captured = raw;
      microphoneSampleISR();
      const unsigned used = static_cast<uint16_t>(audioWrite - audioRead);
      if (peak < used) peak = used;
      if (++index % 4 == 0) {
        const uint32_t now = micros();
        if (maxGap < now - previousLoop) maxGap = now - previousLoop;
        previousLoop = now;
        loop();
      }
      if (index % 64 == 0) {
        std::cout << index << '\t' << speakingActive << '\t' << unsigned(mouthAperture) << '\t'
                  << noiseFloor << '\t' << audioGate << '\t' << smoothedEnvelope << '\t';
        constexpr char hex[] = "0123456789abcdef";
        for (int y = 0; y < Face::Height; ++y)
          for (int i = 0; i < 4; ++i) {
            auto digit = displayedFrame.bytes[y * Face::RowBytes + Face::MouthRight / 8 + i];
            std::cout << hex[digit >> 4] << hex[digit & 15];
          }
        std::cout << '\n';
      }
    }
    const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    std::cerr << "{\"samples\":" << index << ",\"host_seconds\":" << elapsed
              << ",\"peak_backlog\":" << peak << ",\"dropped\":" << audioDroppedSamples
              << ",\"modeled_max_loop_gap_us\":" << maxGap
              << ",\"spi_bytes\":" << SPI.bytes - initialBytes
              << ",\"spi_transactions\":" << SPI.transactions - initialTransactions
              << ",\"frames\":" << renderMetrics.compositions << "}\n";
  }
  pauseMicrophone();
  delete sensorAdc;
}

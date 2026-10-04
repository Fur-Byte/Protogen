// Desktop executable of the actual firmware, with simulated GPIO/ADC/SPI/EEPROM.
// One response per line. It uses the same USB protocol as the physical helmet.
#include "tests/sketch.h"
#include <sstream>
static void advance(uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) { advanceTime(1000); loop(); }
}
int main() {
  setup(); loop();
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.rfind("tick ", 0) == 0) {
      unsigned ms = 0; std::istringstream(line.substr(5)) >> ms;
      if (ms > 1000) ms = 1000;
      advance(ms); std::cout << "{\"ok\":true}\n" << std::flush; continue;
    }
    if (line.rfind("sim ", 0) == 0) {
      std::string sensor; int value = 0; std::istringstream(line.substr(4)) >> sensor >> value;
      if (sensor == "boop") boopInput = value ? 100 : 1023;
      else if (sensor == "visor") visorInput = value ? HIGH : LOW;
      else if (sensor == "audio") { syntheticAmplitude = value; syntheticMicrophone = value != 0; }
      else if (sensor == "tap") { buttonInput = LOW; advance(30); buttonInput = HIGH; }
      else if (sensor == "hold") { buttonInput = LOW; advance(1250); buttonInput = HIGH; }
      else { std::cout << "{\"ok\":false,\"error\":\"unknown_simulation\"}\n" << std::flush; continue; }
      advance(30); std::cout << "{\"ok\":true}\n" << std::flush; continue;
    }
    Serial.input = line + "\n"; Serial.output.clear();
    for (unsigned i = 0; i < 2000 && (Serial.output.empty() || Serial.output.back() != '\n'); ++i) {
      advanceTime(100); loop();
    }
    if (Serial.output.empty() || Serial.output.back() != '\n') return 1;
    std::cout << Serial.output << std::flush;
  }
  pauseMicrophone();
#if defined(Microphone) || defined(BoopSensor)
  delete sensorAdc;
#endif
}

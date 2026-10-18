#include "sketch.h"
static void run(uint32_t ms) {for(uint32_t i=0;i<ms;++i){advanceTime(1000);loop();}}
static void command(const char* text) {
  std::string line(text);handleUsbCommand(&line[0],millis());assert(strstr(usbReply,"\"ok\":true"));
}
int main() {
  static_assert(Settings::MaxBrightness==15,"Prospective higher-ceiling host fixture only.");
  RuntimeConfig saved;saved.brightness=15;saved.brightnessApproval=1;assert(saveStoredConfig(saved));
  SPI.requireEnabledBuffer=true;setup();run(200);
  assert(connectorReady && config.brightness==15 && config.brightnessApproval==1 && SPI.registers[10]==1);
  command("SET mouth_mode=1");assert(SPI.registers[10]==1);
  command("BOOST 15 10");assert(SPI.registers[10]==15);run(15);assert(SPI.registers[10]==1);
  command("SLEEP 1");run(3);command("SLEEP 0");run(3);assert(SPI.registers[10]==1);
  pilotInput=HIGH;run(2);SPI.registers.fill(0);SPI.maxIntensity=0;
  pilotInput=LOW;run(100);assert(connectorReady && SPI.registers[10]==1 && SPI.maxIntensity<=1);
  sensorAdc->adc1->stuck=true;run(20);assert(!connectorReady);
  sensorAdc->adc1->stuck=false;run(800);assert(connectorReady && SPI.registers[10]==1 && SPI.maxIntensity<=1);
  command("SAVE");RuntimeConfig restored;assert(loadStoredConfig(restored) && restored.brightnessApproval==1);
  delete sensorAdc;
  std::cout<<"PASS: saved approval1 retained with simulated ceiling15 across dock, ADC recovery, sleep and BOOST expiry\n";
}

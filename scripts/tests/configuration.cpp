#include "sketch.h"
#include <string>

static std::string request(const std::string &command) {
  Serial.output.clear(); Serial.input += command + "\n";
  for (int i = 0; i < 2000 && (Serial.output.empty() || Serial.output.back() != '\n'); ++i) {
    advanceTime(100); loop();
  }
  assert(!Serial.output.empty() && Serial.output.back() == '\n');
  return Serial.output;
}
static bool success(const std::string &reply) { return reply.find("\"ok\":true") != std::string::npos; }

int main() {
  setup(); loop();
  assert(success(request("GET")));
  const auto original = config;
  for (const auto &bad : {"SET brightness=256", "SET sensitivity=-1", "SET sensitivity=1.5",
                         "SET brightness=5 boop_trigger=700 boop_release=600", "SET invented=1",
                         "SET brightness=2 brightness=3", "SET", "EXPR 6", "SLEEP 2", "GET extra"}) {
    assert(!success(request(bad))); assert(sameConfig(config, original));
  }
  assert(success(request("SET brightness=2 mouth_mode=1 boop_trigger=700 boop_release=750")));
  assert(config.brightness == 2 && config.mouthMode == 1 && config.boopThreshold == 700);
  assert(SPI.registers[10] == Settings::effectiveBrightness(2));
  assert(success(request("SAVE")));
  RuntimeConfig stored;
  assert(loadStoredConfig(stored) && sameConfig(config, stored));
  const auto writes = EEPROM.writes;
  assert(success(request("SAVE")) && EEPROM.writes == writes);
  assert(success(request("DEFAULTS")) && sameConfig(config, RuntimeConfig{}));
  assert(success(request("LOAD")) && sameConfig(config, stored));

  assert(success(request("EXPR 5")) && state == ExpressionWink);
  assert(success(request("SLEEP 1")) && sleeping && SPI.shutdown);
  assert(request("FRAME").find("\"hidden\":true") != std::string::npos);
  assert(success(request("SLEEP 0")) && !sleeping && !SPI.shutdown);
  const auto status = request("STATUS");
  assert(success(status) && status.find("\"schema\":2") != std::string::npos);
#ifdef Microphone
  assert(success(request("MIC 0")) && !microphoneEnabled);
  assert(success(request("MIC 1")) && microphoneEnabled);
#else
  assert(!success(request("MIC 1")) && !microphoneEnabled);
#endif
  const auto frame = request("FRAME");
  assert(frame.find("\"width\":112") != std::string::npos);

  // Incoming and outgoing USB backpressure never waits inside loop().
  Serial.capacity = 0; Serial.input = "GET\n"; Serial.output.clear();
  for (int i = 0; i < 100; ++i) { advanceTime(100); loop(); }
  assert(usbReplyLength > 0 && usbReplySent == 0);
  buttonInput = LOW;
  for (int i = 0; i < 1300; ++i) { advanceTime(1000); loop(); }
  assert(sleeping); // Hold processed even with a blocked host.
  Serial.connected = false; loop();
  assert(usbReplyLength == 0 && usbLineLength == 0);
  Serial.connected = true; Serial.capacity = 4096;
  buttonInput = HIGH; sleeping = false;
  assert(!success(request(std::string(500, 'X'))));
  Serial.input = "SET brightness=";
  for (int i = 0; i < 10; ++i) loop();
  advanceTime(2100000); loop();
  assert(!success(request("4")) && config.brightness == 2);
  assert(success(request("GET"))); // Parser recovers after malformed and timed-out lines.

  // Every interrupted byte of a save leaves either the old or complete new
  // configuration loadable; no partly written candidate can be accepted.
  const auto baseline = EEPROM.bytes;
  RuntimeConfig newer = stored; newer.brightness = 7;
  for (int budget = 0; budget < ConfigStore::RecordBytes + 2; ++budget) {
    EEPROM.bytes = baseline; EEPROM.writeBudget = budget;
    try { saveStoredConfig(newer); } catch (const std::runtime_error &) {}
    EEPROM.writeBudget = -1;
    RuntimeConfig recovered;
    assert(loadStoredConfig(recovered));
    assert(sameConfig(recovered, stored) || sameConfig(recovered, newer));
  }
  EEPROM.bytes = baseline;
  EEPROM.bytes[10] ^= 0x20; // Corruption in the only committed record.
  assert(!loadStoredConfig(stored));
  // A real schema-1 byte record migrates with held/proximity behavior and 6 s cap.
  EEPROM.bytes.fill(0xff);
  uint8_t legacy[47]={};memcpy(legacy,"P8CF",4);
  ConfigStore::writeNumber(legacy+4,1,2);ConfigStore::writeNumber(legacy+6,30,2);
  ConfigStore::writeNumber(legacy+8,9,4);
  for(size_t i=0;i<15;++i)ConfigStore::writeNumber(legacy+12+i*2,config.*(configFields[i].member),2);
  ConfigStore::writeNumber(legacy+42,ConfigStore::crc(legacy,42),4);legacy[46]=0xa5;
  memcpy(EEPROM.bytes.data(),legacy,sizeof(legacy));
  RuntimeConfig migrated;assert(loadStoredConfig(migrated));
  assert(migrated.brightness==config.brightness && migrated.boopMode==0 && migrated.boopDuration==6000);
  migrated.boopMode=1;migrated.boopDuration=1200;assert(saveStoredConfig(migrated));
  assert(loadStoredConfig(stored) && sameConfig(stored,migrated));
#ifdef Microphone
  syntheticMicrophone = true;
  assert(success(request("CALIBRATE")));
  for (int i = 0; i < 550; ++i) { advanceTime(1000); loop(); }
  assert(!calibratingAudio && config.noiseFloor > 0);
  assert(request("GET").find("\"noise_floor\":0") == std::string::npos);
  assert(success(request("SAVE")));
  RuntimeConfig calibrated;
  assert(loadStoredConfig(calibrated) && calibrated.noiseFloor == config.noiseFloor);
  syntheticMicrophone = false;
#endif
  std::cout << "PASS: atomic USB settings, strict parsing, diagnostics, backpressure, "
               "explicit save/load/defaults, CRC, unchanged-save wear, interrupted saves\n";
}

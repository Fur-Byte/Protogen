#include "sketch.h"
#include <string>
#include <vector>
#include <clocale>
#include <cwchar>

static bool success() { return strstr(usbReply, "\"ok\":true") != nullptr; }
static void command(const char *text) {
  std::string line(text); handleUsbCommand(&line[0], millis());
  std::cout << usbReply;
}
int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "brightness";
  if (mode == "errors") {
    setup();
    command("SET brightness=0");
    assert(success());
    command("GET");
    const std::string before = usbReply;
    const struct { const char *request; const char *error; } rejected[] = {
      {"SET brightness=15 brightness=10", "duplicate_setting"},
      {"SET brightness=15 brightness=16", "invalid_setting"},
      {"SET brightness=15 brightness=no", "invalid_setting"},
      {"SET brightness=15 brightness=65536", "invalid_setting"},
      {"SET brightness=15 unknown=1", "invalid_setting"},
      {"SET brightness=15 brightness", "expected_key_value"},
      {"SET brightness=15 boop_trigger=900 boop_release=100", "invalid_configuration"},
    };
    for (const auto &test : rejected) {
      command(test.request);
      assert(usbReply == std::string("{\"ok\":false,\"error\":\"") + test.error + "\"}\n");
      command("GET");
      assert(before == usbReply);
    }
    const std::string longError(2048, 'x');
    replyError(longError.c_str());
    assert(std::string(usbReply).find("\"}\n") != std::string::npos);
    std::cout << usbReply;
    return 0;
  }
  if (mode == "bounds") {
    setup();
    ConfigFieldSeen<65> seen;
    for (size_t i=0; i<65; ++i) assert(seen.mark(i));
    for (size_t i=0; i<65; ++i) assert(!seen.mark(i));
    assert(!seen.mark(65) && !seen.mark(SIZE_MAX));
    command("SET brightness=15 startup_expression=5 mouth_mode=1 sensitivity=1023 noise_margin=1023 noise_floor=1023 boop_trigger=1022 boop_release=1023 blink_interval_ms=60000 blink_jitter_ms=5000 transition_ms=1000 attack_ms=2000 release_ms=2000 natural_blinks=1 idle_motion=1 boop_mode=1 boop_duration_ms=6000");
    assert(success());
    for (const auto &field : configFields) assert(config.*(field.member) == field.maximum);
    const auto before = config;
    for (const auto &field : configFields) {
      const std::string duplicate = std::string("SET ") + field.name + "=" + std::to_string(field.maximum) + " " + field.name + "=" + std::to_string(field.maximum);
      command(duplicate.c_str()); assert(!success() && sameConfig(config,before));
      assert(strstr(usbReply, "duplicate_setting"));
    }
    // Full five-digit member values, long names and field counts beyond 32.
    std::vector<std::string> names(80);
    std::vector<ConfigField> fields;
    config.brightness=65535;
    for (size_t i=0; i<names.size(); ++i) names[i] = std::string(40,'x') + std::to_string(i);
    for (const auto &name : names) fields.push_back({name.c_str(),&RuntimeConfig::brightness,0,65535});
    char large[8192];
    assert(writeConfigReply(large,sizeof(large),fields.data(),fields.size()));
    std::cout << large;
    const size_t needed=strlen(large)+1;
    for(size_t capacity=0; capacity<=needed+1; ++capacity) {
      std::vector<char> guarded(capacity+2,'G');
      const bool okay=writeConfigReply(guarded.data()+1,capacity,fields.data(),fields.size());
      assert(guarded.front()=='G' && guarded.back()=='G');
      assert(okay==(capacity>=needed));
      if(okay) assert(strcmp(guarded.data()+1,large)==0);
    }
    assert(!writeConfigReply(nullptr,0,fields.data(),fields.size()));
    assert(!writeConfigReply(usbReply,sizeof(usbReply),fields.data(),fields.size()));
    names[0]=std::string(2048,'z');fields[0].name=names[0].c_str();
    assert(!writeConfigReply(usbReply,sizeof(usbReply),fields.data(),1));
    char small[8]; BoundedReply bounded(small,sizeof(small));
    assert(bounded.append("1234567") && !bounded.append("8"));
    assert(!bounded.append("more") && small[7]=='\0');
    std::setlocale(LC_ALL,"C");
    BoundedReply negative(small,sizeof(small));
    assert(!negative.append("%lc",static_cast<wint_t>(0x100)) && !negative.okay());
    assert(!negative.append("ignored"));
    replyError("quote\" slash\\ newline\n"); std::cout << usbReply;
    replyError(std::string(2048,'x').c_str()); std::cout << usbReply;
    config=before;
    command("GET"); command("STATUS"); command("FRAME"); command("PERF");
    return 0;
  }
  RuntimeConfig saved; saved.brightness = 15;
  assert(saveStoredConfig(saved));
  setup(); loop();
  assert(config.brightness == 15);
  assert(SPI.registers[10] <= Settings::MaxBrightness); // Conservative default must apply to EEPROM startup.
  command("GET");
  assert(strstr(usbReply,"\"configured\":15") && strstr(usbReply,("\"maximum\":" + std::to_string(Settings::MaxBrightness)).c_str()));
  command("SET brightness=15"); assert(success() && SPI.registers[10] <= Settings::MaxBrightness);
  command("BOOST 15 100"); assert(success() && SPI.registers[10] <= Settings::MaxBrightness);
  advanceTime(101000); loop(); assert(SPI.registers[10] <= Settings::MaxBrightness);
  command("SLEEP 1"); loop(); command("SLEEP 0"); loop(); assert(SPI.registers[10] <= Settings::MaxBrightness);
  matrix.begin(Settings::SpiFrequencyHz); assert(SPI.registers[10] <= Settings::MaxBrightness);
  matrix.setIntensity(255); assert(SPI.registers[10] <= Settings::MaxBrightness);
  command("SAVE"); command("DEFAULTS"); command("LOAD");
  assert(success() && config.brightness == 15 && SPI.registers[10] <= Settings::MaxBrightness);
  command("STATUS");
  assert(SPI.maxIntensity <= Settings::MaxBrightness);
}

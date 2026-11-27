#include "sketch.h"
#include <fstream>
#include <string>

// Historical records are fixtures, never generated through the new writer.
static void legacyRecord(unsigned format, unsigned slot, uint32_t sequence, const RuntimeConfig &value) {
  const size_t fields = format == 1 ? 15 : configFieldCount;
  uint8_t record[64] = {};
  memcpy(record,"P8CF",4);
  ConfigStore::writeNumber(record+4,format,2);
  ConfigStore::writeNumber(record+6,fields*2,2);
  ConfigStore::writeNumber(record+8,sequence,4);
  for(size_t i=0;i<fields;++i) ConfigStore::writeNumber(record+12+i*2,value.*configFields[i].member,2);
  const size_t crcAt=12+fields*2;
  ConfigStore::writeNumber(record+crcAt,ConfigStore::crc(record,crcAt),4);
  record[crcAt+4]=0xa5;
  memcpy(EEPROM.bytes.data()+slot*64,record,crcAt+5);
}
static void command(const char *text) {
  std::string line(text); handleUsbCommand(&line[0],millis());
  assert(strstr(usbReply,"\"ok\":true"));
}
static void seed(unsigned format,unsigned slot,const RuntimeConfig &value,uint32_t sequence=7) {
  EEPROM.bytes.fill(0xff);
  if(format<3)legacyRecord(format,slot,sequence,value);
  else {
    uint8_t record[ConfigStore::RecordBytes]={};memcpy(record,"P8CF",4);
    ConfigStore::writeNumber(record+4,3,2);ConfigStore::writeNumber(record+6,ConfigStore::PayloadBytes,2);
    ConfigStore::writeNumber(record+8,sequence,4);
    for(size_t i=0;i<configFieldCount;++i)ConfigStore::writeNumber(record+12+i*2,value.*configFields[i].member,2);
    ConfigStore::writeNumber(record+ConfigStore::ApprovalOffset,value.brightnessApproval,2);
    ConfigStore::writeNumber(record+ConfigStore::CrcOffset,ConfigStore::crc(record,ConfigStore::CrcOffset),4);
    record[ConfigStore::MarkerOffset]=0xa5;
    memcpy(EEPROM.bytes.data()+slot*64,record,sizeof(record));
  }
}
static void boot() {
  SPI=FakeSPI();setup();loop();
  assert(SPI.maxIntensity<=Settings::MaxBrightness);
}
int main(int argc,char **argv) {
  if(argc==3) {
    const std::string mode=argv[1];
    if(mode=="write") {
      boot();command("SET brightness=15");command("BOOST 0 30000");
      command("SAVE");
      std::ofstream output(argv[2],std::ios::binary);
      output.write(reinterpret_cast<const char*>(EEPROM.bytes.data()),EEPROM.bytes.size());assert(output.good());
    } else {
      std::ifstream input(argv[2],std::ios::binary);
      input.read(reinterpret_cast<char*>(EEPROM.bytes.data()),EEPROM.bytes.size());assert(input.good());
      boot();assert(config.brightness==15 && config.brightnessApproval==1 && config.effectiveBrightness()==1);
      assert(SPI.registers[10]==1);command("SET mouth_mode=1");assert(SPI.registers[10]==1);
      command("BOOST 15 1");advanceTime(2000);loop();assert(SPI.registers[10]==1);
      assert(!sameConfig(config,RuntimeConfig{}));
      command("SET brightness=15");assert(SPI.registers[10]==Settings::MaxBrightness);
    }
    return 0;
  }
  static_assert(configSchemaVersion==2,"Public configuration schema must remain unchanged.");
  static_assert(ConfigStore::RecordVersion==3 && ConfigStore::RecordBytes==53,"Explicit format3 layout.");
  RuntimeConfig historical;historical.brightness=15;historical.brightnessApproval=1;
  for(unsigned format=1;format<=3;++format)for(unsigned slot=0;slot<2;++slot) {
    seed(format,slot,historical);
    const auto untouched=EEPROM.bytes;
    boot();
    assert(config.brightness==15 && config.brightnessApproval==1);
    assert(SPI.registers[10]<=1 && EEPROM.bytes==untouched);
    command("SET mouth_mode=1");assert(SPI.registers[10]<=1);
    command("BOOST 15 1");advanceTime(2000);loop();assert(SPI.registers[10]<=1);
    const auto original=config;
    command("SET brightness=15");
    assert(config.brightnessApproval==Settings::MaxBrightness);
    assert(SPI.registers[10]==Settings::MaxBrightness);
    if(Settings::MaxBrightness!=1)assert(!sameConfig(original,config));
    command("SAVE");
    const auto a=ConfigStore::readSlot(0),b=ConfigStore::readSlot(1);
    const auto &saved=ConfigStore::newest(a,b)==0?a:b;
    assert(saved.valid && ConfigStore::readNumber(saved.bytes+4,2)==3);
    assert(saved.config.brightness==15 && saved.config.brightnessApproval==Settings::MaxBrightness);
    const auto writes=EEPROM.writes;command("SAVE");assert(EEPROM.writes==writes);

    // Every interrupted write, in either slot and starting with every format.
    seed(format,slot,historical);const auto baseline=EEPROM.bytes;
    RuntimeConfig old;assert(loadStoredConfig(old));
    RuntimeConfig next=old;assert(setConfigField(next,"brightness",8));next.mouthMode=1;
    for(int budget=0;budget<ConfigStore::RecordBytes+3;++budget) {
      EEPROM.bytes=baseline;EEPROM.writeBudget=budget;
      try {saveStoredConfig(next);}catch(const std::runtime_error&){}
      EEPROM.writeBudget=-1;
      RuntimeConfig recovered;assert(loadStoredConfig(recovered));
      assert(sameConfig(recovered,old) || sameConfig(recovered,next));
      assert(recovered.effectiveBrightness()<=Settings::MaxBrightness);
    }
  }
  // SAVE reports persisted base approval; GET/STATUS report live authorization.
  // A synthetic runtime state models a lower effective level without reapproval.
  config.brightness = 0;
  config.brightnessApproval = 1;
  command("GET");
  assert(strstr(usbReply, "\"approved\":1"));
  command("STATUS");
  assert(strstr(usbReply, "\"approved\":1"));
  command("SAVE");
  assert(strstr(usbReply, "\"approved\":0"));
  assert(config.brightnessApproval == 1);
  RuntimeConfig persisted;
  assert(loadStoredConfig(persisted) && persisted.brightnessApproval == 0);
  command("LOAD");
  assert(config.brightnessApproval == 0);
  assert(strstr(usbReply, "\"approved\":0"));

  // CRC-valid but impossible approval values cannot become authoritative.
  for(unsigned approval:{16u,65535u}) {
    historical.brightnessApproval=approval;seed(3,0,historical);
    RuntimeConfig rejected;assert(!loadStoredConfig(rejected));
  }
  historical.brightness=0;historical.brightnessApproval=1;seed(3,1,historical);
  RuntimeConfig rejected;assert(!loadStoredConfig(rejected));
  // Mixed formats, rollover ordering and corruption still retain a valid prior record.
  historical.brightness=15;historical.brightnessApproval=1;seed(3,0,historical,0xfffffffeUL);
  RuntimeConfig latest=historical;latest.mouthMode=1;legacyRecord(2,1,1,latest);
  RuntimeConfig loaded;assert(loadStoredConfig(loaded) && sameConfig(loaded,latest));
  EEPROM.bytes[64+20]^=1;assert(loadStoredConfig(loaded) && sameConfig(loaded,historical));
  std::cout<<"PASS: format1/2/3, both slots, CRC/approval bounds, read-only legacy loads, explicit reapproval, unchanged saves, interrupted migration and sequence rollover\n";
}

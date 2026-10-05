#pragma once
#include <stdint.h>
#include <string.h>
#include "HelmetProtocol.h"
namespace Device {
#ifdef HELMET_RECEIVER
constexpr uint8_t Role=1;
constexpr int UartRx=18,UartTx=19,I2cSda=0,I2cScl=1;
constexpr int ScreenSck=4,ScreenMosi=6,ScreenCs=7,ScreenDc=5,ScreenReset=10;
constexpr int TemperatureAdc=3,UnlockPin=9;
#else
constexpr uint8_t Role=2;
constexpr int NextButton=4,PreviousButton=5,BoopButton=10,BatteryAdc=3;
#endif
enum Feature : uint8_t { Environment=1,EnergyMonitor=2,Screen=4,BatteryCaseTemperature=8 };
// Serialized explicitly to avoid padding, ABI, or interrupted multi-key writes.
struct Config {
  bool paired=false;
  uint8_t channel=6,features=0,peer[6]{},psk[32]{},lmk[16]{},pmk[16]{};
  uint32_t usableEnergyMWh=0;
};
constexpr size_t ConfigBytes=82;
inline void encode(const Config &c,uint8_t b[ConfigBytes]) {
  memset(b,0,ConfigBytes);b[0]=1;b[1]=Role;b[2]=c.paired;b[3]=c.channel;b[4]=c.features;
  memcpy(b+6,c.peer,6);memcpy(b+12,c.psk,32);memcpy(b+44,c.lmk,16);memcpy(b+60,c.pmk,16);
  HelmetLink::put32(b+76,c.usableEnergyMWh);HelmetLink::put16(b+80,HelmetLink::crc16(b,80));
}
inline bool decode(const uint8_t *b,size_t n,Config &c) {
  if(n!=ConfigBytes || b[0]!=1 || b[1]!=Role || b[2]>1 || b[3]<1 || b[3]>11 ||
     (b[4]&~(Role==1?15:2)) || b[5]!=0 || HelmetLink::u16(b+80)!=HelmetLink::crc16(b,80))return false;
  Config value;value.paired=b[2];value.channel=b[3];value.features=b[4];
  memcpy(value.peer,b+6,6);memcpy(value.psk,b+12,32);memcpy(value.lmk,b+44,16);memcpy(value.pmk,b+60,16);
  value.usableEnergyMWh=HelmetLink::u32(b+76);
  if(value.paired) {
    uint8_t key=0,local=0,master=0,mac=0;
    for(auto x:value.psk)key|=x;
    for(auto x:value.lmk)local|=x;
    for(auto x:value.pmk)master|=x;
    for(auto x:value.peer)mac|=x;
    if(!key || !local || !master || !mac || (value.peer[0]&1))return false;
  }
  if(value.usableEnergyMWh>1000000)return false;
  c=value;return true;
}
}

#include "src/HelmetProtocol/RetryDelay.h"
namespace Dock {
#ifdef VisorConnector
constexpr int PowerEnable=6, BufferEnable=4, Pilot=12, VoltagePin=15;
uint32_t changedAt=0,lastPoll=0;
HelmetLink::RetryDelay<500> retry;
uint8_t stage=0; // 0 off, 1 ramping, 2 ready; hardware also gates both outputs.
void disable(uint32_t now,bool fault) {
  digitalWrite(BufferEnable,LOW);digitalWrite(PowerEnable,LOW);
  connectorReady=false;displayBlanked=true;frameValid=false;
  pauseMicrophone();setColor(0,0,0);stage=0;changedAt=now;
  if(fault){sensorFaults|=1;retry.start(now);}
}
#endif
}
void setupVisorConnector() {
#ifdef VisorConnector
  pinMode(Dock::BufferEnable,OUTPUT);digitalWrite(Dock::BufferEnable,LOW);
  pinMode(Dock::PowerEnable,OUTPUT);digitalWrite(Dock::PowerEnable,LOW);
  pinMode(Dock::Pilot,INPUT_PULLUP);pinMode(Dock::VoltagePin,INPUT);
  connectorReady=false;
#endif
}
void updateVisorConnector(uint32_t now) {
#ifdef VisorConnector
  using namespace Dock;
  if(now-lastPoll<1)return;
  lastPoll=now;
  // This software check complements, and never substitutes for, the hardware pilot gate.
  if(digitalRead(Pilot)!=LOW || !visorVisible) {if(stage)disable(now,false);return;}
  if(stage==0) {
    if(!retry.ready(now) || now-changedAt<20)return;
    digitalWrite(PowerEnable,HIGH);changedAt=now;stage=1;return;
  }
  if(stage==1 && now-changedAt<50)return;
  if(!sensorAdc) {disable(now,true);return;}
  // 47k/33k divider: qualified 4.65..5.35 V becomes ADC 595..684 at 3.3 V reference.
  const int voltage=readDisplayVoltage();
  if(voltage<595 || voltage>684) {disable(now,true);return;}
  if(stage==1) {
    digitalWrite(BufferEnable,HIGH);
    displayReady=matrix.begin(Settings::SpiFrequencyHz,true);
    cancelBrightnessBoost();matrix.setIntensity(static_cast<uint8_t>(config.brightness));
    if(!displayReady){disable(now,true);return;}
    sensorFaults&=static_cast<uint16_t>(~1u);
    connectorReady=true;displayBlanked=true;frameValid=false;rising=true;stage=2;
  }
#else
  (void)now;
#endif
}

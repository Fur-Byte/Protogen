#include "sketch.h"
using namespace HelmetLink;
static void run(uint32_t ms){for(uint32_t i=0;i<ms;++i){advanceTime(1000);loop();}}
static Packet status() {
  run(110);Decoder decoder;Packet p,result;
  for(uint8_t b:Serial1.output)if(decoder.feed(b,millis(),p) && p.kind==Status)result=p;
  Serial1.output.clear();assert(result.epoch && result.ticket);return result;
}
static void send(const Packet &p) {
  uint8_t raw[MaxRaw],encoded[MaxEncoded];const size_t n=pack(p,raw,sizeof(raw)),m=cobsEncode(raw,n,encoded,sizeof(encoded));
  Serial1.input.append(reinterpret_cast<const char*>(encoded),m);run(5);
}
int main() {
  setup();assert(!connectorReady);run(200);
  config.transitionMs=0;config.naturalBlinks=0;assert(connectorReady && !SPI.shutdown);
  assert(digitalValues[6]==HIGH && digitalValues[4]==HIGH);
  const auto normal=SPI.display;
  auto s=status();Packet p;p.kind=Hello;p.length=4;p.epoch=s.epoch;p.ticket=s.ticket;put32(p.payload,100);send(p);assert(pawIsOnline(millis()));
  p.kind=Command;p.length=8;p.sequence=1;p.payload[4]=HelmetLink::Expression;p.payload[5]=2;send(p);assert(state==2);
  selectExpression(0,millis());send(p);assert(state==0); // A duplicate ACK retry cannot execute again.
  s=status();p.epoch=s.epoch;p.ticket=s.ticket;p.sequence=2;p.payload[4]=Boop;p.payload[5]=0;put16(p.payload+6,1200);send(p);
  assert(boopActive && boopFromRemote);const auto artwork=SPI.display;run(1300);assert(!boopActive);
  s=status();p.ticket=s.ticket;p.sequence=3;p.payload[4]=Sleep;p.payload[5]=1;put16(p.payload+6,0);
  run(310);send(p);assert(!sleeping); // Valid-looking UART packet is now too old.
  run(2200);assert(!pawIsOnline(millis()));
  s=status();p.ticket=s.ticket;p.sequence=4;p.payload[4]=MicControl;p.payload[5]=0;send(p);assert(!microphoneEnabled && !speakingActive);
  // Hardware pilot controls outputs immediately; software also stops using the display.
  pilotInput=HIGH;run(2);assert(!connectorReady && digitalValues[4]==LOW && digitalValues[6]==LOW);
  pilotInput=LOW;run(100);assert(connectorReady && !SPI.shutdown && SPI.display==normal);
  voltageInput=500;run(10);assert(!connectorReady && (sensorFaults&1));voltageInput=640;run(700);assert(connectorReady);
  // Rail monitoring must not reuse an old good reading during sleep/reattach.
  sleeping=true;run(10);voltageInput=500;run(20);
  assert(!connectorReady && (sensorFaults&1) && readDisplayVoltage()==500);
  voltageInput=640;pilotInput=HIGH;run(2);pilotInput=LOW;run(700);
  assert(connectorReady && SPI.shutdown);
  sleeping=false;run(20);assert(!SPI.shutdown && SPI.display==normal);
  // Physical timed mode uses the same original vwv artwork, and survives early release.
  config.boopMode=1;config.boopDuration=1000;boopInput=1023;run(20);boopInput=100;run(20);
  assert(boopActive && !boopFromRemote && SPI.display==artwork);boopInput=1023;run(100);assert(boopActive);
  run(1000);assert(!boopActive);
  config.boopMode=0;boopInput=100;run(20);assert(boopActive);boopInput=1023;run(20);assert(!boopActive);
  microphoneEnabled=true;micInput=1023;run(100);
  assert(!microphoneEnabled && !speakingActive && (sensorFaults&4));
  // A permanently busy ADC1 conversion can't wait inside the face loop.
  sensorAdc->adc1->stuck=true;run(30);assert(sensorFaults&2);
  const uint8_t before=state;buttonInput=LOW;run(35);buttonInput=HIGH;run(35);assert(state==(before+1)%ExpressionCount);
  // Disconnect/reconnect display cannot be validated when its voltage ADC has failed.
  assert(!connectorReady);
  delete sensorAdc;
  std::cout<<"PASS: authenticated-link handoff, command dedup/expiry, remote and physical original-vwv boops, timeout, mute, pilot, undervoltage, sleep/reattach monitoring, reinitialization, stuck-ADC isolation\n";
}

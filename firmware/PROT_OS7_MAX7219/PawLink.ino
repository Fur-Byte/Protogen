#ifdef PROTOGEN_UART_AUTH
#include "src/HelmetProtocol/UartAuthentication.h"
#include "src/UartCrypto/UartMac.h"
#endif
namespace PawLink {
#ifdef WirelessPaw
#ifdef PROTOGEN_UART_AUTH
UartSecurity::MbedMac uartMac;
bool authMac(void *,const uint8_t *p,size_t n,uint8_t out[32]) {return uartMac.authenticate(p,n,out);}
bool authRandom(void *,uint8_t *p,size_t n) {
  if(Entropy.available()<(n+3)/4)return false;
  while(n) {const uint32_t value=Entropy.random();const size_t take=n<4?n:4;memcpy(p,&value,take);p+=take;n-=take;}
  return true;
}
UartSecurity::Endpoint decoder(UartSecurity::Teensy,nullptr,authMac,authRandom);
constexpr size_t UartMaxEncoded=UartSecurity::MaxEncoded;
#else
HelmetLink::Decoder decoder;
constexpr size_t UartMaxEncoded=HelmetLink::MaxEncoded;
#endif
HelmetLink::Gate gate;
uint32_t lastStatus = 0, ticket = 0, statusSequence = 0, heartbeatAt = 0;
uint8_t tx[UartMaxEncoded] = {}, serialRxMemory[256] = {};
size_t txLength = 0, txSent = 0;
HelmetLink::Result lastResult = HelmetLink::Unavailable;
bool haveHeartbeat = false;

bool enqueue(const HelmetLink::Packet &packet) {
  if (txSent < txLength) return false;
#ifdef PROTOGEN_UART_AUTH
  txLength=decoder.seal(packet,tx,sizeof(tx));txSent=0;
  return txLength!=0;
#else
  uint8_t raw[HelmetLink::MaxRaw];
  const size_t n = HelmetLink::pack(packet, raw, sizeof(raw));
  if (!n) return false;
  txLength = HelmetLink::cobsEncode(raw, n, tx, sizeof(tx)); txSent = 0;
  return txLength != 0;
#endif
}
void status(uint32_t now) {
  if (txSent < txLength) return;
  HelmetLink::Packet p; p.kind = HelmetLink::Status; p.length = HelmetLink::StatusBytes;
  p.sequence = ++statusSequence; p.epoch = gate.epoch; p.ticket = ++ticket;
  if (!p.ticket) p.ticket = ++ticket;
  HelmetLink::put32(p.payload, gate.bridge);
  p.payload[4] = state;
  p.payload[5] = (sleeping ? HelmetLink::Sleeping : 0) | (visorVisible ? HelmetLink::Visible : 0) |
    (boopActive ? HelmetLink::Booping : 0) | (speakingActive ? HelmetLink::Speaking : 0) |
    (microphoneEnabled ? HelmetLink::MicEnabled : 0) | (displayReady && connectorReady ? HelmetLink::DisplayReady : 0) |
    (haveHeartbeat && now-heartbeatAt < HelmetLink::LinkTimeoutMs ? HelmetLink::PawOnline : 0);
  p.payload[6] = 8;
#ifdef VisorSensor
  p.payload[6] |= 1;
#endif
#ifdef BoopSensor
  p.payload[6] |= 2;
#endif
#ifdef Microphone
  p.payload[6] |= 4;
  HelmetLink::put32(p.payload+12, audioDroppedSamples);
#endif
#ifdef VisorConnector
  p.payload[6] |= 16;
#endif
  p.payload[7] = static_cast<uint8_t>(config.boopMode);
  HelmetLink::put16(p.payload+8, static_cast<uint16_t>(boopReading));
  HelmetLink::put16(p.payload+10, sensorFaults);
  const uint32_t duration = boopFromRemote ? remoteBoopDuration : config.boopDuration;
  const uint32_t elapsed = now-boopTime;
  HelmetLink::put16(p.payload+16, boopActive && elapsed<duration ? static_cast<uint16_t>(duration-elapsed) : 0);
  p.payload[18] = static_cast<uint8_t>(configSchemaVersion);
  if (enqueue(p)) {gate.issue(p.ticket, now);lastStatus=now;}
}
void receive(const HelmetLink::Packet &p, uint32_t now) {
  if (p.kind == HelmetLink::Hello) {
    const uint32_t oldBridge=gate.bridge;
    if (gate.hello(p, now)) {
      haveHeartbeat=true;heartbeatAt=now;
      if(oldBridge!=gate.bridge)lastResult=HelmetLink::Unavailable;
    }
    return;
  }
  if (p.kind == HelmetLink::Heartbeat && p.length == 4 && p.epoch == gate.epoch &&
      gate.bridge && HelmetLink::u32(p.payload) == gate.bridge && gate.fresh(p.ticket, now)) {
    haveHeartbeat=true;heartbeatAt=now;return;
  }
  if (p.kind != HelmetLink::Command) return;
  HelmetLink::Result result=gate.check(p,now);
  if (result == HelmetLink::Duplicate) result = lastResult == HelmetLink::Accepted ? HelmetLink::Duplicate : lastResult;
  else if (result == HelmetLink::Accepted) {
    switch(p.payload[4]) {
      case HelmetLink::Expression: selectExpression(p.payload[5],now); break;
      case HelmetLink::Sleep: sleeping=p.payload[5]!=0; break;
      case HelmetLink::MicControl:
#ifdef Microphone
        microphoneEnabled=p.payload[5]!=0;
        if(!microphoneEnabled)pauseMicrophone();
        break;
#else
        result=HelmetLink::Unavailable;break;
#endif
      case HelmetLink::Boop:
        if(sleeping || !visorVisible || !connectorReady || !displayReady || boopActive) result=HelmetLink::Unavailable;
        else {boopActive=true;boopFromRemote=true;remoteBoopDuration=HelmetLink::u16(p.payload+6);
          boopTime=previousMillis=now;resetAnimations(now);setColor(16,4,0);rising=true;}
        break;
    }
    lastResult=result;
  }
  HelmetLink::Packet ack;ack.kind=HelmetLink::Ack;ack.length=5;
  ack.sequence=p.sequence;ack.epoch=gate.epoch;ack.ticket=p.ticket;
  HelmetLink::put32(ack.payload,gate.bridge);ack.payload[4]=result;enqueue(ack);
}
#endif
}
void setupPawLink() {
#ifdef WirelessPaw
  Serial1.begin(115200);Serial1.addMemoryForRead(PawLink::serialRxMemory,sizeof(PawLink::serialRxMemory));
#endif
}
bool pawIsOnline(uint32_t now) {
#ifdef WirelessPaw
  return PawLink::haveHeartbeat && now-PawLink::heartbeatAt<HelmetLink::LinkTimeoutMs;
#else
  (void)now;return false;
#endif
}
uint32_t pawRejectedPackets() {
#ifdef WirelessPaw
  return PawLink::decoder.errors+PawLink::gate.rejected;
#else
  return 0;
#endif
}
void servicePawLink(uint32_t now) {
#ifdef WirelessPaw
  using namespace PawLink;
  if(!gate.epoch && Entropy.available()) {gate.epoch=Entropy.random();if(!gate.epoch)gate.epoch=1;
#ifdef PROTOGEN_UART_AUTH
    decoder.begin(gate.epoch,now);
#endif
  }
  if(txSent<txLength) {
    const int capacity=Serial1.availableForWrite();
    if(capacity>0) {
      size_t n=txLength-txSent;if(n>32)n=32;if(n>static_cast<size_t>(capacity))n=capacity;
      txSent+=Serial1.write(tx+txSent,n);
    }
  }
  if(txSent>=txLength) {
    HelmetLink::Packet p;
    for(unsigned budget=0;budget<64 && Serial1.available();++budget) {
      if(decoder.feed(static_cast<uint8_t>(Serial1.read()),now,p)) {receive(p,now);break;}
    }
#ifdef PROTOGEN_UART_AUTH
    if(txSent>=txLength) {txLength=decoder.poll(now,tx,sizeof(tx));txSent=0;}
#endif
    if(txSent>=txLength && now-lastStatus>=100) status(now);
  }
#else
  (void)now;
#endif
}

bool pawHasPendingWork() {
#ifdef WirelessPaw
  return Serial1.available() || PawLink::txSent < PawLink::txLength;
#else
  return false;
#endif
}

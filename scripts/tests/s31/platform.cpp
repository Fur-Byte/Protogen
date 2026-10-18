#include "BoardProfile.hpp"
#include "AuthenticatedUart.hpp"
#include "WirelessCrypto.h"
#include <cassert>
#include <deque>
#include <vector>
#include <cstdio>
using namespace ProtogenResearch;
namespace {
struct Pipe : ByteChannel {
  Pipe *peer = nullptr;
  std::deque<uint8_t> incoming;
  std::vector<uint8_t> sent;
  bool blocked = false, failed = false;
  int read(uint8_t *p, size_t capacity) override {
    if (failed) return -1;
    size_t n = 0;
    while (n < capacity && n < 7 && !incoming.empty()) {
      p[n++] = incoming.front(); incoming.pop_front();
    }
    return int(n);
  }
  int write(const uint8_t *p, size_t size) override {
    if (failed) return -1;
    if (blocked) return 0;
    const size_t n = size < 5 ? size : 5;
    for (size_t i=0;i<n;++i) {peer->incoming.push_back(p[i]);sent.push_back(p[i]);}
    return int(n);
  }
};
uint8_t key[32]{13}; // Public synthetic fixture; no device credential.
bool mac(void *, const uint8_t *p, size_t n, uint8_t out[32]) {
  return Wireless::Crypto::hmac(key,p,n,out);
}
bool random(void *, uint8_t *p, size_t n) {return Wireless::Crypto::random(p,n);}
struct Receiver { unsigned packets=0; HelmetLink::Packet latest;
  static void accept(void *context,const HelmetLink::Packet &p) {
    auto &self=*static_cast<Receiver*>(context);++self.packets;self.latest=p;
  }
};
void boardChecks() {
  BoardProfile p;
  Capabilities c{0xff,0xff,4}; // Synthetic host mask, not S31 board pin advice.
  assert(validate(p,c)==ProfileError::Unreviewed);
  p.reviewed=true;p.name="synthetic-host-fixture";p.target=Target::Esp32S31;
  p.role=Role::Helmet;p.flashBytes=4*1024*1024;p.uartPort=1;p.rx=2;p.tx=3;
  p.exposedInputs=p.exposedOutputs=0xff;
  assert(validate(p,c)==ProfileError::None);
  p.tx=p.rx;assert(validate(p,c)==ProfileError::PinConflict);p.tx=3;
  p.rx=-1;assert(validate(p,c)==ProfileError::PinUnavailable);p.rx=2;
  p.rx=64;assert(validate(p,c)==ProfileError::PinUnavailable);p.rx=2;
  p.reservedPins=uint64_t(1)<<3;assert(validate(p,c)==ProfileError::PinUnavailable);p.reservedPins=0;
  p.consolePort=1;assert(validate(p,c)==ProfileError::ConsoleConflict);p.consolePort=-1;
  p.consolePins=uint64_t(1)<<2;assert(validate(p,c)==ProfileError::ConsoleConflict);p.consolePins=0;
  p.exposedOutputs=0;assert(validate(p,c)==ProfileError::PinUnavailable);p.exposedOutputs=0xff;
  p.uartPort=4;assert(validate(p,c)==ProfileError::UartUnavailable);p.uartPort=1;
  p.target=Target::Unspecified;assert(validate(p,c)==ProfileError::WrongTarget);p.target=Target::Esp32S31;
  p.role=Role::Paw;assert(validate(p,c)==ProfileError::UnsupportedRole);p.role=Role::Helmet;
  p.flashBytes=0;assert(validate(p,c)==ProfileError::MemoryUnspecified);p.flashBytes=4*1024*1024;
  p.psramBytes=1;assert(validate(p,c)==ProfileError::MemoryUnspecified);p.psramBytes=0;
  p.baud=9600;assert(validate(p,c)==ProfileError::UartUnavailable);p.baud=115200;
  assert(validate(p,c)==ProfileError::None);
}
void uartChecks() {
  Pipe a,b;a.peer=&b;b.peer=&a;
  UartSecurity::Endpoint esp(UartSecurity::Esp,nullptr,mac,random),
      teensy(UartSecurity::Teensy,nullptr,mac,random);
  AuthenticatedUart bridge(a,esp), device(b,teensy);
  Receiver atEsp,atTeensy;
  assert(bridge.begin(0,100) && device.begin(9,100));
  uint32_t now=100;
  for (;now<1000 && !bridge.online();++now) {
    bridge.service(now,Receiver::accept,&atEsp);
    device.service(now,Receiver::accept,&atTeensy);
    if(teensy.online()) {HelmetLink::Packet status;status.kind=HelmetLink::Status;status.epoch=9;
      status.length=HelmetLink::StatusBytes;device.queue(status,now);}
  }
  assert(bridge.online() && device.online());
  while(bridge.pending() || device.pending() || !a.incoming.empty() || !b.incoming.empty()) {
    bridge.service(now,Receiver::accept,&atEsp);device.service(now,Receiver::accept,&atTeensy);++now;
  }
  HelmetLink::Packet command;command.kind=HelmetLink::Command;command.epoch=9;command.length=8;
  command.sequence=1;command.ticket=2;HelmetLink::put32(command.payload,3);command.payload[4]=HelmetLink::Expression;
  a.sent.clear();assert(bridge.queue(command,now));assert(!bridge.queue(command,now));
  for(unsigned i=0;i<80;++i,++now) {bridge.service(now,Receiver::accept,&atEsp);device.service(now,Receiver::accept,&atTeensy);}
  assert(atTeensy.packets==1 && atTeensy.latest.sequence==1);
  for(uint8_t byte:a.sent)b.incoming.push_back(byte);
  for(unsigned i=0;i<80;++i,++now) device.service(now,Receiver::accept,&atTeensy);
  assert(atTeensy.packets==1);
  a.blocked=true;assert(bridge.queue(command,now));
  bridge.service(now+AuthenticatedUart::WriteDeadlineMs,Receiver::accept,&atEsp);
  assert(bridge.failed() && !bridge.pending() && !bridge.online());
  a.blocked=false;assert(bridge.begin(0,now));
  a.failed=true;bridge.service(now,Receiver::accept,&atEsp);
  assert(bridge.failed() && !bridge.online());
}
}
int main(){assert(Wireless::Crypto::init());boardChecks();uartChecks();
  std::puts("PASS: explicit S31 research profile rejection and bounded authenticated UART partial-I/O/replay/failure milestone (HOST ONLY)");}

#include "Simulator.hpp"
#include "AuthenticatedUart.hpp"
#include <deque>
#include <vector>
using namespace ProtogenV4;
using namespace V4Test;
using namespace ProtogenResearch;
namespace {
struct Wire final : ByteChannel {
  Wire *peer=nullptr;
  std::deque<uint8_t> incoming;
  std::vector<uint8_t> sent;
  int read(uint8_t *p,size_t capacity) override {
    size_t n=0;while(n<capacity && n<31 && !incoming.empty()){
      p[n++]=incoming.front();incoming.pop_front();}return int(n);
  }
  int write(const uint8_t *p,size_t size) override {
    const size_t n=size<23?size:23;
    for(size_t i=0;i<n;++i){peer->incoming.push_back(p[i]);sent.push_back(p[i]);}
    return int(n);
  }
};
struct WiredHelmet {
  HostCrypto &crypto;
  Wire espIo,teensyIo;
  uint8_t key[32]{13}; // Separate public UART fixture, never the radio PSK.
  UartSecurity::Endpoint esp,teensy;
  AuthenticatedUart bridge,device;
  HelmetLink::Gate gate;
  HelmetLink::Packet latest;
  uint32_t now=0,statusAt=0,ticket=0;
  unsigned actions=0,forwardRejects=0;
  static bool mac(void *context,const uint8_t *p,size_t n,uint8_t out[32]) {
    auto &s=*static_cast<WiredHelmet*>(context);return s.crypto.hmac(s.key,p,n,out);
  }
  static bool random(void *context,uint8_t *p,size_t n) {
    return static_cast<WiredHelmet*>(context)->crypto.random(p,n);
  }
  static void status(void *context,const HelmetLink::Packet &p) {
    if(p.kind==HelmetLink::Status)static_cast<WiredHelmet*>(context)->latest=p;
  }
  static void action(void *context,const HelmetLink::Packet &p) {
    auto &s=*static_cast<WiredHelmet*>(context);
    if(p.kind==HelmetLink::Hello)s.gate.hello(p,s.now);
    else if(p.kind==HelmetLink::Command && s.gate.check(p,s.now)==HelmetLink::Accepted)++s.actions;
  }
  static void forward(void *context,const HelmetLink::Packet &p) {
    auto &s=*static_cast<WiredHelmet*>(context);
    if(!s.bridge.queue(p,s.now))++s.forwardRejects;
  }
  explicit WiredHelmet(HostCrypto &c):crypto(c),
    esp(UartSecurity::Esp,this,mac,random),teensy(UartSecurity::Teensy,this,mac,random),
    bridge(espIo,esp),device(teensyIo,teensy) {
    espIo.peer=&teensyIo;teensyIo.peer=&espIo;reboot(1,100);
  }
  void reboot(uint32_t epoch,uint32_t at) {
    now=at;gate=HelmetLink::Gate{};gate.epoch=epoch;latest={};statusAt=0;
    espIo.incoming.clear();teensyIo.incoming.clear();
    assert(bridge.begin(0,now) && device.begin(epoch,now));
  }
  bool ready() const{return bridge.online() && device.online() && gate.bridge==3;}
  void tick(uint32_t at) {
    now=at;
    bridge.service(now,status,this);device.service(now,action,this);
    if(teensy.online() && now-statusAt>=100 && !device.pending()) {
      HelmetLink::Packet p;p.kind=HelmetLink::Status;p.epoch=gate.epoch;
      p.length=HelmetLink::StatusBytes;p.ticket=++ticket;HelmetLink::put32(p.payload,3);
      gate.issue(p.ticket,now);assert(device.queue(p,now));statusAt=now;
    }
    if(bridge.online() && !gate.bridge && latest.epoch==gate.epoch && latest.ticket && !bridge.pending()) {
      HelmetLink::Packet p;p.kind=HelmetLink::Hello;p.epoch=latest.epoch;p.ticket=latest.ticket;
      p.length=4;HelmetLink::put32(p.payload,3);assert(bridge.queue(p,now));
    }
  }
  HelmetLink::Packet command(uint32_t sequence) const {
    HelmetLink::Packet p;p.kind=HelmetLink::Command;p.sequence=sequence;
    p.epoch=latest.epoch;p.ticket=latest.ticket;p.length=8;
    HelmetLink::put32(p.payload,3);p.payload[4]=HelmetLink::Expression;p.payload[5]=2;
    return p;
  }
};
}
void uartIntegration() {
  Simulation s;assert(s.pair());
  WiredHelmet wired(s.helmet.crypto);
  s.helmet.hooks.forwardContext=&wired;s.helmet.hooks.forward=WiredHelmet::forward;
  auto step=[&]{s.step();wired.tick(uint32_t(s.time.ms));};
  auto until=[&](auto predicate,unsigned limit=10000){
    for(unsigned i=0;i<limit;++i){if(predicate())return true;step();}return predicate();};
  assert(until([&]{return wired.ready();}));
  // Drain handshake output before measuring only the authenticated command.
  assert(until([&]{return !wired.bridge.pending();}));wired.espIo.sent.clear();
  const auto first=wired.command(1);
  assert(until([&]{return s.paw.core.send(first);}));
  assert(until([&]{return wired.actions==1;}));
  const auto oldUart=wired.espIo.sent;
  Received oldRadio;
  for(const auto &f:s.radio.history)
    if(f.frame.bytes[2]==uint8_t(Kind::Data) && f.frame.bytes[3]==Paw)oldRadio=f;
  s.helmet.port.inject(oldRadio);
  for(auto b:oldUart)wired.teensyIo.incoming.push_back(b);
  for(unsigned i=0;i<80;++i)step();
  assert(wired.actions==1);
  const auto installs=s.paw.port.installs;
  s.paw.core.requestRekey();
  assert(until([&]{return s.paw.port.installs>installs && s.paw.core.online() && s.helmet.core.online();}));
  assert(wired.ready()); // Wired authentication and epoch survive radio renewal.
  const auto second=wired.command(2);
  assert(until([&]{return s.paw.core.send(second);}));
  assert(until([&]{return wired.actions==2;}));
  const auto failures=s.helmet.core.telemetry().keygenFailures;
  s.helmet.worker.fail=true;s.paw.core.requestRekey();
  assert(until([&]{return s.helmet.core.telemetry().keygenFailures>failures;}));
  assert(wired.actions==2 && wired.ready());
  assert(until([&]{return s.paw.core.online() && s.helmet.core.online();}));
  // A Teensy reboot changes only the wired epoch, requiring fresh wired auth.
  wired.reboot(2,uint32_t(s.time.ms));
  assert(until([&]{return wired.ready();}));
  const auto rejected=wired.forwardRejects;
  assert(until([&]{return s.paw.core.send(second);}));
  assert(until([&]{return wired.forwardRejects>rejected;}));
  assert(wired.actions==2 && s.helmet.core.online());
  const auto third=wired.command(1);
  assert(until([&]{return s.paw.core.send(third);}));
  assert(until([&]{return wired.actions==3;}));
  for(auto b:oldUart)wired.teensyIo.incoming.push_back(b);
  for(unsigned i=0;i<80;++i)step();
  assert(wired.actions==3);
  s.helmet.hooks.forward=nullptr;s.helmet.hooks.forwardContext=nullptr;
  std::puts("PASS: real v4 pairing/data/rekey/KEM-failure recovery with independent authenticated UART, replay rejection and Teensy epoch reset (HOST ONLY)");
}

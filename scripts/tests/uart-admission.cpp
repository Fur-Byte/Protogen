// No stand-in crypto: both endpoints use the firmware's upstream Mbed TLS
// SHA-256/HMAC subset and the public, test-only fixture key.
#include "UartAuthentication.h"
#include "UartMac.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using HelmetLink::Packet;
using UartSecurity::Endpoint;
struct Context {
  UartSecurity::MbedMac mac;
  uint32_t entropy=1;
  unsigned randomCalls=0,macCalls=0;
  bool failRandom=false;
};
static bool mac(void *ctx,const uint8_t *p,size_t n,uint8_t tag[32]) {
  auto &c=*static_cast<Context*>(ctx);++c.macCalls;
  return c.mac.authenticate(p,n,tag);
}
static bool randomBytes(void *ctx,uint8_t *p,size_t n) {
  auto &c=*static_cast<Context*>(ctx);++c.randomCalls;
  if(c.failRandom)return false;
  while(n--) {*p++=uint8_t(c.entropy);c.entropy=c.entropy*1664525u+1013904223u;}
  return true;
}
using Frame=std::array<uint8_t,UartSecurity::MaxEncoded>;
static bool deliver(Endpoint &target,const Frame &frame,size_t n,uint32_t now) {
  Packet p;bool accepted=false;
  for(size_t i=0;i<n;++i)accepted=target.feed(frame[i],now,p)||accepted;
  return accepted;
}
static bool checkedDeliver(Endpoint &target,Context &context,const Frame &frame,size_t n,uint32_t now) {
  const auto before=context.macCalls;
  const auto accepted=deliver(target,frame,n,now);
  assert(context.macCalls-before<=3); // fixed short HMAC budget per input frame
  return accepted;
}
static size_t poll(Endpoint &e,uint32_t now,Frame &frame) {
  const auto n=e.poll(now,frame.data(),frame.size());assert(n);return n;
}
int main(int argc,char **argv) {
  assert(argc==2);
  const std::string scenario=argv[1];
  const uint32_t base=scenario=="wrap"?UINT32_MAX-1500:100;
  Context receiverContext,legitContext,captureContext;
  legitContext.entropy=42;captureContext.entropy=78;
  Endpoint receiver(UartSecurity::Teensy,&receiverContext,mac,randomBytes);
  Endpoint legit(UartSecurity::Esp,&legitContext,mac,randomBytes);
  Endpoint captured(UartSecurity::Esp,&captureContext,mac,randomBytes);
  assert(receiver.begin(19,base) && legit.begin(0,base) && captured.begin(0,base));
  Frame oldRequest,request,challenge,confirm;
  const auto oldLength=poll(captured,base,oldRequest);
  const auto requestLength=poll(legit,base,request);
  if(scenario=="multiple") {
    Context otherContext;otherContext.entropy=123;
    Endpoint other(UartSecurity::Esp,&otherContext,mac,randomBytes);
    assert(other.begin(0,base));
    Frame otherRequest;
    const auto otherLength=poll(other,base,otherRequest);
    checkedDeliver(receiver,receiverContext,oldRequest,oldLength,base);
    poll(receiver,base,challenge);
    checkedDeliver(receiver,receiverContext,otherRequest,otherLength,base+1);
    poll(receiver,base+1,challenge);
    checkedDeliver(receiver,receiverContext,request,requestLength,base+10);
    const auto n=poll(receiver,base+10,challenge);
    deliver(legit,challenge,n,base+10);
    const auto confirmLength=poll(legit,base+10,confirm);
    checkedDeliver(receiver,receiverContext,confirm,confirmLength,base+10);
  } else if(scenario=="first") {
    checkedDeliver(receiver,receiverContext,oldRequest,oldLength,base);
    poll(receiver,base,challenge);
    checkedDeliver(receiver,receiverContext,request,requestLength,base+10);
    const auto n=poll(receiver,base+10,challenge);
    deliver(legit,challenge,n,base+10);
    const auto confirmLength=poll(legit,base+10,confirm);
    checkedDeliver(receiver,receiverContext,confirm,confirmLength,base+10);
  } else {
    checkedDeliver(receiver,receiverContext,request,requestLength,base);
    const auto n=poll(receiver,base,challenge);
    deliver(legit,challenge,n,base);
    const auto confirmLength=poll(legit,base,confirm);
    // A real authenticated captured Request arrives after the old global
    // limiter expires, while the legitimate Confirm is delayed in transit.
    checkedDeliver(receiver,receiverContext,oldRequest,oldLength,base+1000);
    checkedDeliver(receiver,receiverContext,confirm,confirmLength,base+1001);
  }
  assert(receiver.online());
  Packet status;status.kind=HelmetLink::Status;status.epoch=19;status.length=20;
  auto n=receiver.seal(status,challenge.data(),challenge.size());
  assert(n && deliver(legit,challenge,n,base+1002));
  assert(legit.online());
  // A captured Confirm must not roll counters back in the established
  // session, including after another successful fresh handshake below.
  Frame oldConfirm=confirm;
  // Frame storage is fixed-size; locate its wire terminator without decoding.
  size_t capturedConfirmLength=0;
  while(capturedConfirmLength<oldConfirm.size() && oldConfirm[capturedConfirmLength])++capturedConfirmLength;
  assert(capturedConfirmLength<oldConfirm.size());++capturedConfirmLength;
  Packet initialCommand;initialCommand.kind=HelmetLink::Command;initialCommand.epoch=19;initialCommand.length=8;
  auto capturedDataLength=legit.seal(initialCommand,request.data(),request.size());
  assert(capturedDataLength && checkedDeliver(receiver,receiverContext,request,capturedDataLength,base+1003));
  checkedDeliver(receiver,receiverContext,oldConfirm,capturedConfirmLength,base+1004);
  assert(!checkedDeliver(receiver,receiverContext,request,capturedDataLength,base+1005));
  // Repeat the captured Request through many admission windows while current
  // authenticated application traffic continues to succeed.
  Packet command;command.kind=HelmetLink::Command;command.epoch=19;command.length=8;
  const auto randomBefore=receiverContext.randomCalls;
  for(uint32_t ms=1100;ms<11100;++ms) {
    checkedDeliver(receiver,receiverContext,oldRequest,oldLength,base+ms);
    receiver.poll(base+ms,challenge.data(),challenge.size());
    if(ms%100==0) {
      n=legit.seal(command,request.data(),request.size());
      assert(n && checkedDeliver(receiver,receiverContext,request,n,base+ms));
    }
  }
  assert(receiverContext.randomCalls-randomBefore<=6);
  // Renew repeatedly while a captured Request arrives on every service tick.
  // Alternate replay-first and fresh-first arrival; lose the first legitimate
  // Challenge so the same-nonce retry path also has to recover.
  for(uint32_t attempt=0;attempt<40;++attempt) {
    const uint32_t at=base+12000+attempt*3000;
    assert(legit.begin(0,at));
    const auto freshLength=poll(legit,at,request);
    if(attempt&1) {
      checkedDeliver(receiver,receiverContext,oldRequest,oldLength,at);
      receiver.poll(at,challenge.data(),challenge.size());
    }
    checkedDeliver(receiver,receiverContext,request,freshLength,at+1);
    poll(receiver,at+1,challenge); // deliberately lost
    for(uint32_t dt=2;dt<=1001;++dt) {
      checkedDeliver(receiver,receiverContext,oldRequest,oldLength,at+dt);
      receiver.poll(at+dt,challenge.data(),challenge.size());
    }
    checkedDeliver(receiver,receiverContext,request,freshLength,at+1002);
    auto length=poll(receiver,at+1002,challenge);
    deliver(legit,challenge,length,at+1002);
    length=poll(legit,at+1002,confirm);
    checkedDeliver(receiver,receiverContext,oldRequest,oldLength,at+1003);
    checkedDeliver(receiver,receiverContext,confirm,length,at+1003);
    assert(receiver.online());
    length=receiver.seal(status,challenge.data(),challenge.size());
    assert(length && deliver(legit,challenge,length,at+1003));
    assert(legit.online());
    checkedDeliver(receiver,receiverContext,oldConfirm,capturedConfirmLength,at+1004);
    length=legit.seal(command,request.data(),request.size());
    assert(length && checkedDeliver(receiver,receiverContext,request,length,at+1005));
  }
  // Keep the original exact CandidateMs expiry, including timer rollover.
  for(uint32_t delay : {0u,UartSecurity::CandidateMs-1,UartSecurity::CandidateMs}) {
    Context expiryReceiverContext,expiryClientContext;
    Endpoint expiryReceiver(UartSecurity::Teensy,&expiryReceiverContext,mac,randomBytes);
    Endpoint expiryClient(UartSecurity::Esp,&expiryClientContext,mac,randomBytes);
    const auto at=base+150000;
    assert(expiryReceiver.begin(25,at) && expiryClient.begin(0,at));
    auto length=poll(expiryClient,at,request);
    deliver(expiryReceiver,request,length,at);
    length=poll(expiryReceiver,at,challenge);
    deliver(expiryClient,challenge,length,at);
    length=poll(expiryClient,at,confirm);
    deliver(expiryReceiver,confirm,length,at+delay);
    assert(expiryReceiver.online()==(delay<UartSecurity::CandidateMs));
    assert(expiryReceiver.begin(25,at+delay+1));
    deliver(expiryReceiver,confirm,length,at+delay+2);
    assert(!expiryReceiver.online()); // same-epoch reboot also needs new seed

  }
  // A failed entropy source stays fail-closed and is retried at most once a
  // second; replay traffic must not cause unbounded RNG calls during recovery.
  Context failedRandomContext;failedRandomContext.failRandom=true;
  Endpoint failedRandomReceiver(UartSecurity::Teensy,&failedRandomContext,mac,randomBytes);
  const auto failureAt=base+160000;
  assert(failedRandomReceiver.begin(29,failureAt));
  for(uint32_t ms=0;ms<3000;++ms) {
    checkedDeliver(failedRandomReceiver,failedRandomContext,oldRequest,oldLength,failureAt+ms);
    assert(!failedRandomReceiver.online());
    assert(!failedRandomReceiver.poll(failureAt+ms,challenge.data(),challenge.size()));
  }
  assert(failedRandomContext.randomCalls<=3);
  failedRandomContext.failRandom=false;
  checkedDeliver(failedRandomReceiver,failedRandomContext,oldRequest,oldLength,failureAt+3000);
  n=poll(failedRandomReceiver,failureAt+3000,challenge);
  deliver(captured,challenge,n,failureAt+3000);
  n=poll(captured,failureAt+3000,confirm);
  checkedDeliver(failedRandomReceiver,failedRandomContext,confirm,n,failureAt+3001);
  assert(failedRandomReceiver.online());
  // Old genuine Data is still rejected across a new session and reboot epoch.
  n=legit.seal(command,request.data(),request.size());assert(n);
  assert(checkedDeliver(receiver,receiverContext,request,n,base+140000));
  assert(!checkedDeliver(receiver,receiverContext,request,n,base+140001));
  assert(receiver.begin(20,base+141000));
  assert(!checkedDeliver(receiver,receiverContext,request,n,base+141001));
  // An uncommitted Confirm must not regain freshness after a full millis era.
  // poll/feed observe the wrap during ordinary continuous controller service.
  Context longReceiverContext,longClientContext;
  Endpoint longReceiver(UartSecurity::Teensy,&longReceiverContext,mac,randomBytes);
  Endpoint longClient(UartSecurity::Esp,&longClientContext,mac,randomBytes);
  assert(longReceiver.begin(31,100)&&longClient.begin(0,100));
  auto length=poll(longClient,100,request);
  deliver(longReceiver,request,length,100);
  length=poll(longReceiver,100,challenge);
  deliver(longClient,challenge,length,100);
  length=poll(longClient,100,confirm);
  longReceiver.poll(UINT32_MAX-100,challenge.data(),challenge.size());
  longReceiver.poll(200,challenge.data(),challenge.size());
  checkedDeliver(longReceiver,longReceiverContext,confirm,length,200);
  assert(!longReceiver.online());
  std::cout<<"PASS: UART "<<scenario<<" stateless Request replay, active-session preservation, bounded seed RNG, exact TTL, era/epoch/Data rejection\n";
}

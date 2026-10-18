#pragma once
#include "UartAuthentication.h"
namespace ProtogenResearch {
struct ByteChannel {
  virtual ~ByteChannel()=default;
  // Nonblocking: 0 means no progress; a negative value is a transport fault.
  virtual int read(uint8_t *,size_t)=0;
  virtual int write(const uint8_t *,size_t)=0;
};
// Fixed, single-frame UART milestone for an isolated platform adapter. Reuses
// the unchanged authentication/framing protocol; it owns no authentication key.
class AuthenticatedUart {
  ByteChannel &channel;
  UartSecurity::Endpoint &endpoint;
  uint8_t tx[UartSecurity::MaxEncoded]{};
  size_t length=0, sent=0;
  uint32_t queuedAt=0;
  bool fault=false;
  void fail() {
    endpoint.clear();protogenSecureZero(tx,sizeof(tx));length=sent=0;fault=true;
  }
public:
  static constexpr uint32_t WriteDeadlineMs=100;
  static constexpr size_t ReadBudget=64,WriteBudget=32;
  using Handler=void (*)(void *,const HelmetLink::Packet &);
  AuthenticatedUart(ByteChannel &io,UartSecurity::Endpoint &auth):channel(io),endpoint(auth){}
  ~AuthenticatedUart(){endpoint.clear();protogenSecureZero(tx,sizeof(tx));}
  AuthenticatedUart(const AuthenticatedUart&)=delete;
  AuthenticatedUart &operator=(const AuthenticatedUart&)=delete;
  bool begin(uint32_t epoch,uint32_t now) {
    protogenSecureZero(tx,sizeof(tx));length=sent=0;
    fault=!endpoint.begin(epoch,now);return !fault;
  }
  bool failed() const{return fault;}
  bool online() const{return !fault && endpoint.online();}
  bool pending() const{return length!=0;}
  bool queue(const HelmetLink::Packet &p,uint32_t now) {
    if(fault || length)return false;
    length=endpoint.seal(p,tx,sizeof(tx));sent=0;queuedAt=now;
    return length!=0;
  }
  void service(uint32_t now,Handler handler,void *context) {
    if(fault)return;
    uint8_t input[ReadBudget]{};
    const int received=channel.read(input,sizeof(input));
    if(received<0 || size_t(received)>sizeof(input)){fail();return;}
    for(int i=0;i<received;++i) {
      HelmetLink::Packet packet;
      if(endpoint.feed(input[i],now,packet) && handler)handler(context,packet);
    }
    if(length && now-queuedAt>=WriteDeadlineMs){fail();return;}
    if(!length){length=endpoint.poll(now,tx,sizeof(tx));sent=0;queuedAt=now;}
    if(!length)return;
    const size_t left=length-sent,amount=left<WriteBudget?left:WriteBudget;
    const int written=channel.write(tx+sent,amount);
    if(written<0 || size_t(written)>amount){fail();return;}
    sent+=size_t(written);
    if(sent==length){protogenSecureZero(tx,sizeof(tx));sent=length=0;}
  }
};
} // namespace ProtogenResearch

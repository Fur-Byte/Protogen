#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Little-endian application and UART framing. The ESP32 wireless layer owns
// radio authentication, encryption, session establishment and replay checks.
namespace HelmetLink {
constexpr uint8_t Version = 1;
constexpr size_t MaxPayload = 72, HeaderBytes = 16, MaxRaw = HeaderBytes + MaxPayload + 2;
constexpr size_t MaxEncoded = MaxRaw + 2;
constexpr uint32_t FreshMs = 300, LinkTimeoutMs = 2000;
enum Kind : uint8_t { Command = 1, Status = 2, Ack = 3, Hello = 4, Heartbeat = 5 };
enum Action : uint8_t { Expression = 1, Boop = 2, Sleep = 3, MicControl = 4 };
enum Result : uint8_t { Accepted, Duplicate, BadSession, Expired, Invalid, RateLimited, Unavailable };
inline uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put16(uint8_t *p, uint16_t x) { p[0] = uint8_t(x); p[1] = uint8_t(x >> 8); }
inline void put32(uint8_t *p, uint32_t x) { for (unsigned i=0; i<4; ++i) p[i] = uint8_t(x >> (i*8)); }
inline uint16_t crc16(const uint8_t *p, size_t n) {
  uint16_t crc = 0xffff;
  while (n--) { crc ^= uint16_t(*p++) << 8; for (unsigned b=0;b<8;++b) crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0)); }
  return crc;
}
struct Packet {
  uint8_t kind = 0, length = 0;
  uint32_t sequence = 0, epoch = 0, ticket = 0;
  uint8_t payload[MaxPayload] = {};
};
inline size_t pack(const Packet &p, uint8_t *out, size_t capacity) {
  const size_t n = HeaderBytes + p.length + 2;
  if (p.length > MaxPayload || capacity < n || p.kind < Command || p.kind > Heartbeat) return 0;
  out[0]=0x50; out[1]=Version; out[2]=p.kind; out[3]=p.length;
  put32(out+4,p.sequence); put32(out+8,p.epoch); put32(out+12,p.ticket);
  memcpy(out+16,p.payload,p.length); put16(out+n-2,crc16(out,n-2)); return n;
}
inline bool unpack(const uint8_t *in, size_t n, Packet &p) {
  if (n < HeaderBytes+2 || n > MaxRaw || in[0]!=0x50 || in[1]!=Version ||
      in[2]<Command || in[2]>Heartbeat || in[3]>MaxPayload || n!=HeaderBytes+in[3]+2 ||
      u16(in+n-2)!=crc16(in,n-2)) return false;
  p = Packet{}; p.kind=in[2]; p.length=in[3]; p.sequence=u32(in+4);
  p.epoch=u32(in+8); p.ticket=u32(in+12); memcpy(p.payload,in+16,p.length); return true;
}
inline size_t cobsEncode(const uint8_t *in,size_t n,uint8_t *out,size_t capacity) {
  // The minimum includes the initial code and delimiter. Additional code
  // bytes are checked when reserved; avoid overflowing n + overhead.
  if (!out || (!in && n) || capacity < 2 || n > capacity - 2) return 0;
  size_t codeAt=0, used=1; uint8_t code=1;
  for(size_t i=0;i<n;++i) {
    if(in[i]==0) {
      out[codeAt]=code;
      if(used>=capacity)return 0;
      codeAt=used++; code=1;
    } else {
      if(used>=capacity)return 0;
      out[used++]=in[i];
      if(++code==0xff) {
        out[codeAt]=code;
        if(used>=capacity)return 0;
        codeAt=used++;code=1;
      }
    }
  }
  out[codeAt]=code; if(used>=capacity)return 0; out[used++]=0; return used;
}
inline size_t cobsDecode(const uint8_t *in,size_t n,uint8_t *out,size_t capacity) {
  if ((!in && n) || !out) return 0;
  size_t used=0, i=0;
  while(i<n) {
    const uint8_t code=in[i++]; if(!code || size_t(code-1)>n-i || size_t(code-1)>capacity-used) return 0;
    for(unsigned j=1;j<code;++j) { if(!in[i]) return 0; out[used++]=in[i++]; }
    if(code!=0xff && i<n) {if(used==capacity)return 0;out[used++]=0;}
  }
  return used;
}
class Decoder {
  uint8_t encoded[MaxEncoded] = {}; size_t used=0; uint32_t started=0; bool discard=false;
public:
  uint32_t errors=0;
  bool feed(uint8_t b,uint32_t now,Packet &p) {
    if(used && now-started>100) {used=0;discard=true;++errors;}
    if(!b) {
      uint8_t raw[MaxRaw];
      const size_t n=discard ? 0 : cobsDecode(encoded,used,raw,sizeof(raw));
      const bool okay=n && unpack(raw,n,p);
      if(!okay && (used || discard)) ++errors;
      used=0;discard=false;return okay;
    }
    if(!used) started=now;
    if(used==sizeof(encoded)) {used=0;discard=true;++errors;}
    if(!discard) encoded[used++]=b;
    return false;
  }
};
// A receiver-generated ticket binds an action to at most 300 ms of helmet time.
// This catches delayed authentic commands, including UART congestion, without
// trusting a transmitter timestamp or inventing clock synchronization.
class Gate {
  struct Ticket { uint32_t value=0, at=0; } tickets[4];
  unsigned cursor=0, count=0; uint32_t periodAt=0, lastSequence=0;
  bool haveSequence=false;
  uint8_t lastPayload[8]{};uint32_t lastTicket=0;
public:
  uint32_t epoch=0, bridge=0, rejected=0;
  void issue(uint32_t value,uint32_t now) {tickets[cursor]={value,now};cursor=(cursor+1)%4;}
  bool fresh(uint32_t value,uint32_t now) const {
    if(!value)return false;
    for(const auto &t:tickets)if(t.value==value && now-t.at<=FreshMs)return true;
    return false;
  }
  bool hello(const Packet &p,uint32_t now) {
    if(p.kind!=Hello || p.length!=4 || p.epoch!=epoch || !epoch || !fresh(p.ticket,now) || !u32(p.payload))return false;
    const uint32_t next=u32(p.payload);
    if(next!=bridge) {bridge=next;haveSequence=false;lastSequence=0;count=0;periodAt=now;}
    return true;
  }
  Result check(const Packet &p,uint32_t now) {
    if(p.kind!=Command || p.length!=8 || !p.sequence || !p.epoch || p.epoch!=epoch ||
       !bridge || u32(p.payload)!=bridge) return fail(BadSession);
    // Deduplicate before expiry so a lost ACK can be repeated without an action.
    if(haveSequence && p.sequence==lastSequence)
      return p.ticket==lastTicket && !memcmp(p.payload,lastPayload,8)?Duplicate:fail(Invalid);
    if(haveSequence && p.sequence<lastSequence)return fail(Expired);
    if(!fresh(p.ticket,now))return fail(Expired);
    const uint8_t action=p.payload[4], value=p.payload[5];
    const uint16_t duration=u16(p.payload+6);
    if(action<Expression || action>MicControl ||
       (action==Expression && value>=6) ||
       ((action==Sleep || action==MicControl) && value>1) ||
       (action==Boop && (value!=0 || duration<100 || duration>6000)) ||
       (action!=Boop && duration!=0))return fail(Invalid);
    if(now-periodAt>=1000){periodAt=now;count=0;}
    if(count>=20)return fail(RateLimited);
    ++count;lastSequence=p.sequence;lastTicket=p.ticket;memcpy(lastPayload,p.payload,8);haveSequence=true;return Accepted;
  }
private:
  Result fail(Result r){++rejected;return r;}
};
constexpr size_t StatusBytes=20;
constexpr size_t ExtendedStatusBytes=StatusBytes+4;
enum StatusFlag : uint8_t { Sleeping=1, Visible=2, Booping=4, Speaking=8, MicEnabled=16, DisplayReady=32, PawOnline=64 };
// Both the radio record validator and the paw application consume this schema.
inline bool validHelmetStatus(const Packet &p) {
  return p.kind==Status &&
         (p.length==StatusBytes ||
          (p.length==ExtendedStatusBytes &&
           (u16(p.payload+StatusBytes)==0xffff || u16(p.payload+StatusBytes)<=100))) &&
         p.epoch && u32(p.payload) && p.payload[4]<6 &&
         !(p.payload[5]&0x80) && p.payload[19]==0;
}
}

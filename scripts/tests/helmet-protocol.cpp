#include "HelmetProtocol.h"
#include "SensorMath.h"
#include "DeviceConfig.h"
#include <cassert>
#include <iostream>
#include <random>
using namespace HelmetLink;
int main() {
  assert(crc16(reinterpret_cast<const uint8_t*>("123456789"),9)==0x29b1);
  std::mt19937 rng(13);
  for(unsigned run=0;run<20000;++run) {
    Packet p;p.kind=1+rng()%5;p.length=rng()%73;p.sequence=rng();p.epoch=rng();p.ticket=rng();
    for(auto &b:p.payload)b=rng();
    uint8_t raw[MaxRaw],encoded[MaxEncoded],decoded[MaxRaw];const size_t n=pack(p,raw,sizeof(raw));
    const size_t m=cobsEncode(raw,n,encoded,sizeof(encoded));assert(m && encoded[m-1]==0);
    assert(cobsDecode(encoded,m-1,decoded,sizeof(decoded))==n && !memcmp(raw,decoded,n));
    Packet q;assert(unpack(decoded,n,q));
    Decoder decoder;bool found=false;for(size_t i=0;i<m;++i)found=decoder.feed(encoded[i],run,q)||found;
    assert(found && q.epoch==p.epoch && q.length==p.length && !memcmp(q.payload,p.payload,p.length));
    raw[rng()%n]^=1;assert(!unpack(raw,n,q));
    for(size_t cut=0;cut<n;++cut)assert(!unpack(decoded,cut,q));
  }
  Decoder d;Packet q;
  for(unsigned i=0;i<5000;++i)d.feed(1,0,q);
  assert(!d.feed(0,0,q) && d.errors);
  const uint8_t bad[]={2,0};assert(cobsDecode(bad,2,reinterpret_cast<uint8_t*>(&q),sizeof(q))==0);
  Gate g;g.epoch=99;g.issue(1,100);
  Packet hello;hello.kind=Hello;hello.length=4;hello.epoch=99;hello.ticket=1;put32(hello.payload,12);
  assert(g.hello(hello,101));
  Packet c;c.kind=Command;c.length=8;c.epoch=99;c.ticket=1;c.sequence=1;put32(c.payload,12);c.payload[4]=Expression;c.payload[5]=4;
  assert(g.check(c,102)==Accepted);
  assert(g.check(c,1000)==Duplicate); // A lost ACK doesn't execute it twice, even later.
  c.payload[5]=2;assert(g.check(c,103)==Invalid);c.payload[5]=4;
  c.sequence=2;assert(g.check(c,401)==Expired);assert(g.check(c,400)==Accepted);
  c.sequence=1;assert(g.check(c,105)==Expired);c.sequence=3;c.epoch=98;assert(g.check(c,105)==BadSession);c.epoch=99;
  c.payload[4]=Boop;put16(c.payload+6,1200);assert(g.check(c,105)==Invalid);c.payload[5]=0;
  assert(g.check(c,105)==Accepted);
  c.sequence=4;c.payload[4]=Expression;put16(c.payload+6,0);c.payload[5]=6;assert(g.check(c,106)==Invalid);
  c.payload[5]=0;
  for(unsigned i=4;i<=20;++i){c.sequence=i;assert(g.check(c,106)==Accepted);}
  c.sequence=21;assert(g.check(c,106)==RateLimited);
  g.issue(2,1200);c.ticket=2;assert(g.check(c,1201)==Accepted);
  put32(hello.payload,13);hello.ticket=2;assert(g.hello(hello,1202));c.sequence=1;put32(c.payload,13);assert(g.check(c,1202)==Accepted);
  g.issue(3,0xfffffff0);assert(g.fresh(3,32));assert(!g.fresh(3,400));
  uint8_t sample[6]={0x66,0x66,0,0x80,0,0};sample[2]=SensorMath::shtCrc(sample,2);sample[5]=SensorMath::shtCrc(sample+3,2);
  float t,h;assert(SensorMath::sht40(sample,t,h) && t>24.9 && t<25.1 && h>56 && h<57);
  sample[0]^=1;assert(!SensorMath::sht40(sample,t,h));
  const uint8_t positive[]={0x00,0x10,0x00},negative[]={0xff,0xf0,0x00};
  assert(SensorMath::signedCurrent(positive)==256 && SensorMath::signedCurrent(negative)==-256);
  assert(SensorMath::ntc(1320,t) && t>24.9 && t<25.1);assert(!SensorMath::ntc(3300,t));
  assert(SensorMath::percent(30000,60000)==50 && SensorMath::percent(70000,60000)==0 && SensorMath::percent(0,0)==-1);
  Device::Config config;config.paired=true;config.psk[0]=1;config.usableEnergyMWh=60000;
  uint8_t b[Device::ConfigBytes];Device::encode(config,b);Device::Config loaded;assert(Device::decode(b,sizeof(b),loaded) && loaded.paired);
  b[4]^=1;assert(!Device::decode(b,sizeof(b),loaded) && !loaded.paired);
  memset(b+8,0,32);put16(b+44,crc16(b,44));assert(!Device::decode(b,sizeof(b),loaded) && loaded.psk[0]==0);
  std::cout<<"PASS: 20000 framed packets, truncation/corruption/overflow, freshness, replay/dedup, epoch/session, rate limit, sensor CRC/conversions, provisioning records\n";
}

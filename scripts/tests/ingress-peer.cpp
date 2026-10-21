#include "WirelessTransport.h"
#include <cassert>
#include <iostream>
using namespace Wireless;
// Allows this same behavior reproducer to run against main's old four-argument
// boundary. It intentionally demonstrates the missing source identity check.
template<class... Args>
auto admission(int,Args... args)->decltype(admitIngress(args...)) {
  return admitIngress(args...);
}
template<class Bytes>
bool admission(long,Bytes b,size_t n,bool broadcast,bool operational,
               const Mac &,const Mac &) {
  return admitIngress(b,n,broadcast,operational);
}
int main() {
  const Mac peer{{2,3,4,5,6,7}},foreign{{2,8,9,10,11,12}};
  Frame f;header(f,Data,Paw);
  assert(admission(0,f.bytes,PacketBytes,false,true,peer,peer));
  const bool accepted=admission(0,f.bytes,PacketBytes,false,true,foreign,peer);
  std::cerr<<"foreign-source Data admitted before RX queue="<<accepted<<'\n';
  assert(!accepted);
  assert(!admission(0,f.bytes,PacketBytes,true,true,peer,peer));
  assert(!admission(0,f.bytes,PacketBytes,false,false,peer,peer));
  header(f,SessionRequest,Paw);
  assert(admission(0,f.bytes,PacketBytes,true,false,foreign,Mac{}));
  assert(!admission(0,f.bytes,PacketBytes,true,true,peer,peer));
  f.bytes[1]=4;assert(!admission(0,f.bytes,PacketBytes,true,false,peer,peer));
  assert(!admission(0,f.bytes,PacketBytes-1,false,false,peer,peer));
  assert(!admission(0,static_cast<const uint8_t*>(nullptr),PacketBytes,false,true,peer,peer));
  IngressIdentity identity;identity.enabled=true;identity.generation=12;
  Received queued;queued.ingressGeneration=11;
  assert(!currentIngress(queued,identity));
  queued.ingressGeneration=12;assert(currentIngress(queued,identity));
  identity.enabled=false;assert(!currentIngress(queued,identity));
  std::cout<<"PASS: operational source filtering before queue, recovery/pairing policy and malformed headers\n";
}

#include "SecureSession.h"
#include <cassert>
#include <deque>
#include <vector>
#include <string.h>
#include <iostream>
namespace {
uint32_t now=0;
struct Endpoint {
  Endpoint *peer=nullptr;
  std::deque<std::vector<uint8_t>> queue;
  std::vector<uint8_t> last;
  unsigned drop=0;bool blocked=false;
  static int send(void *v,const unsigned char *data,size_t n) {
    auto &e=*static_cast<Endpoint*>(v);assert(n<=240);
    if(e.blocked)return MBEDTLS_ERR_SSL_WANT_WRITE;
    e.last.assign(data,data+n);
    if(e.drop)--e.drop;else e.peer->queue.push_back(e.last);
    return n;
  }
  static int receive(void *v,unsigned char *data,size_t n) {
    auto &e=*static_cast<Endpoint*>(v);if(e.queue.empty())return MBEDTLS_ERR_SSL_WANT_READ;
    const auto d=e.queue.front();e.queue.pop_front();assert(d.size()<=n);memcpy(data,d.data(),d.size());return d.size();
  }
  static uint32_t clock(void*){return now;}
  SecureSession::Transport transport(){return {this,send,receive,clock};}
};
bool handshake(SecureSession &client,SecureSession &server) {
  for(unsigned i=0;i<2000;++i) {
    for(auto s:{&client,&server})if(!s->ready()) {
      const int r=s->step();if(r && !SecureSession::pending(r))return false;
    }
    if(client.ready() && server.ready())return true;
    now+=5;
  }
  return false;
}
int read(SecureSession &s,uint8_t b[128]) {
  for(unsigned i=0;i<50;++i) {const int r=s.read(b,128);if(!SecureSession::pending(r))return r;now+=5;}
  return MBEDTLS_ERR_SSL_WANT_READ;
}
}
int main() {
  uint8_t key[32],mac[6]={2,3,4,5,6,7};for(unsigned i=0;i<32;++i)key[i]=i+1;
  Endpoint a,b;a.peer=&b;b.peer=&a;
  SecureSession client,server;assert(client.begin(false,key,mac,a.transport()));assert(server.begin(true,key,mac,b.transport()));
  a.drop=1;assert(handshake(client,server)); // Library retries a lost ClientHello.
  uint8_t message[72];memset(message,0x31,sizeof(message));uint8_t buffer[128];
  a.blocked=true;assert(SecureSession::pending(client.write(message,sizeof(message))));
  a.blocked=false;assert(client.write(message,sizeof(message))==sizeof(message));
  assert(read(server,buffer)==sizeof(message) && !memcmp(message,buffer,sizeof(message)));
  const auto old=a.last;b.queue.push_back(old);assert(SecureSession::pending(read(server,buffer))); // Record replay.
  assert(client.write(message,sizeof(message))==sizeof(message));auto changed=b.queue.front();b.queue.pop_front();changed.back()^=1;b.queue.push_back(changed);
  assert(SecureSession::pending(read(server,buffer))); // Bad authentication tag.
  assert(client.write(message,sizeof(message))==sizeof(message));assert(read(server,buffer)==sizeof(message));
  assert(client.reset() && server.reset());a.queue.clear();b.queue.clear();assert(handshake(client,server));
  b.queue.push_back(old);assert(SecureSession::pending(read(server,buffer))); // Old session ciphertext fails.
  assert(client.write(message,sizeof(message))==sizeof(message));assert(read(server,buffer)==sizeof(message));
  Endpoint c,d;c.peer=&d;d.peer=&c;SecureSession wrongClient,wrongServer;key[0]^=1;
  assert(wrongClient.begin(false,key,mac,c.transport()));key[0]^=1;assert(wrongServer.begin(true,key,mac,d.transport()));
  assert(!handshake(wrongClient,wrongServer));
  std::cout<<"PASS: real Mbed TLS DTLS handshake/cookies, loss recovery, nonblocking writes, full-tag authentication, record replay, new-session isolation, wrong-key rejection\n";
}

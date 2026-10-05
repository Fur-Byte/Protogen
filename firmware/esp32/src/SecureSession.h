#pragma once
#include <stddef.h>
#include <stdint.h>
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_cookie.h"

// A small adapter around the library's DTLS implementation, not a handshake or
// cryptographic construction. BIO callbacks MUST be nonblocking datagram I/O.
class SecureSession {
public:
  struct Transport {
    void *context;
    int (*send)(void *, const unsigned char *, size_t);
    int (*receive)(void *, unsigned char *, size_t);
    uint32_t (*clock)(void *);
  };
  SecureSession();
  ~SecureSession();
  SecureSession(const SecureSession &)=delete;
  SecureSession &operator=(const SecureSession &)=delete;
  bool begin(bool server,const uint8_t psk[32],const uint8_t peer[6],Transport io);
  bool reset();
  int step();
  bool ready() const;
  int read(uint8_t *buffer,size_t capacity);
  int write(const uint8_t *buffer,size_t length);
  static bool pending(int result);
private:
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  mbedtls_ssl_cookie_ctx cookies;
  Transport transport{};
  uint8_t peerMac[6]{};
  uint32_t timerStarted=0,intermediate=0,final=0;
  bool serverMode=false,initialized=false;
  static void setTimer(void *,uint32_t,uint32_t);
  static int getTimer(void *);
};

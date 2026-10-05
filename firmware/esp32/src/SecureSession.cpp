#include "SecureSession.h"
#include <string.h>
#include "psa/crypto.h"

#if !defined(MBEDTLS_SSL_PROTO_DTLS) || !defined(MBEDTLS_SSL_DTLS_ANTI_REPLAY) || !defined(MBEDTLS_SSL_DTLS_HELLO_VERIFY)
#error "DTLS, record replay protection and cookie verification are required"
#endif
namespace {
const int Suites[]={MBEDTLS_TLS_PSK_WITH_AES_128_CCM,0}; // Full 128-bit authentication tag.
const uint8_t Identity[]="protogen-paw-v1";
}
SecureSession::SecureSession() {
  mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_ssl_cookie_init(&cookies);
}
SecureSession::~SecureSession() {
  mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_ssl_cookie_free(&cookies);
}
bool SecureSession::begin(bool server,const uint8_t psk[32],const uint8_t peer[6],Transport io) {
  transport=io;serverMode=server;memcpy(peerMac,peer,6);
  if(psa_crypto_init()!=PSA_SUCCESS)return false;
  if(mbedtls_ssl_config_defaults(&config,server?MBEDTLS_SSL_IS_SERVER:MBEDTLS_SSL_IS_CLIENT,
      MBEDTLS_SSL_TRANSPORT_DATAGRAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)return false;
  mbedtls_ssl_conf_min_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_ciphersuites(&config,Suites);
  mbedtls_ssl_conf_dtls_anti_replay(&config,MBEDTLS_SSL_ANTI_REPLAY_ENABLED);
  mbedtls_ssl_conf_dtls_badmac_limit(&config,16);
  mbedtls_ssl_conf_handshake_timeout(&config,250,1000);
  if(mbedtls_ssl_conf_psk(&config,psk,32,Identity,sizeof(Identity)-1)!=0)return false;
  if(server) {
    if(mbedtls_ssl_cookie_setup(&cookies)!=0)return false;
    mbedtls_ssl_conf_dtls_cookies(&config,mbedtls_ssl_cookie_write,mbedtls_ssl_cookie_check,&cookies);
  }
  if(mbedtls_ssl_setup(&ssl,&config)!=0)return false;
  initialized=true;return reset();
}
bool SecureSession::reset() {
  if(!initialized || mbedtls_ssl_session_reset(&ssl)!=0)return false;
  final=intermediate=0;
  mbedtls_ssl_set_bio(&ssl,transport.context,transport.send,transport.receive,nullptr);
  mbedtls_ssl_set_timer_cb(&ssl,this,setTimer,getTimer);
  mbedtls_ssl_set_mtu(&ssl,240);
  return !serverMode || mbedtls_ssl_set_client_transport_id(&ssl,peerMac,6)==0;
}
void SecureSession::setTimer(void *p,uint32_t mid,uint32_t end) {
  auto &s=*static_cast<SecureSession*>(p);s.timerStarted=s.transport.clock(s.transport.context);
  s.intermediate=mid;s.final=end;
}
int SecureSession::getTimer(void *p) {
  auto &s=*static_cast<SecureSession*>(p);if(!s.final)return -1;
  const uint32_t elapsed=s.transport.clock(s.transport.context)-s.timerStarted;
  return elapsed>=s.final?2:elapsed>=s.intermediate?1:0;
}
bool SecureSession::pending(int r) {return r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE;}
bool SecureSession::ready() const {return initialized && mbedtls_ssl_is_handshake_over(const_cast<mbedtls_ssl_context*>(&ssl));}
int SecureSession::step() {
  if(!initialized)return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  if(ready())return 0;
  const int r=mbedtls_ssl_handshake_step(&ssl);
  if(r==MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED)return reset()?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  return r;
}
int SecureSession::read(uint8_t *b,size_t n) {return mbedtls_ssl_read(&ssl,b,n);}
int SecureSession::write(const uint8_t *b,size_t n) {return mbedtls_ssl_write(&ssl,b,n);}

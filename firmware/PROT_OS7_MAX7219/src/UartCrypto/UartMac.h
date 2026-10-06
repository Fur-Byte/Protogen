#pragma once
#ifdef PROTOGEN_UART_AUTH
#ifdef MBEDTLS_CONFIG_FILE
#undef MBEDTLS_CONFIG_FILE
#endif
#define MBEDTLS_CONFIG_FILE "UartCryptoConfig.h"
#include "../../mbedtls/md.h"
#include "../HelmetProtocol/SecureZero.h"
#include <ProtogenUartKey.h>
namespace UartSecurity {
class MbedMac {
  mbedtls_md_context_t md{};
  bool initialized = false;

public:
  ~MbedMac() { mbedtls_md_free(&md); }
  MbedMac() = default;
  MbedMac(const MbedMac &) = delete;
  bool authenticate(const uint8_t *input, size_t n, uint8_t out[32]) {
    if (!initialized) {
      mbedtls_md_init(&md);
      if (mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                           1))
        return false;
      initialized = true;
    }
    const bool okay =
        !mbedtls_md_hmac_starts(&md, PROTOGEN_UART_AUTH_KEY, 32) &&
        !mbedtls_md_hmac_update(&md, input, n) &&
        !mbedtls_md_hmac_finish(&md, out);
    if (!okay)
      protogenSecureZero(out, 32);
    return okay;
  }
};
} // namespace UartSecurity
#endif

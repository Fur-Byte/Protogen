#ifdef PROTOGEN_UART_AUTH
#ifdef MBEDTLS_CONFIG_FILE
#undef MBEDTLS_CONFIG_FILE
#endif
#define MBEDTLS_CONFIG_FILE "UartCryptoConfig.h"
#include "platform_util.inc"
#endif

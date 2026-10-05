#pragma once
// Same protocol, suite, replay and cookie requirements as the ESP-IDF build;
// portable software PSA back-end lets this test run without radio hardware.
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_COOKIE_C
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_DTLS
#define MBEDTLS_SSL_DTLS_ANTI_REPLAY
#define MBEDTLS_SSL_DTLS_HELLO_VERIFY
#define MBEDTLS_SSL_CIPHERSUITES MBEDTLS_TLS_PSK_WITH_AES_128_CCM
#define MBEDTLS_SSL_IN_CONTENT_LEN 1024
#define MBEDTLS_SSL_OUT_CONTENT_LEN 1024
#define MBEDTLS_PSK_MAX_LEN 32

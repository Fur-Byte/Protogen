#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTLS_SOURCE="${MBEDTLS_SOURCE:-$ROOT/build/platformio/packages/framework-espidf/components/mbedtls/mbedtls}"
DTLS_CMAKE="${CMAKE:-$ROOT/build/platformio/packages/tool-cmake/bin/cmake}"
"$DTLS_CMAKE" -S "$ROOT/scripts/tests/dtls" -B "$ROOT/build/dtls-test" -DMBEDTLS_SOURCE="$DTLS_SOURCE" -DCMAKE_BUILD_TYPE=Release
"$DTLS_CMAKE" --build "$ROOT/build/dtls-test" -j 8
"$ROOT/build/dtls-test/test-dtls"

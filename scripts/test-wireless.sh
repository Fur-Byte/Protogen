#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIRELESS_SOURCE="${MBEDTLS_SOURCE:-$ROOT/build/platformio/packages/framework-espidf/components/mbedtls/mbedtls}"
WIRELESS_CMAKE="${CMAKE:-$ROOT/build/platformio/packages/tool-cmake/bin/cmake}"
"$WIRELESS_CMAKE" -S "$ROOT/scripts/tests/wireless" -B "$ROOT/build/wireless-test" -DMBEDTLS_SOURCE="$WIRELESS_SOURCE" -DHOST_SANITIZERS="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
"$WIRELESS_CMAKE" --build "$ROOT/build/wireless-test" -j 8
"$ROOT/build/wireless-test/test-wireless"
"$ROOT/build/wireless-test/test-storage"
"$ROOT/build/wireless-test/test-uart"
"$ROOT/build/wireless-test/benchmark-wireless" > "$ROOT/build/wireless-benchmark.json"

#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="${MBEDTLS_SOURCE:-$ROOT/build/platformio/packages/framework-espidf/components/mbedtls/mbedtls}"
CMAKE_BIN="${CMAKE:-$ROOT/build/platformio/packages/tool-cmake/bin/cmake}"
"$CMAKE_BIN" -S "$ROOT/scripts/tests/wireless" -B "$ROOT/build/wireless-test" -DMBEDTLS_SOURCE="$SOURCE" -DHOST_SANITIZERS="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
"$CMAKE_BIN" --build "$ROOT/build/wireless-test" --target test-s31-platform -j8
"$ROOT/build/wireless-test/test-s31-platform"

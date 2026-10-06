#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="${MBEDTLS_SOURCE:-$ROOT/build/platformio/packages/framework-espidf/components/mbedtls/mbedtls}"
CMAKE_BIN="${CMAKE:-$ROOT/build/platformio/packages/tool-cmake/bin/cmake}"
"$CMAKE_BIN" -S "$ROOT/scripts/tests/v4" -B "$ROOT/build/v4-test" -DMBEDTLS_SOURCE="$SOURCE" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
"$CMAKE_BIN" --build "$ROOT/build/v4-test" -j 8
"$ROOT/build/v4-test/test-v4" "$@"

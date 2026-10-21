#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-uart-admission.XXXXXX")"
trap 'rm -rf "$TEST_TEMP"' EXIT
SANITIZERS="${PROTOGEN_HOST_SANITIZERS:-address,undefined}"
TEENSY="$ROOT/firmware/PROT_OS7_MAX7219"
FLAGS=(-g -Wall -Wextra -Werror -fsanitize="$SANITIZERS" -fno-sanitize-recover=all
  -DPROTOGEN_UART_AUTH=1 -I"$TEENSY" -I"$TEENSY/src/HelmetProtocol"
  -I"$TEENSY/src/UartCrypto" -I"$ROOT/scripts/tests/uart-fixture")
for unit in md sha256 platform_util; do
  "${CXX:-clang++}" -x c "${FLAGS[@]}" -c "$TEENSY/src/UartCrypto/$unit.c" -o "$TEST_TEMP/$unit.o"
done
"${CXX:-clang++}" -std=c++17 "${FLAGS[@]}" "$ROOT/scripts/tests/uart-admission.cpp" \
  "$TEST_TEMP/md.o" "$TEST_TEMP/sha256.o" "$TEST_TEMP/platform_util.o" -o "$TEST_TEMP/uart-admission"
for scenario in ${UART_ADMISSION_SCENARIOS:-pending first multiple wrap}; do
  "$TEST_TEMP/uart-admission" "$scenario"
done

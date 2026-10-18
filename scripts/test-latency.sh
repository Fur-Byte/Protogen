#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FFT_SRC="${ARDUINO_FFT_SRC:-$HOME/Documents/Arduino/libraries/arduinoFFT/src}"
LATENCY_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-latency.XXXXXX")"
trap 'rm -rf "$LATENCY_TEMP"' EXIT
FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -fno-sanitize-recover=all
 -DARDUINO=10819 -DARDUINO_TEENSY40 -DMicrophone -DBoopSensor -DVisorSensor -DWirelessPaw -DVisorConnector
 -I"$ROOT/scripts/tests/stubs" -I"$ROOT/firmware/PROT_OS7_MAX7219" -I"$FFT_SRC" -I"$ROOT/firmware/esp32/src"
 -I"$ROOT/firmware/PROT_OS7_MAX7219/src/HelmetProtocol" -I"$ROOT/scripts/tests/uart-fixture")
source "$ROOT/scripts/host-fft.sh"
compile_host_fft "$FFT_SRC" "$LATENCY_TEMP/fft.o"
UART_SOURCE="$ROOT/firmware/PROT_OS7_MAX7219/src/UartCrypto"
for unit in md sha256 platform_util; do
 "${CXX:-clang++}" -x c -g -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -DPROTOGEN_UART_AUTH=1 \
 -I"$ROOT/firmware/PROT_OS7_MAX7219" -I"$ROOT/scripts/tests/uart-fixture" -c "$UART_SOURCE/$unit.c" -o "$LATENCY_TEMP/$unit.o"
done
for auth in 0 1; do
 AUTH_FLAGS=(); OBJECTS=()
 if [[ "$auth" == 1 ]]; then AUTH_FLAGS=(-DPROTOGEN_UART_AUTH=1); OBJECTS=("$LATENCY_TEMP/md.o" "$LATENCY_TEMP/sha256.o" "$LATENCY_TEMP/platform_util.o"); fi
 "${CXX:-clang++}" "${FLAGS[@]}" ${AUTH_FLAGS[@]+"${AUTH_FLAGS[@]}"} "$ROOT/scripts/tests/response-latency.cpp" \
 "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$LATENCY_TEMP/fft.o" ${OBJECTS[@]+"${OBJECTS[@]}"} -o "$LATENCY_TEMP/latency"
 "$LATENCY_TEMP/latency"
done

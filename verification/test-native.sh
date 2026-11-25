#!/bin/bash
# Instrument actual production wrappers and mocked persistence boundaries.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build/formal
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cxx="${CXX:-g++}"
sketch=firmware/PROT_OS7_MAX7219
esp=firmware/esp32/src
flags=(-std=c++17 -O2 -Wall -Wextra -Werror
       -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
       -fno-pie -no-pie -I "$esp" -I "$sketch" -I "$sketch/src/HelmetProtocol")
"$cxx" "${flags[@]}" verification/tests/replay.cpp -o "$tmp/replay"
"$tmp/replay" | tee build/formal/replay-native.log
"$cxx" "${flags[@]}" -DPROTOGEN_ANIMATION_V2=1 verification/tests/assurance.cpp \
  "$sketch/src/BehaviorCore/AnimationV2.cpp" "$sketch/src/AudioV2/AudioV2.cpp" -o "$tmp/assurance"
"$tmp/assurance" | tee build/formal/assurance-native.log
"$cxx" "${flags[@]}" -I scripts/tests/stubs scripts/tests/config-compatibility.cpp -o "$tmp/eeprom"
"$tmp/eeprom" scripts/tests/fixtures/eeprom-v1.hex | tee build/formal/eeprom-native.log
"$cxx" "${flags[@]}" -DHELMET_RECEIVER -I scripts/tests/storage-stubs \
  scripts/tests/nvs-verification.cpp "$esp/SecureStorage.cpp" -o "$tmp/nvs"
"$tmp/nvs" | tee build/formal/nvs-native.log
"$cxx" "${flags[@]}" -DHELMET_RECEIVER scripts/tests/storage-faults.cpp -o "$tmp/storage"
"$tmp/storage" | tee build/formal/storage-native.log
"$cxx" "${flags[@]}" -I scripts/tests/storage-stubs verification/tests/session_faults.cpp \
  "$esp/WirelessSession.cpp" -o "$tmp/session-faults"
"$tmp/session-faults" | tee build/formal/session-faults-native.log

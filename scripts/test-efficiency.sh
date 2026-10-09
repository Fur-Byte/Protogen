#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FFT_SRC="${ARDUINO_FFT_SRC:-$HOME/Documents/Arduino/libraries/arduinoFFT/src}"
EFF_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-efficiency.XXXXXX")"
trap 'rm -rf "$EFF_TEMP"' EXIT
PYTHONDONTWRITEBYTECODE=1 python3 - "$ROOT" "$EFF_TEMP" <<'PY'
from pathlib import Path
import runpy, shutil, sys
root, temporary = map(Path,sys.argv[1:])
features=runpy.run_path(str(root/'scripts/check-features.py'))
sketch=temporary/'PROT_OS7_MAX7219'
shutil.copytree(root/'firmware/PROT_OS7_MAX7219',sketch)
main=sketch/'PROT_OS7_MAX7219.ino'
main.write_text(features['configure_features'](main.read_text(),(),features['ALL_FEATURES']))
PY
FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -fno-sanitize-recover=all
  -DARDUINO=10819 -DARDUINO_TEENSY40 -DPROTOGEN_CACHE_FACE=1
  -I"$ROOT/scripts/tests/stubs" -I"$EFF_TEMP/PROT_OS7_MAX7219" -I"$FFT_SRC"
  -I"$EFF_TEMP/PROT_OS7_MAX7219/src/HelmetProtocol")
"${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/efficiency.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" -o "$EFF_TEMP/cache"
"$EFF_TEMP/cache"
# Existing animation/audio stress suite also runs with the cache enabled.
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor \
  "$ROOT/scripts/tests/firmware.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" \
  "$FFT_SRC/arduinoFFT.cpp" -o "$EFF_TEMP/full"
"$EFF_TEMP/full"
"${CXX:-clang++}" "${FLAGS[@]}" -DHELMET_RECEIVER \
  -I"$ROOT/scripts/tests/storage-stubs" -I"$ROOT/firmware/esp32/src" \
  "$ROOT/scripts/tests/nvs-verification.cpp" "$ROOT/firmware/esp32/src/SecureStorage.cpp" -o "$EFF_TEMP/nvs"
"$EFF_TEMP/nvs"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-efficiency.py"

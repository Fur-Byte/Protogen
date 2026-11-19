#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FFT_SRC="${ARDUINO_FFT_SRC:-$HOME/Documents/Arduino/libraries/arduinoFFT/src}"
EFF_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-efficiency.XXXXXX")"
trap 'rm -rf "$EFF_TEMP"' EXIT
PYTHONDONTWRITEBYTECODE=1 python3 - "$ROOT" "$EFF_TEMP" <<'PY'
from pathlib import Path
import shutil, sys
root, temporary = map(Path,sys.argv[1:])
sys.path.insert(0, str(root / "scripts"))
from sketch_features import ALL_FEATURES, configure_features
sketch=temporary/'PROT_OS7_MAX7219'
shutil.copytree(root/'firmware/PROT_OS7_MAX7219',sketch)
main=sketch/'PROT_OS7_MAX7219.ino'
main.write_text(configure_features(main.read_text(), (), ALL_FEATURES))
PY
FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -fno-sanitize-recover=all
  -DARDUINO=10819 -DARDUINO_TEENSY40 -DPROTOGEN_CACHE_FACE=1
  -I"$ROOT/scripts/tests/stubs" -I"$EFF_TEMP/PROT_OS7_MAX7219" -I"$FFT_SRC"
  -I"$EFF_TEMP/PROT_OS7_MAX7219/src/HelmetProtocol")
source "$ROOT/scripts/host-fft.sh"
compile_host_fft "$FFT_SRC" "${EFF_TEMP}/arduinoFFT.o"
"${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/efficiency.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" -o "$EFF_TEMP/cache"
"$EFF_TEMP/cache"
# Existing animation/audio stress suite also runs with the cache enabled.
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor \
  "$ROOT/scripts/tests/firmware.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "${EFF_TEMP}/arduinoFFT.o" -o "$EFF_TEMP/full"
"$EFF_TEMP/full"
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor -DWirelessPaw -DVisorConnector \
  "$ROOT/scripts/tests/extensions.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "$EFF_TEMP/arduinoFFT.o" -o "$EFF_TEMP/extensions"
"$EFF_TEMP/extensions"
"${CXX:-clang++}" "${FLAGS[@]}" -DHELMET_RECEIVER \
  -I"$ROOT/scripts/tests/storage-stubs" -I"$ROOT/firmware/esp32/src" \
  "$ROOT/scripts/tests/nvs-verification.cpp" "$ROOT/firmware/esp32/src/SecureStorage.cpp" -o "$EFF_TEMP/nvs"
"$EFF_TEMP/nvs"
# Run the actual ESP worker code with each candidate isolated, then the OLED
# recovery/deadline combination. Fake peripherals model errors, not qualification.
for candidate in BASELINE CASE_2HZ OLED_1HZ OLED_PAGES CHECKPOINT_5MIN WEARER_DEADLINES MAIN_EVENTS OLED_PROTECTION COMBINED; do
  WORKER_FLAGS=()
  if [[ "$candidate" == COMBINED ]]; then
    WORKER_FLAGS=(-DPROTOGEN_WEARER_DEADLINES=1 -DPROTOGEN_OLED_PAGES=1 -DPROTOGEN_OLED_PROTECTION=1)
  elif [[ "$candidate" != BASELINE ]]; then
    WORKER_FLAGS=("-DPROTOGEN_${candidate}=1")
  fi
  "${CXX:-clang++}" "${FLAGS[@]}" ${WORKER_FLAGS[@]+"${WORKER_FLAGS[@]}"} -DHELMET_RECEIVER \
    -I"$ROOT/scripts/tests/wearer-stubs" -I"$ROOT/firmware/esp32/src" \
    "$ROOT/scripts/tests/wearer-io.cpp" -o "$EFF_TEMP/wearer"
  "$EFF_TEMP/wearer"
done
# Retain the old coarse-tick deadline stress as well as production1000Hz.
"${CXX:-clang++}" "${FLAGS[@]}" -DHELMET_RECEIVER -DPROTOGEN_WEARER_DEADLINES=1 -DPROTOGEN_TEST_TICK_HZ=100 \
  -I"$ROOT/scripts/tests/wearer-stubs" -I"$ROOT/firmware/esp32/src" \
  "$ROOT/scripts/tests/wearer-io.cpp" -o "$EFF_TEMP/wearer-coarse"
"$EFF_TEMP/wearer-coarse"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-efficiency.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-host-build.py"

# Optional existing INA228: actual task validation, freshness and fault recovery.
for deadlines in 0 1; do
  "${CXX:-clang++}" "${FLAGS[@]}" -DHELMET_RECEIVER -DPROTOGEN_WEARER_DEADLINES="$deadlines" \
    -I"$ROOT/scripts/tests/wearer-stubs" -I"$ROOT/firmware/esp32/src" \
    "$ROOT/scripts/tests/energy-monitor.cpp" -o "$EFF_TEMP/energy-monitor"
  for mode in direct task bus disabled; do "$EFF_TEMP/energy-monitor" "$mode"; done
done

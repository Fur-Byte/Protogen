#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FFT_SRC="${ARDUINO_FFT_SRC:-$HOME/Documents/Arduino/libraries/arduinoFFT/src}"
TEST_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-test.XXXXXX")"
trap 'rm -rf "$TEST_TEMP"' EXIT
if [[ ! -f "$FFT_SRC/arduinoFFT.cpp" ]]; then
  printf 'arduinoFFT source not found; set ARDUINO_FFT_SRC to its src directory.\n' >&2
  exit 1
fi
PYTHONDONTWRITEBYTECODE=1 python3 - "$ROOT" "$TEST_TEMP" <<'PY'
from pathlib import Path
import runpy, shutil, sys
root, temporary = map(Path, sys.argv[1:])
configure = runpy.run_path(str(root / 'scripts/check-features.py'))['configure_features']
sketch = temporary / 'PROT_OS7_MAX7219'
shutil.copytree(root / 'firmware/PROT_OS7_MAX7219', sketch)
main = sketch / 'PROT_OS7_MAX7219.ino'
main.write_text(configure(main.read_text(), ()))
PY
FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize=address,undefined
  -fno-sanitize-recover=all -DARDUINO=10819 -DARDUINO_TEENSY40
  -I"$ROOT/scripts/tests/stubs" -I"$TEST_TEMP/PROT_OS7_MAX7219" -I"$FFT_SRC")
FLAGS+=(-I"$ROOT/firmware/PROT_OS7_MAX7219/src/HelmetProtocol")
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor \
  "$ROOT/scripts/tests/firmware.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" \
  "$FFT_SRC/arduinoFFT.cpp" -o "$TEST_TEMP/test-firmware"
"$TEST_TEMP/test-firmware"
"${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/matrix.cpp" -o "$TEST_TEMP/test-matrix"
"${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/blink-timing.cpp" -o "$TEST_TEMP/test-blink-timing"
"$TEST_TEMP/test-blink-timing"
"${CXX:-clang++}" "${FLAGS[@]}" \
  "$ROOT/scripts/tests/configuration.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" \
  "$FFT_SRC/arduinoFFT.cpp" -o "$TEST_TEMP/test-configuration"
"$TEST_TEMP/test-configuration"
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor \
  "$ROOT/scripts/tests/configuration.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" \
  "$FFT_SRC/arduinoFFT.cpp" -o "$TEST_TEMP/test-configuration-sensors"
"$TEST_TEMP/test-configuration-sensors"
"$TEST_TEMP/test-matrix"
"${CXX:-clang++}" "${FLAGS[@]}" -DHELMET_RECEIVER \
  -I"$ROOT/firmware/esp32/src" "$ROOT/scripts/tests/helmet-protocol.cpp" -o "$TEST_TEMP/test-protocol"
"$TEST_TEMP/test-protocol"
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor -DWirelessPaw -DVisorConnector \
  "$ROOT/scripts/tests/extensions.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" \
  "$FFT_SRC/arduinoFFT.cpp" -o "$TEST_TEMP/test-extensions"
"$TEST_TEMP/test-extensions"
for mask in {0..7}; do
  FEATURES=()
  if (( mask & 1 )); then FEATURES+=(-DVisorSensor); fi
  if (( mask & 2 )); then FEATURES+=(-DBoopSensor); fi
  if (( mask & 4 )); then FEATURES+=(-DMicrophone); fi
  # Bash 3.2's nounset treats an empty array as unset; use conditional expansion.
  "${CXX:-clang++}" "${FLAGS[@]}" ${FEATURES[@]+"${FEATURES[@]}"} \
    "$ROOT/scripts/tests/feature-smoke.cpp" \
    "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" \
    "$FFT_SRC/arduinoFFT.cpp" -o "$TEST_TEMP/features-$mask"
  printf 'Sensor mask %s: ' "$mask"
  "$TEST_TEMP/features-$mask"
done
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-tools.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-helmet-tool.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-pair-paw.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/compare-archive-faces.py"

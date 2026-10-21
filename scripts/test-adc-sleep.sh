#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FFT_SRC="${ARDUINO_FFT_SRC:-$HOME/Documents/Arduino/libraries/arduinoFFT/src}"
TEST_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-adc-sleep.XXXXXX")"
trap 'rm -rf "$TEST_TEMP"' EXIT
PYTHONDONTWRITEBYTECODE=1 python3 - "$ROOT" "$TEST_TEMP" <<'PY'
from pathlib import Path
import runpy,shutil,sys
root,temporary=map(Path,sys.argv[1:])
features=runpy.run_path(str(root/'scripts/check-features.py'))
sketch=temporary/'PROT_OS7_MAX7219'
shutil.copytree(root/'firmware/PROT_OS7_MAX7219',sketch)
main=sketch/'PROT_OS7_MAX7219.ino'
main.write_text(features['configure_features'](main.read_text(),(),features['ALL_FEATURES']))
PY
FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}"
  -fno-sanitize-recover=all -DARDUINO=10819 -DARDUINO_TEENSY40
  -I"$ROOT/scripts/tests/stubs" -I"$TEST_TEMP/PROT_OS7_MAX7219" -I"$FFT_SRC"
  -I"$ROOT/firmware/PROT_OS7_MAX7219/src/HelmetProtocol")
source "$ROOT/scripts/host-fft.sh"
compile_host_fft "$FFT_SRC" "$TEST_TEMP/arduinoFFT.o"
for connector in 0 1; do
  for microphone in 0 1; do
    FEATURES=(-DBoopSensor)
    if (( connector )); then FEATURES+=(-DVisorConnector); fi
    if (( microphone )); then FEATURES+=(-DMicrophone); fi
    "${CXX:-clang++}" "${FLAGS[@]}" "${FEATURES[@]}" "$ROOT/scripts/tests/adc-sleep.cpp" \
      "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" \
      "$TEST_TEMP/arduinoFFT.o" -o "$TEST_TEMP/adc-sleep"
    for scenario in ${ADC_SLEEP_SCENARIOS:-cancel timeout rapid wrap}; do
      printf 'connector=%s microphone=%s: ' "$connector" "$microphone"
      "$TEST_TEMP/adc-sleep" "$scenario"
    done
  done
done

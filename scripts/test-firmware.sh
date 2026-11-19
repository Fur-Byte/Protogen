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
import shutil, sys
root, temporary = map(Path, sys.argv[1:])
sys.path.insert(0, str(root / "scripts"))
from sketch_features import ALL_FEATURES, configure_features
sketch = temporary / 'PROT_OS7_MAX7219'
shutil.copytree(root / 'firmware/PROT_OS7_MAX7219', sketch)
main = sketch / 'PROT_OS7_MAX7219.ino'
main.write_text(configure_features(main.read_text(), (), ALL_FEATURES))
PY
SANITIZERS="${PROTOGEN_HOST_SANITIZERS:-address,undefined}"
FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize="$SANITIZERS"
  -fno-sanitize-recover=all -DARDUINO=10819 -DARDUINO_TEENSY40
  -I"$ROOT/scripts/tests/stubs" -I"$TEST_TEMP/PROT_OS7_MAX7219" -I"$FFT_SRC")
FLAGS+=(-I"$ROOT/firmware/PROT_OS7_MAX7219/src/HelmetProtocol")
source "$ROOT/scripts/host-fft.sh"
compile_host_fft "$FFT_SRC" "${TEST_TEMP}/arduinoFFT.o"
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor \
  "$ROOT/scripts/tests/firmware.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "${TEST_TEMP}/arduinoFFT.o" -o "$TEST_TEMP/test-firmware"
"$TEST_TEMP/test-firmware"
"${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/matrix.cpp" -o "$TEST_TEMP/test-matrix"
"${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/blink-timing.cpp" -o "$TEST_TEMP/test-blink-timing"
"$TEST_TEMP/test-blink-timing"
"${CXX:-clang++}" "${FLAGS[@]}" \
  "$ROOT/scripts/tests/configuration.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "${TEST_TEMP}/arduinoFFT.o" -o "$TEST_TEMP/test-configuration"
"$TEST_TEMP/test-configuration"
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor \
  "$ROOT/scripts/tests/configuration.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "${TEST_TEMP}/arduinoFFT.o" -o "$TEST_TEMP/test-configuration-sensors"
"$TEST_TEMP/test-configuration-sensors"
"$TEST_TEMP/test-matrix"
"${CXX:-clang++}" "${FLAGS[@]}" -DHELMET_RECEIVER \
  -I"$ROOT/firmware/esp32/src" "$ROOT/scripts/tests/helmet-protocol.cpp" -o "$TEST_TEMP/test-protocol"
"$TEST_TEMP/test-protocol"
"${CXX:-clang++}" "${FLAGS[@]}" -I"$ROOT/firmware/esp32/src" \
  "$ROOT/scripts/tests/application-status.cpp" -o "$TEST_TEMP/test-application-status"
"$TEST_TEMP/test-application-status"
"${CXX:-clang++}" "${FLAGS[@]}" -I"$ROOT/firmware/esp32/src" \
  "$ROOT/scripts/tests/command-status.cpp" -o "$TEST_TEMP/test-command-status"
"$TEST_TEMP/test-command-status"
"${CXX:-clang++}" "${FLAGS[@]}" \
  "$ROOT/scripts/tests/retry-delay.cpp" -o "$TEST_TEMP/test-retry-delay"
"$TEST_TEMP/test-retry-delay"
"${CXX:-clang++}" "${FLAGS[@]}" -DWirelessPaw \
  "$ROOT/scripts/tests/controller-idle.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  -o "$TEST_TEMP/test-controller-idle"
"$TEST_TEMP/test-controller-idle"
"${CXX:-clang++}" "${FLAGS[@]}" -DVisorConnector \
  "$ROOT/scripts/tests/connector-timing.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  -o "$TEST_TEMP/test-connector-timing"
"$TEST_TEMP/test-connector-timing"
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor -DVisorSensor -DWirelessPaw -DVisorConnector \
  "$ROOT/scripts/tests/extensions.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "${TEST_TEMP}/arduinoFFT.o" -o "$TEST_TEMP/test-extensions"
"$TEST_TEMP/test-extensions"
# Exercise bounded ADC recovery, stale rail rejection, MISO/pilot restoration,
# real buffer-gated initialization and microphone recovery with modeled timing.
"${CXX:-clang++}" "${FLAGS[@]}" -DBoopSensor -DVisorConnector \
  "$ROOT/scripts/tests/sensor-recovery.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  -o "$TEST_TEMP/test-sensor-recovery"
for scenario in initialization initdetach pilot timeout invalid start stale stalecompletion boopstale cycle bounded permanent wrap; do
  "$TEST_TEMP/test-sensor-recovery" "$scenario"
done
"${CXX:-clang++}" "${FLAGS[@]}" -DMicrophone -DBoopSensor \
  "$ROOT/scripts/tests/microphone-recovery.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "${TEST_TEMP}/arduinoFFT.o" -o "$TEST_TEMP/test-microphone-recovery"
for scenario in rail invalid start stall bounded disable permanent wrap isolation; do
  "$TEST_TEMP/test-microphone-recovery" "$scenario"
done
# Optional authenticated UART uses the production Mbed TLS subset, with PUBLIC
# fixture material only. The actual sketch and command gate execute under ASan.
UART_SOURCE="$ROOT/firmware/PROT_OS7_MAX7219/src/UartCrypto"
for unit in md sha256 platform_util; do
  "${CXX:-clang++}" -x c -g -Wall -Wextra -Werror -fsanitize="$SANITIZERS" \
    -DPROTOGEN_UART_AUTH=1 -I"$TEST_TEMP/PROT_OS7_MAX7219" \
    -I"$ROOT/scripts/tests/uart-fixture" -c "$UART_SOURCE/$unit.c" -o "$TEST_TEMP/uart-$unit.o"
done
"${CXX:-clang++}" "${FLAGS[@]}" -DWirelessPaw -DPROTOGEN_UART_AUTH=1 \
  -I"$ROOT/scripts/tests/uart-fixture" "$ROOT/scripts/tests/uart-rendering.cpp" \
  "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
  "$TEST_TEMP/uart-md.o" "$TEST_TEMP/uart-sha256.o" "$TEST_TEMP/uart-platform_util.o" \
  -o "$TEST_TEMP/test-uart-rendering"
"$TEST_TEMP/test-uart-rendering"
for mask in {0..7}; do
  FEATURES=()
  if (( mask & 1 )); then FEATURES+=(-DVisorSensor); fi
  if (( mask & 2 )); then FEATURES+=(-DBoopSensor); fi
  if (( mask & 4 )); then FEATURES+=(-DMicrophone); fi
  # Bash 3.2's nounset treats an empty array as unset; use conditional expansion.
  "${CXX:-clang++}" "${FLAGS[@]}" ${FEATURES[@]+"${FEATURES[@]}"} \
    "$ROOT/scripts/tests/feature-smoke.cpp" \
    "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" \
    "${TEST_TEMP}/arduinoFFT.o" -o "$TEST_TEMP/features-$mask"
  printf 'Sensor mask %s: ' "$mask"
  "$TEST_TEMP/features-$mask"
done
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-tools.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-pq-worker.py"
PYTHONDONTWRITEBYTECODE=1 python3 -O "$ROOT/scripts/tests/test-pq-worker.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-helmet-tool.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/tests/test-pair-paw.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/compare-archive-faces.py"

# Boundary serialization and compile-time brightness-ceiling regressions.
bash "$ROOT/scripts/test-configuration-safety.sh"

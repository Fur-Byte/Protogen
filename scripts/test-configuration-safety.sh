#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-config-safety.XXXXXX")"
trap 'rm -rf "$CONFIG_TEMP"' EXIT
for maximum in 0 1 15; do
  PYTHONDONTWRITEBYTECODE=1 python3 - "$ROOT" "$CONFIG_TEMP" "$maximum" <<'PY'
from pathlib import Path
import runpy, shutil, sys
root, temporary = map(Path, sys.argv[1:3]); maximum = int(sys.argv[3])
features = runpy.run_path(str(root / 'scripts/check-features.py'))
sketch = temporary / 'PROT_OS7_MAX7219'
shutil.rmtree(sketch, ignore_errors=True)
shutil.copytree(root / 'firmware/PROT_OS7_MAX7219', sketch)
main = sketch / 'PROT_OS7_MAX7219.ino'
main.write_text(features['configure_features'](main.read_text(), (), features['ALL_FEATURES']))
settings = sketch / 'Settings.h'
settings.write_text(settings.read_text().replace('MaxBrightness = 1;', f'MaxBrightness = {maximum};'))
PY
  FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -fno-sanitize-recover=all
    -DARDUINO=10819 -DARDUINO_TEENSY40 -I"$ROOT/scripts/tests/stubs" -I"$CONFIG_TEMP/PROT_OS7_MAX7219")
  "${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/configuration-safety.cpp" \
    "$CONFIG_TEMP/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$CONFIG_TEMP/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" -o "$CONFIG_TEMP/config-safety"
  "${CXX:-clang++}" "${FLAGS[@]}" "$ROOT/scripts/tests/brightness-upgrade.cpp" \
    "$CONFIG_TEMP/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$CONFIG_TEMP/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" -o "$CONFIG_TEMP/brightness-upgrade"
  "$CONFIG_TEMP/brightness-upgrade"
  if [[ "$maximum" == 1 ]]; then
    "$CONFIG_TEMP/brightness-upgrade" write "$CONFIG_TEMP/brightness-format3.bin"
  elif [[ "$maximum" == 15 ]]; then
    "$CONFIG_TEMP/brightness-upgrade" read "$CONFIG_TEMP/brightness-format3.bin"
    "${CXX:-clang++}" "${FLAGS[@]}" -DVisorConnector \
      "$ROOT/scripts/tests/brightness-reconnect.cpp" \
      "$CONFIG_TEMP/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$CONFIG_TEMP/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" -o "$CONFIG_TEMP/brightness-reconnect"
    "$CONFIG_TEMP/brightness-reconnect"
    printf 'PASS: same EEPROM image booted under ceiling1 then ceiling15, saved approval stays1 until explicit SET\n'
  fi
  : > "$CONFIG_TEMP/replies.jsonl"
  for mode in brightness bounds errors; do
    "$CONFIG_TEMP/config-safety" "$mode" >> "$CONFIG_TEMP/replies.jsonl"
  done
  python3 - "$CONFIG_TEMP/replies.jsonl" "$maximum" <<'PY'
import json, sys
from pathlib import Path
replies = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines()]
assert all(type(reply.get('ok')) is bool for reply in replies)
assert any(reply.get('error') == 'response_too_large' for reply in replies)
assert any(len(reply.get('config', {})) == 80 for reply in replies)
for reply in replies:
    brightness = reply.get('brightness')
    if brightness:
        assert brightness['maximum'] == int(sys.argv[2])
        assert brightness['effective'] <= brightness['maximum']
print(f"PASS: ceiling {sys.argv[2]}, bounded capacities, 65-field duplicate tracking, {len(replies)} valid JSON replies")
PY
done
# Invalid ceilings must fail before a byte narrowing could wrap them to zero.
for invalid in -1 16 256; do
  python3 - "$ROOT/firmware/PROT_OS7_MAX7219/Settings.h" "$CONFIG_TEMP/invalid.h" "$invalid" <<'PY2'
from pathlib import Path
import sys
Path(sys.argv[2]).write_text(Path(sys.argv[1]).read_text().replace('MaxBrightness = 1;', f'MaxBrightness = {sys.argv[3]};'))
PY2
  if "${CXX:-clang++}" -std=c++17 -x c++ -fsyntax-only "$CONFIG_TEMP/invalid.h" > "$CONFIG_TEMP/invalid.log" 2>&1; then
    printf 'FAIL: invalid brightness ceiling %s compiled\n' "$invalid" >&2
    exit 1
  fi
  if ! grep -F -q 'MaxBrightness must be 0..15' "$CONFIG_TEMP/invalid.log"; then
    cat "$CONFIG_TEMP/invalid.log" >&2
    exit 1
  fi
done
printf 'PASS: negative, excessive and narrowing-overflow compile-time ceilings rejected\n'
if ! command -v node >/dev/null 2>&1; then
  printf 'Node.js is required for the Face Studio approval regression.\n' >&2
  exit 1
fi
node "$ROOT/scripts/tests/test-face-studio.js"

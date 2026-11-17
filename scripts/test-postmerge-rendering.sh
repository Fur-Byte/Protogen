#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RENDER_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-rendering.XXXXXX")"
trap 'rm -rf "$RENDER_TEMP"' EXIT
FLAGS=(-std=c++17 -g -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -fno-sanitize-recover=all
 -DARDUINO=10819 -DARDUINO_TEENSY40 -I"$ROOT/scripts/tests/stubs" -I"$ROOT/firmware/PROT_OS7_MAX7219")
for early in 0 1; do
"${CXX:-clang++}" "${FLAGS[@]}" -DPROTOGEN_EARLY_EXPRESSION_REVEAL="$early" "$ROOT/scripts/tests/postmerge-rendering.cpp" \
 "$ROOT/firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp" "$ROOT/firmware/PROT_OS7_MAX7219/src/BehaviorCore/BehaviorCore.cpp" -o "$RENDER_TEMP/rendering"
for scenario in "${@:-retarget causal entropy}"; do
  for name in $scenario; do "$RENDER_TEMP/rendering" "$name"; done
done
done

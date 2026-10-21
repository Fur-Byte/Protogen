#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INGRESS_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-ingress.XXXXXX")"
trap 'rm -rf "$INGRESS_TEMP"' EXIT
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" \
 -fno-sanitize-recover=all -I"$ROOT/firmware/esp32/src" "$ROOT/scripts/tests/ingress-peer.cpp" -o "$INGRESS_TEMP/test"
"$INGRESS_TEMP/test"

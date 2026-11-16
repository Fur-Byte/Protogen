#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-config-compat.XXXXXX")"
trap 'rm -rf "$CONFIG_TEMP"' EXIT
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror   -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" -fno-sanitize-recover=all   -I"$ROOT/scripts/tests/stubs" -I"$ROOT/firmware/PROT_OS7_MAX7219"   "$ROOT/scripts/tests/config-compatibility.cpp" -o "$CONFIG_TEMP/check"
"$CONFIG_TEMP/check" "$ROOT/scripts/tests/fixtures/eeprom-v1.hex"

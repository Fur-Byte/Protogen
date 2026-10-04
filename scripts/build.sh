#!/bin/bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
if [[ -z "${ARDUINO_CLI:-}" && ! -x "$CLI" ]]; then
  CLI="$(command -v arduino-cli || true)"
fi
if [[ -z "$CLI" ]] || ! command -v "$CLI" >/dev/null 2>&1; then
  printf 'Arduino CLI not found; set ARDUINO_CLI to an executable path or command.\n' >&2
  exit 127
fi
BUILD_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-build.XXXXXX")"
trap 'rm -rf "$BUILD_TEMP"' EXIT
mkdir -p "$PROJECT_ROOT/build/teensy40"
"$CLI" compile --fqbn teensy:avr:teensy40:usb=serial,speed=600,opt=o2std --warnings all --clean \
  --build-path "$BUILD_TEMP" --output-dir "$PROJECT_ROOT/build/teensy40" \
  "$PROJECT_ROOT/firmware/PROT_OS7_MAX7219" 2>&1 | tee "$PROJECT_ROOT/build/compile.log"

#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INGRESS_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/protogen-ingress.XXXXXX")"
trap 'rm -rf "$INGRESS_TEMP"' EXIT
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" \
 -fno-sanitize-recover=all -I"$ROOT/firmware/esp32/src" "$ROOT/scripts/tests/ingress-peer.cpp" -o "$INGRESS_TEMP/test"
"$INGRESS_TEMP/test"

# The startup test compiles the production adapter against a bounded SDK model.
for header in freertos/FreeRTOS.h freertos/queue.h freertos/task.h psa/crypto.h \
              bootloader_random.h esp_event.h esp_now.h esp_random.h esp_wifi.h; do
  mkdir -p "$INGRESS_TEMP/sdk/$(dirname "$header")"
  printf '#include "espnow-startup-stubs.h"\n' > "$INGRESS_TEMP/sdk/$header"
done
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize="${PROTOGEN_HOST_SANITIZERS:-address,undefined}" \
 -fno-sanitize-recover=all -I"$INGRESS_TEMP/sdk" -I"$ROOT/scripts/tests" -I"$ROOT/firmware/esp32/src" \
 "$ROOT/scripts/tests/espnow-startup.cpp" "$ROOT/firmware/esp32/src/EspNowTransport.cpp" -o "$INGRESS_TEMP/startup"
"$INGRESS_TEMP/startup"

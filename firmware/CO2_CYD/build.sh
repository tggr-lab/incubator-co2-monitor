#!/usr/bin/env bash
# Build / upload / monitor helper for CO2_CYD.
#
#   ./build.sh            compile only
#   ./build.sh upload     compile and flash
#   ./build.sh monitor    open the serial monitor
#   ./build.sh flash      compile, flash, then monitor
#
# The FQBN is Espressif's own board definition for this exact part, shipped in
# esp32 core 3.3.11, so the pin variant and flash settings come from the core.
#
# PartitionScheme=min_spiffs selects one of the core's stock tables (1.9 MB app
# with OTA, 128 KB SPIFFS). The default 1.28 MB slot is too small once the
# Wi-Fi stack and web server are included.
set -euo pipefail

CLI="${ARDUINO_CLI:-$HOME/.local/bin/arduino-cli}"
FQBN="esp32:esp32:jczn_2432s028r:PartitionScheme=min_spiffs"
PORT="${CO2_CYD_PORT:-/dev/ttyUSB0}"
BAUD=115200
HERE="$(cd "$(dirname "$0")" && pwd)"

cmd="${1:-compile}"

case "$cmd" in
  compile|upload|flash)
    echo "==> compiling $FQBN"
    "$CLI" compile --fqbn "$FQBN" "$HERE"
    ;;
esac

case "$cmd" in
  upload|flash)
    echo "==> uploading to $PORT"
    "$CLI" upload --fqbn "$FQBN" --port "$PORT" "$HERE"
    ;;
esac

case "$cmd" in
  monitor|flash)
    echo "==> monitor $PORT @ $BAUD  (ctrl-C to exit)"
    "$CLI" monitor --port "$PORT" --config "baudrate=$BAUD"
    ;;
esac

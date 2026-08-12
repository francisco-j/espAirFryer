#!/usr/bin/env bash
# Open the serial monitor. Ctrl-C to quit.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SKETCH_DIR/build.conf"

if [[ -z "${PORT:-}" ]]; then
  PORT="$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No serial port found. Plug the board in, or set PORT in build.conf." >&2
  exit 1
fi

echo "=== MONITOR $PORT @ $MONITOR_BAUD (Ctrl-C to quit) ==="
"$ARDUINO_CLI" monitor -p "$PORT" --config "baudrate=$MONITOR_BAUD"

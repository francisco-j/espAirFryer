#!/usr/bin/env bash
# Compile, then upload to the board. Extra args go to arduino-cli upload.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SKETCH_DIR/build.conf"

# Resolve the port: env override > build.conf > auto-detect.
if [[ -z "${PORT:-}" ]]; then
  PORT="$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No serial port found. Plug the board in, or set PORT in build.conf." >&2
  exit 1
fi
if [[ ! -e "$PORT" ]]; then
  echo "Serial port does not exist: $PORT" >&2
  echo "Available:" >&2
  ls /dev/cu.* >&2
  exit 1
fi

"$SKETCH_DIR/build.sh"

echo "=== UPLOADING to $PORT ==="
"$ARDUINO_CLI" upload -p "$PORT" --fqbn "$FQBN" "$@" "$SKETCH_DIR"
echo "=== UPLOAD OK ==="

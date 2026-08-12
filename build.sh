#!/usr/bin/env bash
# Compile the sketch. Any extra args are passed through to arduino-cli compile.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SKETCH_DIR/build.conf"

if [[ ! -x "$ARDUINO_CLI" ]]; then
  echo "arduino-cli not found at: $ARDUINO_CLI" >&2
  echo "Fix ARDUINO_CLI in build.conf." >&2
  exit 1
fi

echo "=== COMPILING ==="
"$ARDUINO_CLI" compile --fqbn "$FQBN" "$@" "$SKETCH_DIR"
echo "=== COMPILE OK ==="

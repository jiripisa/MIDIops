#!/usr/bin/env bash
# Build the Teensy 4.1 firmware and report the resulting .hex path.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

pio run -e teensy41

HEX="$ROOT_DIR/.pio/build/teensy41/firmware.hex"
if [[ ! -f "$HEX" ]]; then
    echo "error: firmware.hex not found at $HEX" >&2
    exit 1
fi

echo
echo "Firmware built: $HEX"

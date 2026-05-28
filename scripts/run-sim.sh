#!/usr/bin/env bash
# Build the macOS simulator and launch it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

pio run -e native

BINARY="$ROOT_DIR/.pio/build/native/program"
if [[ ! -x "$BINARY" ]]; then
    echo "error: simulator binary not found at $BINARY" >&2
    exit 1
fi

echo
echo "Launching $BINARY"
echo
exec "$BINARY"

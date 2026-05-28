#!/usr/bin/env bash
# Build the firmware and upload it to a connected Teensy 4.1.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

pio run -e teensy41 -t upload

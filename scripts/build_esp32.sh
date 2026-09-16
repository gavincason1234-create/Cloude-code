#!/usr/bin/env bash
# build_esp32.sh — Build and optionally flash ESP32-S2 firmware
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="$REPO_ROOT/firmware/esp32s2/jarvis_wifi/jarvis_wifi.ino"
DIST_DIR="$REPO_ROOT/dist/esp32s2"
ARDUINO_CLI="$HOME/.local/bin/arduino-cli"

mkdir -p "$DIST_DIR"

BOARD="esp32:esp32:esp32s2"
BUILD_PROPS="UploadSpeed=921600,FlashFreq=80,FlashMode=dio"

echo "[+] Building ESP32-S2 firmware: jarvis_wifi"

if [[ ! -f "$ARDUINO_CLI" ]]; then
    echo "[!] arduino-cli not found. Run ./scripts/setup.sh first."
    exit 1
fi

"$ARDUINO_CLI" compile \
    --fqbn "$BOARD" \
    --build-properties "$BUILD_PROPS" \
    --output-dir "$DIST_DIR" \
    "$SKETCH" \
    2>&1

echo ""
echo "[+] Build complete!"
echo "    Firmware: $DIST_DIR/jarvis_wifi.ino.bin"
echo ""
echo "    To flash (replace /dev/ttyUSB0 with your port):"
echo "      $ARDUINO_CLI upload --fqbn $BOARD --port /dev/ttyUSB0 $SKETCH"
echo ""
echo "    Or flash the .bin directly:"
echo "      esptool.py --chip esp32s2 --port /dev/ttyUSB0 write_flash 0x0 $DIST_DIR/jarvis_wifi.ino.bin"
echo ""

# Auto-flash if --flash argument provided
if [[ "${1:-}" == "--flash" ]]; then
    PORT="${2:-/dev/ttyUSB0}"
    echo "[*] Flashing to $PORT..."
    "$ARDUINO_CLI" upload \
        --fqbn "$BOARD" \
        --port "$PORT" \
        "$SKETCH"
    echo "[+] Flash complete!"
fi

#!/usr/bin/env bash
# setup.sh — Install toolchain and clone base firmware
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE_DIR="$REPO_ROOT/base_firmware"

echo "[+] Facts Lab Suite — Environment Setup"

# ── Dependencies ──────────────────────────────────────────────────────────────
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "[*] Installing system dependencies..."
    sudo apt-get update -qq
    sudo apt-get install -y \
        git cmake python3 python3-pip python3-venv \
        gcc-arm-none-eabi binutils-arm-none-eabi \
        libusb-1.0-0-dev pkg-config \
        curl wget unzip 2>/dev/null || true
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "[*] Installing via Homebrew..."
    brew install cmake python3 curl wget || true
    brew install --cask gcc-arm-embedded || true
fi

# ── ufbt (micro Flipper Build Tool) ──────────────────────────────────────────
echo "[*] Installing ufbt..."
pip3 install --user ufbt 2>/dev/null || pip install --user ufbt

# ── Clone Unleashed firmware as base ─────────────────────────────────────────
if [[ ! -d "$BASE_DIR" ]]; then
    echo "[*] Cloning Unleashed firmware..."
    git clone --depth=1 \
        https://github.com/DarkFlippers/unleashed-firmware.git \
        "$BASE_DIR"
else
    echo "[*] Base firmware already present, pulling latest..."
    git -C "$BASE_DIR" pull --ff-only || true
fi

# ── Link our custom apps into the base firmware ───────────────────────────────
echo "[*] Linking custom applications..."
APPS_SRC="$REPO_ROOT/firmware/flipper/applications_user"
APPS_DST="$BASE_DIR/applications_user"

for app_dir in "$APPS_SRC"/*/; do
    app_name="$(basename "$app_dir")"
    target="$APPS_DST/$app_name"
    if [[ -L "$target" ]]; then
        rm "$target"
    fi
    ln -sf "$app_dir" "$target"
    echo "    Linked: $app_name"
done

# ── ufbt update (pulls SDK matching firmware) ─────────────────────────────────
echo "[*] Updating ufbt SDK..."
cd "$REPO_ROOT/firmware/flipper/applications_user/wifi_arsenal"
ufbt update || true

# ── Arduino CLI for ESP32-S2 ─────────────────────────────────────────────────
ARDUINO_CLI="$HOME/.local/bin/arduino-cli"
if [[ ! -f "$ARDUINO_CLI" ]]; then
    echo "[*] Installing Arduino CLI..."
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
fi

echo "[*] Installing ESP32 Arduino core..."
"$ARDUINO_CLI" core install esp32:esp32 \
    --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json \
    2>/dev/null || true

echo "[*] Installing required libraries..."
"$ARDUINO_CLI" lib install "WebServer" 2>/dev/null || true

echo ""
echo "[+] Setup complete!"
echo "    Run ./scripts/build_flipper.sh to build Flipper FAPs"
echo "    Run ./scripts/build_esp32.sh   to build ESP32-S2 firmware"

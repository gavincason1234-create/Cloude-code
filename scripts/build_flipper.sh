#!/usr/bin/env bash
# build_flipper.sh — Build all custom FAP applications
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APPS_DIR="$REPO_ROOT/firmware/flipper/applications_user"
DIST_DIR="$REPO_ROOT/dist/flipper"

mkdir -p "$DIST_DIR"

echo "[+] Building Flipper FAP applications..."

APPS=(
    "facts_lab_suite"
    "wifi_arsenal"
    "rf_recon"
)

for app in "${APPS[@]}"; do
    APP_DIR="$APPS_DIR/$app"
    if [[ ! -d "$APP_DIR" ]]; then
        echo "[!] App directory not found: $app — skipping"
        continue
    fi

    echo "[*] Building: $app"
    cd "$APP_DIR"

    if ! ufbt build 2>&1; then
        echo "[!] Build failed for $app"
        exit 1
    fi

    # Copy built FAP to dist
    FAP=$(find "$APP_DIR/.ufbt/build" -name "*.fap" 2>/dev/null | head -1)
    if [[ -n "$FAP" ]]; then
        cp "$FAP" "$DIST_DIR/${app}.fap"
        echo "    -> $DIST_DIR/${app}.fap"
    fi
done

echo ""
echo "[+] Build complete!"
echo "    FAPs in: $DIST_DIR"
echo ""
echo "    To install:"
echo "    1. Mount Flipper SD card"
echo "    2. Copy *.fap to /apps/Tools/ or /apps/GPIO/ on the SD"
echo "    3. Or use: ./scripts/flash_faps.sh"

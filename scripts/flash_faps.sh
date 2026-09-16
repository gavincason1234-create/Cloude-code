#!/usr/bin/env bash
# flash_faps.sh — Deploy FAPs to connected Flipper Zero via qFlipper/ufbt
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$REPO_ROOT/dist/flipper"

if [[ ! -d "$DIST_DIR" ]]; then
    echo "[!] No built FAPs found. Run ./scripts/build_flipper.sh first."
    exit 1
fi

echo "[+] Deploying FAPs to Flipper Zero..."

# ufbt flash method (preferred — auto-detects device)
for fap in "$DIST_DIR"/*.fap; do
    app_name="$(basename "$fap" .fap)"
    echo "[*] Deploying: $app_name"

    # Try ufbt launch (installs + opens app)
    APP_DIR="$REPO_ROOT/firmware/flipper/applications_user/$app_name"
    if [[ -d "$APP_DIR" ]]; then
        cd "$APP_DIR"
        ufbt flash_usb 2>/dev/null || {
            echo "    ufbt flash_usb failed — trying direct copy..."

            # Fallback: copy to mounted SD card
            SD_PATHS=(
                "/media/$USER/Flipper SD"
                "/Volumes/Flipper SD"
                "/mnt/flipper"
            )
            for sd in "${SD_PATHS[@]}"; do
                if [[ -d "$sd/apps" ]]; then
                    cp "$fap" "$sd/apps/Tools/"
                    echo "    Copied to: $sd/apps/Tools/"
                    break
                fi
            done
        }
    fi
done

echo ""
echo "[+] Done. Restart your Flipper to see the new apps."

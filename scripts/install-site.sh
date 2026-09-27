#!/usr/bin/env bash
# Install the site's dependencies exactly as its lockfile says.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/site-dir.sh"

echo "install-site: pnpm install --frozen-lockfile in $SITE_DIR"
pnpm -C "$SITE_DIR" install --frozen-lockfile
echo "install-site: done"

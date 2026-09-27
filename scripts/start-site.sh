#!/usr/bin/env bash
# Build (unless SKIP_BUILD=1) and start the site in memory mode on 127.0.0.1:3100.
# Playwright's webServer runs this; `exec` makes the Next.js server the process Playwright
# owns, so it can stop it cleanly when the run ends.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/site-dir.sh"
use_memory_mode

PORT="${SITE_PORT:-3100}"

if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
  echo "start-site: SKIP_BUILD=1 — using the existing build in $SITE_DIR/.next"
  if [[ ! -d "$SITE_DIR/.next" ]]; then
    echo "start-site: $SITE_DIR/.next does not exist. Run 'pnpm site:build' first or unset SKIP_BUILD." >&2
    exit 1
  fi
else
  echo "start-site: building the site in $SITE_DIR (set SKIP_BUILD=1 to reuse a build)"
  pnpm -C "$SITE_DIR" build
fi

echo "start-site: next start → http://127.0.0.1:$PORT  (LOT_STORE=memory, LOT_SEED_SAMPLE=0)"
cd "$SITE_DIR"
exec ./node_modules/.bin/next start -H 127.0.0.1 -p "$PORT"

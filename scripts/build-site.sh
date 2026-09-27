#!/usr/bin/env bash
# Production build of the site (`next build`) with the memory-mode environment the tests use.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/site-dir.sh"
use_memory_mode

echo "build-site: SITE_DIR=$SITE_DIR"
echo "build-site: LOT_STORE=$LOT_STORE  NEXT_PUBLIC_SITE_URL=$NEXT_PUBLIC_SITE_URL"
echo "build-site: pnpm build"
pnpm -C "$SITE_DIR" build
echo "build-site: done → $SITE_DIR/.next"

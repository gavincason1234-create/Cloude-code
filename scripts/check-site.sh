#!/usr/bin/env bash
# The site's own static checks: TypeScript, ESLint, Vitest unit tests. Stops at the first failure.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/site-dir.sh"

echo "check-site: SITE_DIR=$SITE_DIR"

echo "check-site: [1/3] typecheck (tsc --noEmit)"
pnpm -C "$SITE_DIR" typecheck

echo "check-site: [2/3] lint (next lint, warnings are errors)"
pnpm -C "$SITE_DIR" lint

echo "check-site: [3/3] unit tests (vitest run)"
pnpm -C "$SITE_DIR" test

echo "check-site: all green"

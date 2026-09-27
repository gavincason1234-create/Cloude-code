#!/usr/bin/env bash
# Sourced by the other scripts in this folder — not run on its own.
#
# Resolves SITE_DIR (the Grandma's Truck Lot checkout) to an absolute path:
#   SITE_DIR unset → ../Grandmas-trucklot-website-  (the site repo cloned next to this one)
#   SITE_DIR set   → used as given; a relative path is taken from the current directory
#                    (CI sets SITE_DIR=./site after checking the site out into ./site)
# Also exports HARNESS_ROOT (this repo) and provides use_memory_mode().

HARNESS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -n "${SITE_DIR:-}" ]]; then
  _requested="$SITE_DIR"
else
  _requested="$HARNESS_ROOT/../Grandmas-trucklot-website-"
fi

if ! SITE_DIR="$(cd "$_requested" 2>/dev/null && pwd)"; then
  echo "site-dir: '$_requested' is not a directory." >&2
  echo "site-dir: clone the site repo next to this one, or point SITE_DIR at a checkout:" >&2
  echo "          SITE_DIR=/path/to/Grandmas-trucklot-website- pnpm e2e" >&2
  exit 1
fi

if [[ ! -f "$SITE_DIR/package.json" ]]; then
  echo "site-dir: $SITE_DIR has no package.json — is SITE_DIR pointing at the site repo?" >&2
  exit 1
fi

export SITE_DIR HARNESS_ROOT
unset _requested

# The site's test target: in-memory store, pretend sign-in buttons, simulated payments,
# no sample bookings seeded (tests load them when they want them), and a site URL that
# matches the address Playwright uses so auth redirects and cookies line up.
use_memory_mode() {
  export LOT_STORE=memory
  export LOT_SEED_SAMPLE=0
  export NEXT_PUBLIC_SITE_URL="http://127.0.0.1:${SITE_PORT:-3100}"
  export NEXT_TELEMETRY_DISABLED=1
}

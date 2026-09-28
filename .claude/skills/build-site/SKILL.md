---
name: build-site
description: Build the Grandma's Truck Lot site (next build) from this harness repo, in the memory-mode environment the tests use, and start it locally on port 3100. Use when asked to build the site, check that it compiles, or bring it up for a look.
---

# Build the site

`SITE_DIR` points at the site checkout (default `../Grandmas-trucklot-website-`; CI uses `./site`).
The build never touches the site's source.

## Steps

1. Dependencies, if not already there:
   ```bash
   pnpm site:install
   ```
2. Build:
   ```bash
   pnpm site:build
   ```
   This runs `next build` with `LOT_STORE=memory LOT_SEED_SAMPLE=0 NEXT_PUBLIC_SITE_URL=http://127.0.0.1:3100`
   (see `scripts/site-dir.sh` → `use_memory_mode`). Output lands in `$SITE_DIR/.next`.
3. To bring the built site up for a look (Ctrl-C stops it):
   ```bash
   SKIP_BUILD=1 bash scripts/start-site.sh     # http://127.0.0.1:3100, pretend sign-in on /login
   ```
   If the terminal is gone, `pnpm site:stop` kills whatever is on 3100. `pnpm e2e` will not start
   while something is there — it always tests its own fresh server, never a leftover one.
4. Build then test in one go:
   ```bash
   pnpm all
   ```

## If the build fails

The error is the site's, not the harness's. Read it, then open a PR in the site repo. Typical
causes: a page importing a server-only module from a client component, a type error, or an
environment variable the build needs that memory mode doesn't provide.

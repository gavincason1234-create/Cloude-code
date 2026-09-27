---
name: test-site
description: Run the Grandma's Truck Lot end-to-end suite (Playwright, memory mode) or the site's own typecheck/lint/unit checks from this harness repo. Use when asked to test, verify, or check the site, or when a change in the site repo needs proving.
---

# Test the site

The site checkout is resolved through `SITE_DIR` (default `../Grandmas-trucklot-website-`).
Never edit files under `SITE_DIR` from here — fixes go through a PR in the site repo.

## Steps

1. Make sure both repos have their dependencies:
   ```bash
   pnpm install && pnpm site:install
   ```
2. The site's static checks (fast, no browser):
   ```bash
   pnpm site:check          # typecheck → lint → vitest
   ```
3. The end-to-end suite. It builds and starts the site on 127.0.0.1:3100 by itself:
   ```bash
   pnpm e2e                 # everything, mobile then desktop
   SKIP_BUILD=1 pnpm e2e    # reuse the last build
   pnpm e2e -- e2e/booking.spec.ts --project=mobile   # narrow it down
   ```
4. When something fails, read the report before guessing:
   ```bash
   pnpm e2e:report
   pnpm exec playwright show-trace test-results/<test-folder>/trace.zip
   ```
5. Sanity checks for the harness itself: `pnpm typecheck` and `pnpm e2e:list`.

## Remember

- Do not run `playwright install` on this machine; Chromium 1194 is already in
  `PLAYWRIGHT_BROWSERS_PATH` and matches the pinned `@playwright/test` 1.56.1.
- Tests import `test`/`expect` from `e2e/helpers.ts` (per-test fake IP so rate limits never trip),
  use role+name locators with regexes, and restore any setting they change.
- "Clear everything" in the owner's settings resets data, not prices or gate codes.
- Point at another checkout with `SITE_DIR=/path pnpm e2e`.

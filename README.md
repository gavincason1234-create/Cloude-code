# Cloude-code — the test & build harness for Grandma's Truck Lot

The website for Grandma's Truck Lot lives in
[`gavincason1234-create/Grandmas-trucklot-website-`](https://github.com/gavincason1234-create/Grandmas-trucklot-website-).
This repo is everything that proves it works:

- **Playwright end-to-end tests** that drive the whole site — home page, booking flow, gate codes,
  "find my booking", the driver's account, the owner's dashboard, and a set of security checks —
  on a phone-sized and a laptop-sized browser.
- **Scripts** that install, typecheck, lint, unit-test and build the site from here.
- **A GitHub Action** that does all of the above nightly, on demand, and on every push to `main`.

Keeping the tests here keeps the site repo small and lets the suite grow without touching it.

## Quick start

```bash
# 1. clone both repos side by side
git clone https://github.com/gavincason1234-create/Grandmas-trucklot-website-
git clone https://github.com/gavincason1234-create/Cloude-code
cd Cloude-code

# 2. install (Node 22, pnpm 10)
pnpm install          # this repo
pnpm site:install     # the site

# 3. run everything
pnpm site:check       # the site's typecheck + lint + unit tests
pnpm all              # build the site, then the end-to-end suite
pnpm e2e:report       # open the HTML report
```

`pnpm e2e` on its own builds and starts the site for you (memory mode, port 3100). Reuse a build
with `SKIP_BUILD=1 pnpm e2e`. The suite always starts its own server; if a `next start` from an
earlier look is still on 3100 it stops with `EADDRINUSE` — `pnpm site:stop` clears it (or
`REUSE_SERVER=1 pnpm e2e` tests against it on purpose). Run one file or one browser:

```bash
pnpm e2e -- e2e/booking.spec.ts
pnpm e2e -- --project=mobile
pnpm e2e:ui
```

The site is expected next to this repo. If it is somewhere else:

```bash
SITE_DIR=/path/to/Grandmas-trucklot-website- pnpm e2e
```

## What the tests run against

The site's **memory mode** (`LOT_STORE=memory`): no database, no Google sign-in, no Stripe.
Everything lives in the Next.js process — pretend "Sign in as a test driver / as the owner" buttons
on `/login`, simulated payments that mark a booking paid at once, gate codes 4471 / 4471 (shed 8820),
15 spots at $10 a night and $125 a month. The owner's Settings page can load sample bookings and
clear everything, and the tests use both.

## Layout

```
e2e/                    the tests
  helpers.ts            sign-in, reset, sample data, the booking flow, API helpers
  public.spec.ts        pages anyone can see
  booking.spec.ts       reserving as a guest, /find, booking API rules
  account.spec.ts       a signed-in driver
  admin.spec.ts         the owner's dashboard
  security.spec.ts      what must never leak, who must never get in
scripts/                install-site, check-site, build-site, start-site, stop-site (+ site-dir.sh they share)
playwright.config.ts    mobile 390×844 + desktop 1280×800, serial, traces on failure
.github/workflows/      site-ci.yml
.claude/skills/         test-site, build-site
CLAUDE.md               the working notes for Claude Code sessions in this repo
```

## CI

`.github/workflows/site-ci.yml` checks this repo out, checks the site out into `./site` with the
`SITE_REPO_TOKEN` secret (a fine-grained personal access token with read access to the private
site repo), installs both, runs the site's checks, builds it, installs Chromium and runs the suite.
The Playwright report is uploaded when something fails.

## Rules of the road

- Never edit the site from here. If a test finds a bug, fix it with a PR in the site repo.
- Pin `@playwright/test` to the Chromium already on the dev box (1.56.1 ↔ Chromium 1194). Don't run
  `playwright install` locally; CI does it because runners start with no browser.
- Tests run serially on purpose — the memory store is one shared state.

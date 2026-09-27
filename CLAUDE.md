# Cloude-code — test & build harness for Grandma's Truck Lot

This repo does one job: prove the website works. It holds the Playwright end-to-end suite and the
scripts that typecheck, lint, unit-test and build the site. **The website itself lives in another
repo** (`gavincason1234-create/Grandmas-trucklot-website-`) and is never edited from here.

> Found a bug in the site while working here? Do not patch the site checkout. Open a PR in the
> site repo (or write the failing test here and hand it over). The site checkout under `SITE_DIR`
> is read-only from this repo's point of view.

## Where the site is

Every script resolves `SITE_DIR`:

| Situation | `SITE_DIR` |
|---|---|
| Default (site cloned next to this repo) | `../Grandmas-trucklot-website-` |
| Somewhere else | `SITE_DIR=/abs/or/relative/path pnpm e2e` |
| GitHub Actions | `./site` (the workflow checks the site out there) |

`scripts/site-dir.sh` does the resolving and is sourced by every other script.

## Commands

| Command | What it does |
|---|---|
| `pnpm install` | this repo's deps: `@playwright/test` **1.56.1** (pinned to match Chromium 1194), TypeScript |
| `pnpm site:install` | `pnpm install --frozen-lockfile` inside the site |
| `pnpm site:check` | site `typecheck` → `lint` → `test` (Vitest), stops on first failure |
| `pnpm site:build` | `next build` of the site with the memory-mode env |
| `pnpm e2e` | Playwright: builds + starts the site on `127.0.0.1:3100`, runs `e2e/*.spec.ts` on the `mobile` and `desktop` projects |
| `SKIP_BUILD=1 pnpm e2e` | same, reusing the site's existing `.next` build |
| `pnpm e2e -- --project=mobile e2e/booking.spec.ts` | one project / one file |
| `pnpm e2e:ui` | Playwright's UI mode |
| `pnpm e2e:list` | list tests without running |
| `pnpm e2e:report` | open the last HTML report |
| `pnpm all` | `site:build` then `e2e` with `SKIP_BUILD=1` |
| `pnpm typecheck` | typecheck the harness's own TypeScript (`e2e/`, `playwright.config.ts`) |
| `pnpm clean` | delete `test-results/` and `playwright-report/` |

Environment: Node 22, pnpm 10.33. Never run `playwright install` on the dev box — Chromium 1194
already sits in `PLAYWRIGHT_BROWSERS_PATH` (`/opt/pw-browsers`) and matches 1.56.1. If Playwright
ever can't find a browser, point `PW_CHROMIUM` at a Chromium binary (see `playwright.config.ts`).
CI *does* run `playwright install --with-deps chromium` because runners have no browser.

## The test target: memory mode

`scripts/start-site.sh` starts the site with

```
LOT_STORE=memory LOT_SEED_SAMPLE=0 NEXT_PUBLIC_SITE_URL=http://127.0.0.1:3100
```

which means:

- **In-memory store.** Everything lives in the Next.js process and vanishes on restart. One shared
  state → tests run **serially** (`workers: 1`, `fullyParallel: false`). Both projects (`mobile`, then
  `desktop`) run every spec against the same server.
- **Pretend sign-in** on `/login`: "Sign in as a test driver" (`driver@example.com`) and "Sign in as
  the owner" (`admin@example.com`). Both POST to `/auth/dev`. No Google, no Supabase.
- **Simulated payments.** A booking is paid the moment the fake checkout sheet's Pay button is
  pressed; the confirmation shows gate codes **4471 / 4471**, plus shed **8820** when an add-on
  (shower/laundry) was chosen. Codes unlock two days before arrival.
- **Defaults:** 15 spots, $10/night, $125/month, lot name "Grandma's Truck Lot".
- **Sample data** (owner → Settings → "Load sample bookings"): Dale Whitaker (tonight), Marisol
  Ortega (parked), Curtis Bell (in 2 days), Tommy Reyes (owes), members Ray Fleitman and Angela
  Hess (past due), two approved reviews and one unapproved "Porta-potty" review.
- **"Clear everything"** wipes bookings/members/payments/reviews/log — **not settings**. A test that
  changes a price or a gate code must put it back (see `admin.spec.ts`).
- **Confirm boxes.** Destructive buttons (driver cancels, owner's Pulled out / Mark paid / Hide /
  Cancel & refund / Clear everything) open a `window.confirm`. Call `acceptDialogs(page)` before
  clicking them or the click is a no-op.
- **Sample data** is refused on a production Vercel deployment with a real database; in memory mode it
  always loads. Its booking codes are random each load, so never hard-code them in a test.

## Layout

```
playwright.config.ts   projects mobile (390×844, touch) + desktop (1280×800); webServer = scripts/start-site.sh
e2e/helpers.ts         the `test` object to import, signInAs, resetData, loadSample, reserveNightly, saveSetting, API helpers
e2e/public.spec.ts     pages anyone can see, availability API, robots, manifest, 404, night mode
e2e/booking.spec.ts    guest booking + /find + API rules (honeypot, past date, availability drops)
e2e/account.spec.ts    signed-in driver: list, cancel/refund, sign out
e2e/admin.spec.ts      owner dashboard: access rules, sample data through every tab, settings → drivers
e2e/security.spec.ts   what must never leak (codes, phone numbers) and who must never get in
scripts/               site-dir.sh (sourced), install-site.sh, check-site.sh, build-site.sh, start-site.sh
.github/workflows/     site-ci.yml — nightly + on demand + push to main
.claude/skills/        test-site, build-site — the commands above, for Claude Code sessions
```

## Writing tests here

1. **Import `test` and `expect` from `./helpers`**, not from `@playwright/test`. The helpers' `test`
   gives every test its own pretend client IP (`x-forwarded-for`), which is how the site keys its
   rate limits (10 bookings / 20 lookups per 10 minutes). Without it a full run trips the limiter.
2. **Role + name locators with regexes** (`getByRole("button", { name: /they.re here/i })`,
   `getByLabel(/your code/i)`). No test ids — the site has none, and wording is what drivers see.
   When a step may already be past, use `clickIfVisible`; when wording may vary, use `firstVisible`
   with a list of patterns (see `saveSetting`).
3. **Start from known state.** Call `resetData(page)` before relying on what is in the store, and
   `loadSample(page)` when you need the sample names. Clean up what you change.
4. **Dates come from the server.** Use `lotToday(request)` (Central Time) and `addDays`, never
   `new Date()` on the test machine. Book for `daysAhead: 1` when you need the cancel window
   (before 6 PM Central on arrival day) to be open.
5. **Both viewports.** Anything you assert must hold on a 390px phone: the header's "Sign in" link
   and the desktop nav are hidden there, so go to `/login` directly and assert inside `main`.
6. **Prefer API assertions for state** (`/api/availability`, `/api/me`, `/api/lookup`) and UI
   assertions for what the driver/owner actually sees.
7. Keep a test's writes local to it: pick far-out dates for availability maths, unique phone
   numbers per test, and restore any setting you touch (in a `finally`).

## When the suite fails

- `pnpm e2e:report` opens the HTML report; failed tests keep a trace (`trace: retain-on-failure`)
  and a screenshot. `pnpm exec playwright show-trace test-results/**/trace.zip` for one trace.
- "SITE_DIR ... is not a directory" → clone the site next to this repo or set `SITE_DIR`.
- Server never comes up → run `pnpm site:build` on its own to see the build error.
- The port is busy → something else is on 3100; locally Playwright reuses it (`reuseExistingServer`),
  so kill a stale server if the tests seem to run against old code.

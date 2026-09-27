import { test as base, expect, type APIRequestContext, type Locator, type Page } from "@playwright/test";

export { expect };

/* ------------------------------------------------------------------------------------------ */
/* The test object every spec imports                                                          */
/* ------------------------------------------------------------------------------------------ */

/**
 * The site rate-limits public POSTs per client IP (10 bookings and 20 lookups per 10 minutes,
 * 5 reviews an hour) and reads the IP from `x-forwarded-for`. Every test gets its own pretend
 * IP so a full run — two projects, a couple dozen bookings — never trips the limiter.
 * `page.request` and the `request` fixture both carry the header too.
 */
function fakeIp(): string {
  const octet = () => 1 + Math.floor(Math.random() * 253);
  return `10.${octet()}.${octet()}.${octet()}`;
}

export const test = base.extend({
  extraHTTPHeaders: async ({ extraHTTPHeaders }, use) => {
    await use({ ...(extraHTTPHeaders ?? {}), "x-forwarded-for": fakeIp() });
  },
});

/* ------------------------------------------------------------------------------------------ */
/* Constants that mirror the site's memory mode                                                */
/* ------------------------------------------------------------------------------------------ */

export const LOT_NAME = "Grandma's Truck Lot";
export const DEFAULT_SPOTS = 15;
/** Gate 1 / gate 2 / shed codes the memory store starts with. */
export const DEFAULT_CODES = { gate1: "4471", gate2: "4471", shed: "8820" } as const;
export const DEV_COOKIE = "lot_dev_user";
export const DEV_EMAIL = { driver: "driver@example.com", admin: "admin@example.com" } as const;
/** Names inside "Load sample bookings". */
export const SAMPLE = {
  tonightDriver: "Dale Whitaker",
  member: "Ray Fleitman",
  unapprovedReview: /porta-potty/i,
} as const;

/* ------------------------------------------------------------------------------------------ */
/* Dates                                                                                       */
/* ------------------------------------------------------------------------------------------ */

/** Same arithmetic the site uses: noon-UTC anchored, so DST never shifts a day. */
export function addDays(day: string, n: number): string {
  const d = new Date(`${day}T12:00:00Z`);
  d.setUTCDate(d.getUTCDate() + n);
  return d.toISOString().slice(0, 10);
}

/* ------------------------------------------------------------------------------------------ */
/* Small locator utilities                                                                     */
/* ------------------------------------------------------------------------------------------ */

/** The first of several candidate locators that resolves to a visible element, or null. */
export async function firstVisible(candidates: Locator[]): Promise<Locator | null> {
  for (const c of candidates) {
    const first = c.first();
    if (await first.isVisible().catch(() => false)) return first;
  }
  return null;
}

/** Click when the element is there; a no-op when it isn't (the step may already be past). */
export async function clickIfVisible(locator: Locator, timeout = 1500): Promise<boolean> {
  const first = locator.first();
  const visible = await first.waitFor({ state: "visible", timeout }).then(() => true, () => false);
  if (visible) await first.click();
  return visible;
}

/**
 * The booking / member / review card that mentions `text`.
 * StayCard and the owner's cards render through <Card>, which carries Tailwind's `border-l-4` —
 * that is the first hook. If a page lays things out differently, fall back to the nearest
 * ancestor of the text that holds a button or link, which is what "the card" means in practice.
 * The choice is made once, so the returned locator still re-resolves on every expect() retry.
 */
export async function cardWith(page: Page, text: string | RegExp): Promise<Locator> {
  await page.getByText(text).first().waitFor({ state: "attached", timeout: 10_000 }).catch(() => undefined);
  const card = page.locator('[class*="border-l-4"]').filter({ hasText: text });
  if ((await card.count()) > 0) return card.first();
  return page.getByText(text).first().locator("xpath=ancestor::*[.//button or .//a][1]");
}

/* ------------------------------------------------------------------------------------------ */
/* Dialogs                                                                                     */
/* ------------------------------------------------------------------------------------------ */

const dialogsWired = new WeakSet<Page>();

/** Auto-accept confirm()/alert() on this page for the rest of the test. Safe to call twice. */
export function acceptDialogs(page: Page): void {
  if (dialogsWired.has(page)) return;
  dialogsWired.add(page);
  page.on("dialog", (d) => d.accept().catch(() => undefined));
}

/* ------------------------------------------------------------------------------------------ */
/* API helpers                                                                                 */
/* ------------------------------------------------------------------------------------------ */

export type Me = { name: string; email: string; isAdmin: boolean } | null;

export async function whoAmI(request: APIRequestContext): Promise<Me> {
  const res = await request.get("/api/me");
  expect(res.status(), "GET /api/me").toBe(200);
  const body = (await res.json()) as { user: Me };
  return body.user;
}

export type DayOccupancy = { day: string; nightly: number; monthly: number; taken: number; open: number };
export type Availability = { spots: number; from: string; days: DayOccupancy[] };

export async function availability(request: APIRequestContext, opts: { from?: string; days?: number } = {}): Promise<Availability> {
  const q = new URLSearchParams();
  if (opts.from) q.set("from", opts.from);
  if (opts.days) q.set("days", String(opts.days));
  const qs = q.toString();
  const res = await request.get(`/api/availability${qs ? `?${qs}` : ""}`);
  expect(res.status(), "GET /api/availability").toBe(200);
  return (await res.json()) as Availability;
}

/** Today's date at the lot (Central Time), as the server sees it. */
export async function lotToday(request: APIRequestContext): Promise<string> {
  return (await availability(request, { days: 1 })).from;
}

export type BookingPayload = {
  plan: "nightly" | "monthly";
  arrive: string;
  nights: number;
  name: string;
  phone: string;
  company: string;
  truck: string;
  plate: string;
  extras: { showers: number; loads: number };
  website: string;
};

/** A valid nightly booking body for POST /api/bookings; override whatever the test is about. */
export function bookingPayload(arrive: string, overrides: Partial<BookingPayload> = {}): BookingPayload {
  return {
    plan: "nightly",
    arrive,
    nights: 1,
    name: "API Tester",
    phone: "(940) 555-0123",
    company: "",
    truck: "Flatbed",
    plate: "TX API123",
    extras: { showers: 0, loads: 0 },
    website: "",
    ...overrides,
  };
}

export function lookup(request: APIRequestContext, code: string, last4: string) {
  return request.post("/api/lookup", { data: { code, last4 } });
}

/** Last four digits of a phone number, the way /find asks for them. */
export function last4(phone: string): string {
  return phone.replace(/\D/g, "").slice(-4);
}

/* ------------------------------------------------------------------------------------------ */
/* Sign-in (memory mode's pretend buttons)                                                     */
/* ------------------------------------------------------------------------------------------ */

export type Role = "driver" | "admin";

const SIGN_IN_BUTTON: Record<Role, RegExp> = {
  driver: /sign in as a test driver/i,
  admin: /sign in as the owner/i,
};

/** Clears the session cookie. Works from any page; fine to call when nobody is signed in. */
export async function signOut(page: Page): Promise<void> {
  await page.request.post("/auth/signout", { maxRedirects: 0 }).catch(() => undefined);
}

/**
 * Sign in through /login's pretend buttons (they POST to /auth/dev). The owner is sent to
 * /admin, a driver to /account. Signs any current user out first, because /login bounces
 * signed-in visitors away before the buttons appear.
 */
export async function signInAs(page: Page, role: Role): Promise<void> {
  await signOut(page);
  await page.goto(role === "admin" ? "/login?next=%2Fadmin" : "/login");
  await page.getByRole("button", { name: SIGN_IN_BUTTON[role] }).click();
  await page.waitForURL(/\/(account|admin)(\/|\?|#|$)/);

  const me = await whoAmI(page.request);
  expect(me, `a session exists after signing in as ${role}`).not.toBeNull();
  expect(me?.isAdmin ?? false, `isAdmin after signing in as ${role}`).toBe(role === "admin");

  if (role === "admin" && !/\/admin(\/|\?|#|$)/.test(page.url())) await page.goto("/admin");
}

/* ------------------------------------------------------------------------------------------ */
/* Owner's settings page: reset + sample data                                                  */
/* ------------------------------------------------------------------------------------------ */

async function onSettingsAsAdmin(page: Page): Promise<void> {
  const me = await whoAmI(page.request).catch(() => null);
  if (!me?.isAdmin) await signInAs(page, "admin");
  if (!/\/admin\/settings/.test(page.url())) await page.goto("/admin/settings");
  await expect(page).toHaveURL(/\/admin\/settings/);
}

/**
 * "Clear everything" on /admin/settings: wipes bookings, members, payments, reviews and the log.
 * Settings (prices, gate codes) are NOT reset by it — restore those yourself if you changed them.
 */
export async function resetData(page: Page): Promise<void> {
  await onSettingsAsAdmin(page);
  acceptDialogs(page);
  await page.getByRole("button", { name: /clear everything/i }).click();
  await expect
    .poll(async () => (await availability(page.request, { days: 1 })).days[0]?.taken ?? -1, {
      message: "no stalls are taken after 'Clear everything'",
      timeout: 15_000,
    })
    .toBe(0);
}

/** "Load sample bookings" on /admin/settings: Dale, Marisol, Curtis, Tommy, Ray, Angela, 3 reviews. */
export async function loadSample(page: Page): Promise<void> {
  await onSettingsAsAdmin(page);
  acceptDialogs(page);
  await page.getByRole("button", { name: /load sample (bookings|data)/i }).click();
  await expect
    .poll(async () => (await availability(page.request, { days: 1 })).days[0]?.taken ?? 0, {
      message: "sample bookings occupy stalls tonight",
      timeout: 15_000,
    })
    .toBeGreaterThan(0);
}

/**
 * Change one setting on /admin/settings and save the form it lives in.
 * `labels` are tried in order; the first one that matches a visible field wins, so the test keeps
 * working if the owner-facing wording shifts a little.
 */
export async function saveSetting(page: Page, labels: RegExp[], value: string): Promise<void> {
  await onSettingsAsAdmin(page);
  // Only things you can type into: a number box (spinbutton) or a text box — never a checkbox.
  const candidates = labels.flatMap((l) => [page.getByRole("spinbutton", { name: l }), page.getByRole("textbox", { name: l })]);
  const field = await firstVisible(candidates);
  if (!field) throw new Error(`No text/number settings field matched ${labels.map(String).join(", ")} on /admin/settings`);
  await field.fill(value);

  const form = field.locator("xpath=ancestor::form[1]");
  const saveInForm = form.getByRole("button", { name: /save|update/i });
  const save = (await saveInForm.count()) > 0 ? saveInForm.first() : page.getByRole("button", { name: /save|update/i }).first();
  await save.click();
  await page.waitForLoadState("networkidle");
}

/* ------------------------------------------------------------------------------------------ */
/* The booking flow                                                                            */
/* ------------------------------------------------------------------------------------------ */

export type ReserveOptions = {
  name: string;
  phone: string;
  /** 1–30, default 1. */
  nights?: number;
  /** Shower add-ons; any add-on unlocks the shed code on the confirmation. */
  showers?: number;
  /** Laundry loads. */
  loads?: number;
  /** Arrive this many days after the lot's "today". 0 (default) = tonight. */
  daysAhead?: number;
  company?: string;
  truck?: string;
  plate?: string;
};

const CONFIRMED_URL = /\/book\/confirmed\/([A-Za-z0-9]+)/;

export function codeFromUrl(url: string): string {
  const m = CONFIRMED_URL.exec(url);
  if (!m?.[1]) throw new Error(`No booking code in ${url}`);
  return m[1].toUpperCase();
}

/**
 * Drive /book end to end — plan → dates → details → pay (simulated checkout sheet) — and
 * return the 5-character booking code from the confirmation URL.
 */
export async function reserveNightly(page: Page, o: ReserveOptions): Promise<string> {
  const nights = o.nights ?? 1;
  await page.goto("/book?plan=nightly");

  // Step 1 — plan. `?plan=nightly` skips it; handle it anyway in case the shortcut goes away.
  const planCard = page.getByRole("button", { name: /tonight or a few nights|nightly/i });
  if (await planCard.first().isVisible().catch(() => false)) {
    await planCard.first().click();
    await clickIfVisible(page.getByRole("button", { name: /next.*dates|continue|next/i }));
  }

  // Step 2 — dates.
  const arrive = page.getByLabel(/^arrive/i).or(page.locator("#arrive")).first();
  await expect(arrive, "the date step is showing").toBeVisible();
  if (o.daysAhead) {
    const lotTodayValue = await arrive.inputValue(); // the box starts on the lot's today
    await arrive.fill(addDays(lotTodayValue, o.daysAhead));
  }
  if (nights !== 1) {
    const nightsBox = page.getByLabel("Nights", { exact: true });
    if (await nightsBox.isVisible().catch(() => false)) await nightsBox.fill(String(nights));
  }
  await page.getByRole("button", { name: /next.*details|continue|next/i }).first().click();

  // Step 3 — who's coming.
  await page.getByLabel(/your name/i).fill(o.name);
  await page.getByLabel(/^phone/i).fill(o.phone);
  if (o.company !== undefined) await page.getByLabel(/^company/i).fill(o.company);
  if (o.truck !== undefined) await page.getByLabel(/^truck/i).fill(o.truck);
  if (o.plate !== undefined) await page.getByLabel(/^plate/i).fill(o.plate);
  for (let i = 0; i < (o.showers ?? 0); i++) await page.getByRole("button", { name: /more showers?/i }).click();
  for (let i = 0; i < (o.loads ?? 0); i++) await page.getByRole("button", { name: /more laundry/i }).click();
  await page.getByRole("button", { name: /next.*(review|pay)|continue|next/i }).first().click();

  // Step 4 — review & pay. The Pay button opens the pretend checkout sheet in simulated mode.
  await page.getByRole("button", { name: /^pay\b/i }).first().click();
  const sheet = page.getByRole("dialog");
  const sheetShown = await sheet.waitFor({ state: "visible", timeout: 5_000 }).then(() => true, () => false);
  if (sheetShown) await sheet.getByRole("button", { name: /^pay\b/i }).click();

  await page.waitForURL(CONFIRMED_URL, { timeout: 30_000 });
  return codeFromUrl(page.url());
}

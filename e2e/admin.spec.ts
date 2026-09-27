import { acceptDialogs, cardWith, DEFAULT_CODES, expect, loadSample, reserveNightly, resetData, SAMPLE, saveSetting, signInAs, signOut, test } from "./helpers";

/**
 * The owner's dashboard. Serial: every test here shares the one memory store, and the last one
 * puts prices and gate codes back to their defaults and clears the data, so the specs that run
 * after this file (booking, public, security) see a lot as fresh as the one they were written for.
 */
test.describe("owner dashboard", () => {
  test.describe.configure({ mode: "serial" });

  test("anonymous /admin goes to sign-in and comes back afterwards", async ({ page }) => {
    await page.goto("/admin");
    await expect(page).toHaveURL(/\/login/);
    expect(new URL(page.url()).searchParams.get("next")).toBe("/admin");
    await page.getByRole("button", { name: /sign in as the owner/i }).click();
    await expect(page).toHaveURL(/\/admin(\/|\?|#|$)/);
  });

  test("a plain driver is turned away with the owner-list note", async ({ page }) => {
    await signInAs(page, "driver");
    await page.goto("/admin");
    await expect(page).toHaveURL(/\/account/);
    // Scoped to <main>: Next.js's route announcer is also role="alert".
    await expect(page.locator("main").getByRole("alert")).toContainText(/isn['’]t on the owner list/i);
  });

  test("owner sign-in lands on Tonight", async ({ page }) => {
    await signInAs(page, "admin");
    await expect(page).toHaveURL(/\/admin(\/|\?|#|$)/);
    // The page heading, not the site header's "Tonight" link (hidden on phones).
    await expect(page.locator("main").getByRole("heading", { level: 1 })).toContainText(/tonight/i);
    // The dashboard nav has every tab.
    const nav = page.getByRole("navigation", { name: /owner|dashboard|admin/i });
    for (const tab of ["Monthly", "Money", "Reviews", "Settings", "Log"]) {
      await expect(nav.getByRole("link", { name: tab }).or(nav.getByRole("listitem").filter({ hasText: tab })).first()).toBeVisible();
    }
  });

  test("sample data flows through Tonight, Monthly, Money and Reviews", async ({ page }) => {
    await resetData(page);
    await loadSample(page);

    // Tonight: Dale is expected; "They're here" parks him.
    await page.goto("/admin");
    const main = page.locator("main");
    await expect(main).toContainText(SAMPLE.tonightDriver);
    const dale = await cardWith(page, SAMPLE.tonightDriver);
    await expect(dale).toBeVisible();
    await expect(dale).toContainText(/reserved|expected/i);
    await dale.getByRole("button", { name: /they.re here/i }).click();
    const parked = await cardWith(page, SAMPLE.tonightDriver);
    await expect(parked).toContainText(/parked/i);
    await expect(parked).not.toContainText(/reserved/i);

    // Monthly: Ray's membership.
    await page.goto("/admin/monthly");
    await expect(page.locator("main")).toContainText(SAMPLE.member);

    // Money: what has come in.
    await page.goto("/admin/money");
    await expect(page.locator("main")).toContainText(/collected/i);

    // Reviews: the unapproved porta-potty review waits for a look, then goes live.
    await page.goto("/admin/reviews");
    await expect(page.locator("main")).toContainText(SAMPLE.unapprovedReview);
    acceptDialogs(page);
    const review = await cardWith(page, SAMPLE.unapprovedReview);
    const approveInCard = review.getByRole("button", { name: /approve|show|publish/i });
    if ((await approveInCard.count()) > 0) await approveInCard.first().click();
    else await page.getByRole("button", { name: /approve|show|publish/i }).first().click();

    await page.goto("/reviews");
    await expect(page.locator("main")).toContainText(SAMPLE.unapprovedReview);

    // The log knows what the owner just did.
    await page.goto("/admin/log");
    await expect(page.locator("main")).toContainText(/review|parked|here/i);
  });

  test("settings changes reach drivers: nightly price on /pricing, gate code on a new confirmation", async ({ page }) => {
    const PRICE = [/^per.?night/i, /night(ly)?\s*(price|rate)/i, /price.*night/i, /^nightly\b/i, /a night/i];
    const GATE1 = [/^gate\s*1\b/i, /gate\s*1\s*code/i, /first gate/i, /gate code 1/i];

    await signInAs(page, "admin");
    try {
      await saveSetting(page, PRICE, "12");
      await page.goto("/pricing");
      // "$12 per night" in the plan panel and "$12 a night" in the comparison table.
      // (A bare "$10" still appears legitimately — the shower add-on is $10 each.)
      await expect(page.locator("main")).toContainText(/\$12(\.00)?\s*(a|per)\s*night/i);
      await expect(page.locator("main")).not.toContainText(/\$10(\.00)?\s*(a|per)\s*night/i);

      await saveSetting(page, GATE1, "5555");
      await signOut(page);
      const code = await reserveNightly(page, { name: "Gate Check", phone: "(405) 555-0111" });
      expect(code).toMatch(/^[A-Z0-9]{5,7}$/);
      const main = page.locator("main");
      await expect(main).toContainText("5555");
      await expect(main).toContainText(DEFAULT_CODES.gate2); // gate 2 untouched
      await expect(main).toContainText(/\$12\b/); // the new price was charged
    } finally {
      // Put the lot back the way the other specs expect it, whatever happened above.
      await signInAs(page, "admin");
      await saveSetting(page, PRICE, "10");
      await saveSetting(page, GATE1, DEFAULT_CODES.gate1);
      await page.goto("/pricing");
      await expect(page.locator("main")).toContainText(/\$10(\.00)?\s*(a|per)\s*night/i);
      await resetData(page);
    }
  });
});

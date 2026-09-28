import { acceptDialogs, addDays, cardWith, DEFAULT_CODES, expect, loadSample, lotToday, reserveNightly, resetData, SAMPLE, saveSetting, signInAs, signOut, test } from "./helpers";

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
    await expect(page.locator("main").getByRole("alert")).toContainText(/isn['’]t set up as an owner/i);
  });

  test("owner sign-in lands on Tonight", async ({ page }) => {
    await signInAs(page, "admin");
    await expect(page).toHaveURL(/\/admin(\/|\?|#|$)/);
    // The page heading, not the site header's "Tonight" link (hidden on phones).
    await expect(page.locator("main").getByRole("heading", { level: 1 })).toContainText(/tonight/i);
    // The dashboard nav has every tab.
    const nav = page.getByRole("navigation", { name: /owner|dashboard|admin/i });
    for (const tab of ["Bookings", "Monthly", "Money", "Reviews", "Settings", "Log"]) {
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

  test("Bookings lists every stay and finds one by name, plate, phone or night", async ({ page, request }) => {
    // The sample: Dale (tonight), Marisol (parked, arrived yesterday for 2 nights), Curtis (in 2 days),
    // Tommy (parked 3 days ago, owes). Loaded here so the test stands on its own.
    await resetData(page);
    await loadSample(page);
    await page.goto("/admin/bookings");
    const main = page.locator("main");
    await expect(main.getByRole("heading", { level: 1 })).toContainText(/bookings/i);
    for (const name of SAMPLE.bookings) await expect(main).toContainText(name);

    const search = main.getByRole("textbox", { name: /name.*code.*phone.*plate/i });
    const go = main.getByRole("button", { name: /^search$/i });

    // By part of a name. Each submit is pinned to its own URL so no assertion can pass on the page before it.
    await search.fill("curtis");
    await go.click();
    await expect(page).toHaveURL(/\/admin\/bookings\?.*q=curtis/);
    await expect(main).toContainText("Curtis Bell");
    await expect(main).not.toContainText("Marisol Ortega");
    await expect(main).not.toContainText("Dale Whitaker");

    // By a plate read off the truck, spacing ignored.
    await search.fill("771pz");
    await go.click();
    await expect(page).toHaveURL(/q=771pz/);
    await expect(main).toContainText("Curtis Bell");
    await expect(main).not.toContainText("Tommy Reyes");

    // By the last four digits of a phone number.
    await search.fill("0177");
    await go.click();
    await expect(page).toHaveURL(/q=0177/);
    await expect(main).toContainText("Marisol Ortega");
    await expect(main).not.toContainText("Curtis Bell");

    // "Who was here on" last night: Marisol's two nights cover it, nobody else's stay does.
    await search.fill("");
    const yesterday = addDays(await lotToday(request), -1);
    await main.getByLabel(/who was here on/i).fill(yesterday);
    await go.click();
    await expect(page).toHaveURL(new RegExp(`on=${yesterday}`));
    await expect(main).toContainText("Marisol Ortega");
    await expect(main).not.toContainText("Curtis Bell");
    await expect(main).not.toContainText("Dale Whitaker");

    // Clear brings everyone back.
    await main.getByRole("link", { name: /^clear$/i }).click();
    await expect(page).toHaveURL(/\/admin\/bookings\/?$/);
    for (const name of SAMPLE.bookings) await expect(main).toContainText(name);

    // Nothing found says so in plain words instead of an empty page.
    await search.fill("zzqx");
    await go.click();
    await expect(page).toHaveURL(/q=zzqx/);
    await expect(main).toContainText(/nothing matches/i);
  });

  test("a button on Bookings comes back to the same search", async ({ page }) => {
    await resetData(page);
    await loadSample(page);
    await page.goto("/admin/bookings?q=curtis");
    acceptDialogs(page);
    const curtis = await cardWith(page, "Curtis Bell");
    await expect(curtis).toContainText(/reserved/i);
    await curtis.getByRole("button", { name: /cancel/i }).click();
    // The round trip is done once his card says cancelled; only then is the URL worth checking.
    const after = await cardWith(page, "Curtis Bell");
    await expect(after).toContainText(/cancelled/i);
    await expect(after).not.toContainText(/reserved/i);
    await expect(page).toHaveURL(/\/admin\/bookings\?.*q=curtis/);
    // And he sits under the Past heading, not just somewhere on the page.
    const past = page.locator("main").getByText(/^past \(\d+\)$/i);
    await expect(past).toBeVisible();
    await expect(past.locator("xpath=following-sibling::*[1]")).toContainText("Curtis Bell");
    // Tonight no longer expects him.
    await page.goto("/admin");
    await expect(page.locator("main")).not.toContainText("Curtis Bell");
  });

  test("Pulled out, Hide from Tonight and Show on Tonight, all from Bookings", async ({ page }) => {
    await resetData(page);
    await loadSample(page);
    await page.goto("/admin/bookings?q=tommy");
    acceptDialogs(page);

    // Tommy is parked. Pulled out closes the stay; he is now one of Tonight's "Recently left".
    let card = await cardWith(page, "Tommy Reyes");
    await card.getByRole("button", { name: /pulled out/i }).click();
    card = await cardWith(page, "Tommy Reyes");
    await expect(card.getByRole("button", { name: /pulled out/i })).toHaveCount(0);
    await expect(card).toContainText(/pulled out/i);
    await expect(page).toHaveURL(/q=tommy/);
    await page.goto("/admin");
    await expect(page.locator("main")).toContainText("Tommy Reyes");

    // Hide him from Tonight: gone from there, still in the book, and the button becomes the undo.
    await page.goto("/admin/bookings?q=tommy");
    card = await cardWith(page, "Tommy Reyes");
    await card.getByRole("button", { name: /hide from tonight/i }).click();
    card = await cardWith(page, "Tommy Reyes");
    await expect(card).toContainText(/hidden from tonight/i);
    await expect(card.getByRole("button", { name: /hide from tonight/i })).toHaveCount(0);
    await page.goto("/admin");
    await expect(page.locator("main")).not.toContainText("Tommy Reyes");

    // Show on Tonight puts him back.
    await page.goto("/admin/bookings?q=tommy");
    card = await cardWith(page, "Tommy Reyes");
    await card.getByRole("button", { name: /show on tonight/i }).click();
    card = await cardWith(page, "Tommy Reyes");
    await expect(card).not.toContainText(/hidden from tonight/i);
    await expect(card.getByRole("button", { name: /hide from tonight/i })).toBeVisible();
    await page.goto("/admin");
    await expect(page.locator("main")).toContainText("Tommy Reyes");
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

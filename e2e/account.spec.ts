import { cardWith, expect, reserveNightly, signInAs, test, whoAmI } from "./helpers";

/**
 * A signed-in driver: their bookings under /account, cancelling for a full refund, signing out.
 * The stay is booked for TOMORROW so the cancel window (before 6 PM Central on arrival day) is
 * always open no matter what time the suite runs; codes still show, they unlock two days ahead.
 */
test.describe("driver account", () => {
  test("a driver signs in, reserves, sees the booking, cancels for a full refund and signs out", async ({ page }) => {
    await signInAs(page, "driver");
    await expect(page).toHaveURL(/\/account/);
    await expect(page.getByRole("heading", { level: 1 })).toContainText(/hey/i);

    const code = await reserveNightly(page, { name: "Test Driver", phone: "(940) 555-0199", daysAhead: 1, truck: "Reefer", plate: "TX 3390LK" });

    // Listed under the account with its code and a receipt link.
    await page.goto("/account");
    const main = page.locator("main");
    await expect(main).toContainText(code);
    const card = await cardWith(page, code);
    await expect(card).toBeVisible();
    await expect(card).toContainText(/reserved/i);
    await expect(card.getByRole("link", { name: /codes|receipt/i })).toBeVisible();

    // Cancel → full refund → the card and a flash both say cancelled.
    await card.getByRole("button", { name: /cancel.*refund/i }).click();
    await page.waitForURL(/\/account\?.*cancelled=/);
    await expect(page.getByRole("status").filter({ hasText: code })).toContainText(/cancelled/i);
    const cancelled = await cardWith(page, code);
    await expect(cancelled).toContainText(/cancelled/i);
    await expect(cancelled.getByRole("button", { name: /cancel/i })).toHaveCount(0);

    // The confirmation page agrees.
    await page.goto(`/book/confirmed/${code}`);
    await expect(page.getByRole("heading", { level: 1 })).toContainText(/cancelled/i);
    await expect(page.locator("main")).not.toContainText(/gate 1/i);

    // Sign out lands on the home page and the session is gone.
    await page.goto("/account");
    await page.getByRole("button", { name: /sign out/i }).click();
    await page.waitForURL((url) => url.pathname === "/");
    expect(await whoAmI(page.request)).toBeNull();
    await page.goto("/account");
    await expect(page).toHaveURL(/\/login/);
  });

  test("the account page prefills the booking form with the driver's details", async ({ page }) => {
    await signInAs(page, "driver");
    await page.goto("/book?plan=nightly");
    await page.getByRole("button", { name: /next.*details|continue|next/i }).first().click();
    // The pretend driver is "Test Driver"; their name should already be in the box.
    await expect(page.getByLabel(/your name/i)).toHaveValue(/test driver/i);
  });
});

import { DEFAULT_SPOTS, expect, test } from "./helpers";

/**
 * The driver-facing pages nobody has to sign in for. These run against a fresh memory store
 * (LOT_SEED_SAMPLE=0) and touch nothing, so they are safe in any order.
 */
test.describe("public pages", () => {
  test("home shows the lot name, tonight's open count and a Reserve link", async ({ page }) => {
    const res = await page.goto("/");
    expect(res?.status()).toBe(200);

    await expect(page.getByRole("banner")).toContainText(/grandma['’]s truck lot/i);
    await expect(page.getByRole("heading", { level: 1 })).toBeVisible();

    // "<big number> open" — the first thing a driver looks for.
    await expect(page.locator("main")).toContainText(/\d+\s*open/i);

    await expect(page.getByRole("link", { name: /reserve a spot|pick another night/i }).first()).toBeVisible();
  });

  for (const path of ["/lot", "/pricing", "/directions", "/reviews", "/find"]) {
    test(`${path} responds 200 with an h1`, async ({ page }) => {
      const res = await page.goto(path);
      expect(res?.status(), `${path} status`).toBe(200);
      const h1 = page.getByRole("heading", { level: 1 });
      await expect(h1).toBeVisible();
      await expect(h1).not.toBeEmpty();
    });
  }

  test("/api/availability lists 14 days for the lot's spots", async ({ request }) => {
    const res = await request.get("/api/availability");
    expect(res.status()).toBe(200);
    expect(res.headers()["content-type"]).toMatch(/application\/json/);

    const body = (await res.json()) as { spots: number; from: string; days: { day: string; open: number }[] };
    expect(body.spots).toBe(DEFAULT_SPOTS);
    expect(body.from).toMatch(/^\d{4}-\d{2}-\d{2}$/);
    expect(Array.isArray(body.days)).toBe(true);
    expect(body.days).toHaveLength(14);
    for (const d of body.days) {
      expect(d.day).toMatch(/^\d{4}-\d{2}-\d{2}$/);
      expect(d.open).toBeGreaterThanOrEqual(0);
      expect(d.open).toBeLessThanOrEqual(body.spots);
    }
    expect(body.days[0]?.day).toBe(body.from);
  });

  test("robots.txt keeps crawlers out of /admin", async ({ request }) => {
    const res = await request.get("/robots.txt");
    expect(res.status()).toBe(200);
    const text = await res.text();
    expect(text).toMatch(/^Disallow:\s*\/admin\s*$/im);
    expect(text).toMatch(/^Allow:\s*\/\s*$/im);
  });

  test("the web manifest is installable as \"Truck Lot\"", async ({ request }) => {
    const res = await request.get("/manifest.webmanifest");
    expect(res.status()).toBe(200);
    const manifest = (await res.json()) as { name: string; short_name: string; start_url: string; icons: unknown[] };
    expect(manifest.short_name).toBe("Truck Lot");
    expect(manifest.name).toMatch(/grandma['’]s truck lot/i);
    expect(manifest.start_url).toBe("/");
    expect(manifest.icons.length).toBeGreaterThan(0);
  });

  test("an unknown page is a real 404 with a way home", async ({ page }) => {
    const res = await page.goto("/this-page-does-not-exist-9f3k");
    expect(res?.status()).toBe(404);
    await expect(page.getByRole("heading", { level: 1 })).toBeVisible();
    await expect(page.locator("main").locator('a[href="/"]').first()).toBeVisible();
  });

  test("the night mode toggle sets data-theme on <html>", async ({ page }) => {
    await page.goto("/");
    const html = page.locator("html");
    const toggle = page.getByRole("button", { name: /night mode|day mode/i }).first();
    await expect(toggle).toBeVisible();

    const before = await html.getAttribute("data-theme");
    await toggle.click();
    await expect(html).toHaveAttribute("data-theme", /^(dark|light)$/);
    const after = await html.getAttribute("data-theme");
    expect(after).not.toBe(before);
    // colorScheme is pinned to light in playwright.config.ts, so the first flip is to night mode.
    expect(after).toBe("dark");

    await toggle.click();
    await expect(html).toHaveAttribute("data-theme", "light");
  });
});

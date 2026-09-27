import { addDays, availability, bookingPayload, DEFAULT_CODES, expect, last4, lookup, lotToday, reserveNightly, test } from "./helpers";

/**
 * A guest (nobody signed in) reserving a night, then finding it again, plus the API rules behind
 * the form. Payments are simulated, so a booking is paid the moment the sheet says so.
 */
test.describe("booking", () => {
  const GUEST = { name: "Dale Whitaker", phone: "(940) 555-0142", truck: "53' dry van", plate: "TX 8421RM" };

  test("a guest reserves tonight with a shower and sees the code, both gate codes and the shed code", async ({ page }) => {
    const code = await reserveNightly(page, { ...GUEST, showers: 1 });
    expect(code).toMatch(/^[A-Z0-9]{5,7}$/);

    const main = page.locator("main");
    await expect(main.getByText(code, { exact: true })).toBeVisible();
    await expect(page.getByRole("heading", { level: 1 })).toContainText(/booked/i);

    await expect(main).toContainText(/gate 1/i);
    await expect(main).toContainText(/gate 2/i);
    await expect(main).toContainText(/shed/i);

    const text = await main.innerText();
    expect(text.match(new RegExp(DEFAULT_CODES.gate1, "g"))?.length, "gate code shown twice (gate 1 + gate 2)").toBe(2);
    expect(text).toContain(DEFAULT_CODES.shed);
  });

  test("/find shows the codes for code + last 4, and nothing for the wrong last 4", async ({ page }) => {
    const code = await reserveNightly(page, { ...GUEST, showers: 1 });

    await page.goto("/find");
    await page.getByLabel(/your code/i).fill(code);
    await page.getByLabel(/last 4/i).fill(last4(GUEST.phone));
    await page.getByRole("button", { name: /find my booking/i }).click();

    const main = page.locator("main");
    await expect(main).toContainText(GUEST.name);
    await expect(main).toContainText(code);
    await expect(main).toContainText(DEFAULT_CODES.gate1);
    await expect(main).toContainText(DEFAULT_CODES.shed);
    // No account and no full phone number: only "ending in 0142".
    await expect(main).not.toContainText("555-0142");

    await page.getByLabel(/last 4/i).fill("9999");
    await page.getByRole("button", { name: /find my booking/i }).click();
    await expect(main.getByRole("alert")).toContainText(/no booking matches/i); // scoped: Next's route announcer is role="alert" too
    await expect(main).not.toContainText(DEFAULT_CODES.shed);
  });

  test("POST /api/bookings rejects the honeypot", async ({ request }) => {
    const today = await lotToday(request);
    const res = await request.post("/api/bookings", { data: bookingPayload(today, { website: "x" }) });
    expect(res.status()).toBe(400);
    const body = (await res.json()) as { error: string; code?: string; ok?: boolean };
    expect(body.ok).toBeUndefined();
    expect(typeof body.error).toBe("string");
  });

  test("POST /api/bookings rejects a date in the past", async ({ request }) => {
    const today = await lotToday(request);
    const res = await request.post("/api/bookings", { data: bookingPayload(addDays(today, -1)) });
    expect(res.status()).toBe(400);
    const body = (await res.json()) as { error: string; code?: string };
    expect(body.code).toBe("past_date");
    expect(body.error).toMatch(/already passed/i);
  });

  test("a booking takes one stall off that night's availability", async ({ request }) => {
    // A night far enough out that no other test touches it, well inside the 180-day window.
    const day = addDays(await lotToday(request), 40);

    const before = await availability(request, { from: day, days: 1 });
    expect(before.days[0]?.day).toBe(day);
    const openBefore = before.days[0]!.open;
    expect(openBefore).toBeGreaterThan(0);

    const res = await request.post("/api/bookings", { data: bookingPayload(day, { name: "Availability Probe", phone: "(214) 555-0177" }) });
    expect(res.status()).toBe(200);
    const created = (await res.json()) as { ok: true; kind: string; code: string; checkoutUrl: string | null };
    expect(created.ok).toBe(true);
    expect(created.kind).toBe("nightly");
    expect(created.code).toMatch(/^[A-Z0-9]{5,7}$/);
    expect(created.checkoutUrl).toBeNull(); // simulated payments: no Stripe hop

    const after = await availability(request, { from: day, days: 1 });
    expect(after.days[0]!.open).toBe(openBefore - 1);
    expect(after.days[0]!.nightly).toBe(before.days[0]!.nightly + 1);

    // And the lookup finds it — proof it is a real, paid booking, not just a counter.
    const found = await lookup(request, created.code, "0177");
    expect(found.status()).toBe(200);
    const stay = (await found.json()) as { code: string; paid: boolean; arrive: string; codes: unknown };
    expect(stay.code).toBe(created.code);
    expect(stay.paid).toBe(true);
    expect(stay.arrive).toBe(day);
    expect(stay.codes, "codes stay hidden until two days before arrival").toBeNull();
  });
});

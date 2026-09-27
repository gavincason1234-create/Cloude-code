import { bookingPayload, DEFAULT_CODES, DEV_COOKIE, DEV_EMAIL, expect, lookup, lotToday, reserveNightly, signInAs, test, whoAmI } from "./helpers";

/** base64url(JSON) — the payload half of the memory-mode session cookie. */
function cookiePayload(user: { id: string; email: string; name: string }): string {
  return Buffer.from(JSON.stringify(user), "utf8").toString("base64url");
}

const GATE_CODE = new RegExp(`${DEFAULT_CODES.gate1}|${DEFAULT_CODES.shed}`);

/**
 * What must never leak and who must never get in. Everything here is meant to keep passing when
 * the site grows, so it checks behaviour (status codes, redirects, response bodies), not markup.
 */
test.describe("security", () => {
  test("/api/lookup with a made-up code answers 404 JSON and says nothing useful", async ({ request }) => {
    const res = await lookup(request, "ZZZZQ", "1234");
    expect(res.status()).toBe(404);
    expect(res.headers()["content-type"]).toMatch(/application\/json/);
    const body = (await res.json()) as { error: string; code: string };
    expect(body.code).toBe("not_found");
    expect(body.error).toMatch(/no booking matches/i);
    // Same answer for a real-looking code with the wrong phone, so codes can't be probed.
    const probe = await lookup(request, "K7M2P", "0000");
    expect(probe.status()).toBe(404);
    expect(await probe.json()).toEqual(body);
  });

  test("POST /api/bookings never puts gate codes in a response", async ({ request }) => {
    const today = await lotToday(request);

    const ok = await request.post("/api/bookings", { data: bookingPayload(today, { extras: { showers: 1, loads: 1 } }) });
    expect(ok.status()).toBe(200);
    const okText = await ok.text();
    expect(okText).not.toMatch(GATE_CODE);
    expect(JSON.parse(okText)).toEqual(expect.objectContaining({ ok: true, code: expect.any(String) }));

    for (const bad of [bookingPayload(today, { website: "x" }), bookingPayload("2000-01-01"), bookingPayload(today, { phone: "12" })]) {
      const res = await request.post("/api/bookings", { data: bad });
      expect(res.status()).toBeGreaterThanOrEqual(400);
      expect(await res.text()).not.toMatch(GATE_CODE);
    }

    const garbage = await request.post("/api/bookings", { data: "not json", headers: { "content-type": "application/json" } });
    expect(garbage.status()).toBe(400);
    expect(await garbage.text()).not.toMatch(GATE_CODE);
  });

  test("a driver asking for /admin/settings is redirected away", async ({ page }) => {
    await signInAs(page, "driver");

    const raw = await page.request.get("/admin/settings", { maxRedirects: 0 });
    expect([302, 303, 307, 308]).toContain(raw.status());
    expect(raw.headers()["location"] ?? "").toMatch(/\/account/);

    await page.goto("/admin/settings");
    await expect(page).toHaveURL(/\/account/);
    await expect(page.locator("main")).not.toContainText(/gate 1/i);
  });

  test("POST /auth/dev is the only way in; forged and tampered cookies are ignored", async ({ page, context, baseURL }) => {
    const url = baseURL!;

    // GET on the dev route is not a sign-in.
    const get = await page.request.get("/auth/dev", { maxRedirects: 0 });
    expect(get.status()).toBeGreaterThanOrEqual(400);
    expect(await whoAmI(page.request)).toBeNull();

    // The Google route has nothing to talk to in memory mode: it bounces back to /login, signs nobody in.
    const google = await page.request.get("/auth/login?next=%2Fadmin", { maxRedirects: 0 });
    expect([302, 303, 307, 308]).toContain(google.status());
    expect(google.headers()["location"] ?? "").toMatch(/\/login/);
    expect(await whoAmI(page.request)).toBeNull();

    // A cookie somebody typed up themselves.
    await context.addCookies([{ name: DEV_COOKIE, value: `${cookiePayload({ id: "x", email: DEV_EMAIL.admin, name: "Mallory" })}.bogus`, url }]);
    expect(await whoAmI(page.request)).toBeNull();
    await context.clearCookies();

    // A genuine driver cookie with the payload swapped for the owner's email: the signature no longer fits.
    const signIn = await page.request.post("/auth/dev", { form: { as: "driver" }, maxRedirects: 0 });
    expect(signIn.status()).toBe(303);
    const real = (await context.cookies()).find((c) => c.name === DEV_COOKIE);
    expect(real, "POST /auth/dev sets the session cookie").toBeTruthy();
    expect(real!.httpOnly).toBe(true);
    expect((await whoAmI(page.request))?.isAdmin).toBe(false);

    const signature = real!.value.split(".")[1] ?? "";
    await context.clearCookies();
    await context.addCookies([{ name: DEV_COOKIE, value: `${cookiePayload({ id: "00000000-0000-4000-8000-000000000002", email: DEV_EMAIL.admin, name: "Test Owner" })}.${signature}`, url }]);
    expect(await whoAmI(page.request)).toBeNull();
    await context.clearCookies();

    // Nothing the browser sends decides who is an owner: only the server's own list does.
    const owner = await page.request.post("/auth/dev", { form: { as: "admin" }, maxRedirects: 0 });
    expect(owner.status()).toBe(303);
    const me = await whoAmI(page.request);
    expect(me?.email).toBe(DEV_EMAIL.admin);
    expect(me?.isAdmin).toBe(true);
  });

  test("/api/me for an anonymous visitor is { user: null } and never cached", async ({ request }) => {
    const res = await request.get("/api/me");
    expect(res.status()).toBe(200);
    expect(await res.json()).toEqual({ user: null });
    expect(res.headers()["cache-control"] ?? "").toMatch(/no-store/);
  });

  test("a confirmation page shows only the last 4 digits of the phone", async ({ page, request }) => {
    const phone = "(940) 555-0142";
    const code = await reserveNightly(page, { name: "Dale Whitaker", phone });

    const text = await page.locator("body").innerText();
    expect(text).toMatch(/ending in 0142/i);
    expect(text).not.toMatch(/555[-\s.]?0142/);
    expect(text).not.toMatch(/9405550142/);
    // Not in the page source either (server-rendered props included).
    const html = await page.content();
    expect(html).not.toMatch(/555[-\s.]?0142/);

    // The lookup API says only the last 4 too.
    const found = await lookup(request, code, "0142");
    expect(found.status()).toBe(200);
    const body = await found.text();
    expect(body).not.toMatch(/555[-\s.]?0142/);
    expect((JSON.parse(body) as { phoneLast4: string }).phoneLast4).toBe("0142");
  });
});

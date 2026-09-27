import { defineConfig, devices } from "@playwright/test";

/**
 * Playwright drives the real site (`next start`) in memory mode on 127.0.0.1:3100.
 *
 * Environment knobs:
 *   SITE_DIR     where the site checkout lives (default ../Grandmas-trucklot-website-)
 *   SKIP_BUILD=1 don't run `next build` before starting — use the existing .next folder
 *   CI=1         one retry, never reuse a server that is already on the port, forbid test.only
 *   PW_CHROMIUM  absolute path to a Chromium binary. Only set this when Playwright cannot find
 *                its own browser. Locally Chromium 1194 (in PLAYWRIGHT_BROWSERS_PATH) matches
 *                @playwright/test 1.56.1, so this is normally left unset.
 */

const PORT = Number(process.env.SITE_PORT ?? 3100);
const BASE_URL = `http://127.0.0.1:${PORT}`;
const CI = Boolean(process.env.CI);

export default defineConfig({
  testDir: "./e2e",

  // The memory store is one shared process-wide state, so tests must not overlap.
  fullyParallel: false,
  workers: 1,

  retries: CI ? 1 : 0,
  forbidOnly: CI,
  timeout: 90_000,
  expect: { timeout: 10_000 },

  reporter: [["list"], ["html", { open: "never", outputFolder: "playwright-report" }]],
  outputDir: "test-results",

  use: {
    baseURL: BASE_URL,
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
    // Night mode follows the system by default; pin it so the toggle test is deterministic.
    colorScheme: "light",
    ...(process.env.PW_CHROMIUM ? { launchOptions: { executablePath: process.env.PW_CHROMIUM } } : {}),
  },

  projects: [
    {
      // A phone in a cab. Pixel-like: touch, mobile viewport, device scale.
      name: "mobile",
      use: {
        ...devices["Pixel 7"],
        viewport: { width: 390, height: 844 },
        isMobile: true,
        hasTouch: true,
      },
    },
    {
      // The owner at her kitchen-table laptop.
      name: "desktop",
      use: {
        ...devices["Desktop Chrome"],
        viewport: { width: 1280, height: 800 },
      },
    },
  ],

  webServer: {
    command: "bash scripts/start-site.sh",
    url: BASE_URL,
    reuseExistingServer: !CI,
    timeout: 180_000,
    stdout: "pipe",
    stderr: "pipe",
  },
});

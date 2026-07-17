// Capture the LIVE web reference (vite dev server on :5173) through its real
// user flow with Playwright headless — the ONLY trustworthy oracle for
// presentation parity. Code-level constant checks proved gameplay numbers but
// completely missed that the rendered UI (menu cards, HUD layout, popups)
// looks nothing like the native build (operator callout, 2026-07-17).
//
// Captures: menu → customize (after clicking Survival) → gameplay at several
// timestamps (wave 1 combat, wave popup window, death screen if reached).
// Frames land in build-ninja/webref/. Headless Chromium renders R3F fine —
// the rAF-throttling phantom only afflicts hidden tabs of a VISIBLE Chrome.
//
// Usage: bun scripts/webref_capture.ts [--url http://localhost:5173/]

import { chromium } from "playwright";

const url = process.argv.includes("--url")
    ? process.argv[process.argv.indexOf("--url") + 1]
    : "http://localhost:5173/";
const outDir = "build-ninja/webref";

const browser = await chromium.launch({ headless: true });
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
page.on("pageerror", (err) => console.log("PAGE ERROR:", String(err).slice(0, 200)));

await page.goto(url, { waitUntil: "domcontentloaded" });
await page.waitForTimeout(4000);
await page.screenshot({ path: `${outDir}/web_01_menu.png` });
console.log("menu captured");

// Click the Survival Mode card. The card is a styled div — target by text.
await page.getByText("Survival Mode", { exact: true }).click();
await page.waitForTimeout(2500);
await page.screenshot({ path: `${outDir}/web_02_customize.png` });
console.log("customize captured");

// Start the game. The customize screen's start control — target by text
// variants the web build might use.
const startButton = page
    .getByText(/start game|start survival|begin/i)
    .first();
await startButton.click();
await page.waitForTimeout(4000);
await page.screenshot({ path: `${outDir}/web_03_gameplay_early.png` });
console.log("gameplay early captured");

// Wave 1 combat in progress — enemies closing / contact.
await page.waitForTimeout(6000);
await page.screenshot({ path: `${outDir}/web_04_gameplay_combat.png` });
console.log("gameplay combat captured");

// Let the run play out unattended — captures whatever state the game is in
// (likely death screen; web has no i-frames so an idle cat dies fast).
await page.waitForTimeout(10000);
await page.screenshot({ path: `${outDir}/web_05_late.png` });
console.log("late frame captured");

await browser.close();
console.log("DONE");

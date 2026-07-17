// wavescan_guard.ts — RENDERED-proof regression guard for the between-waves
// transition panel.
//
// WHY THIS EXISTS (2026-07-17): the wave popup's update AND render were both
// gated on a UI state live play never enters, so the panel had never rendered
// once in a real run — while its state machine logged "popup shown" every
// wave. A code-level check can't catch that class; only pixels can. This
// guard runs a hidden autoplay session with a 1-second screenshot cadence,
// finds the shots taken INSIDE the popup window (from the engine log), and
// requires the web-parity green "WAVE N COMPLETE!" line to actually be
// present in at least one of them.
//
// PASS: exit 0 (green text found in an in-window shot).
// FAIL: exit 1 (wave never cleared in the time budget = inconclusive-fail,
//       or the panel pixels are missing = the regression this guards).
//
// Cost: one ~2-minute hidden run — meant for nightly / pre-release, not the
// per-commit gate. Single-instance: do not run while another game session
// is up (the engine exits 3).
//
// Usage: bun scripts/wavescan_guard.ts [--budget 110]

import { execFileSync } from "child_process";
import { existsSync, readFileSync, readdirSync, writeFileSync, mkdirSync } from "fs";
import { join, dirname } from "path";

const repoRoot = dirname(import.meta.dir);
const buildDir = join(repoRoot, "build-ninja");
const outDir = join(buildDir, "headless", "wavescan-guard");
const budget = process.argv.includes("--budget")
    ? Number(process.argv[process.argv.indexOf("--budget") + 1])
    : 110;

mkdirSync(outDir, { recursive: true });

// 1-second screenshot cadence across the whole budget: the wave-1 clear time
// varies with autoplay RNG (observed 34-77s), so blanket coverage is the only
// reliable way to land shots inside the ~5s popup window.
let script = "wait:24;";
for (let t = 24; t <= budget; t += 1) script += `screenshot:g${t};wait:1;`;
script += "quit";
const scriptPath = join(outDir, "script.txt");
writeFileSync(scriptPath, script);

console.log(`wavescan-guard: hidden autoplay run (~${budget + 10}s)...`);
execFileSync("bun", [
    join(repoRoot, "scripts", "headless_run.ts"),
    "--script-file", scriptPath,
    "--out", outDir,
    "--timeout", String(budget + 40),
    "--", "--autoplay",
], { cwd: repoRoot, stdio: "ignore", timeout: (budget + 60) * 1000 });

// Which shots landed inside the popup window? The engine logs the popup
// lifecycle; screenshots log their own timestamps in the same file.
const log = readFileSync(join(outDir, "run.log"), "utf8");
const shownAt = log.match(/(\d+:\d+:\d+\.\d+)\].*complete popup shown/)?.[1];
if (!shownAt) {
    console.log("FAIL (inconclusive): wave 1 never cleared inside the budget — rerun (autoplay RNG) or raise --budget");
    process.exit(1);
}
const shots = [...log.matchAll(/(\d+:\d+:\d+\.\d+)\].*screenshot '.*[\\/](g\d+)\.ppm/g)]
    .filter((m) => m[1] > shownAt)
    .slice(0, 4)
    .map((m) => m[2]);
if (shots.length === 0) {
    console.log("FAIL (inconclusive): no screenshot landed after the popup appeared");
    process.exit(1);
}

// Pixel oracle: the panel's headline renders in the web's pure green
// #00ff00 (WebParity::kWaveTransitionComplete). Nothing else in the scene
// produces saturated pure-green pixels — grass/trees are far darker and
// red-shifted. A couple hundred green pixels = the text is really there.
function greenCount(ppmPath: string): number {
    const buffer = readFileSync(ppmPath);
    let offset = 0; const tokens: string[] = [];
    while (tokens.length < 4) {
        while ([32, 9, 10, 13].includes(buffer[offset])) offset++;
        const start = offset;
        while (offset < buffer.length && ![32, 9, 10, 13].includes(buffer[offset])) offset++;
        tokens.push(buffer.subarray(start, offset).toString("ascii"));
    }
    offset++;
    let count = 0;
    for (let i = offset; i + 2 < buffer.length; i += 3) {
        if (buffer[i] < 100 && buffer[i + 1] > 200 && buffer[i + 2] < 100) count++;
    }
    return count;
}

let best = { name: "", count: 0 };
for (const name of shots) {
    const path = join(outDir, `${name}.ppm`);
    if (!existsSync(path)) continue;
    const count = greenCount(path);
    if (count > best.count) best = { name, count };
    console.log(`  ${name}: ${count} pure-green pixels`);
}

const THRESHOLD = 200; // the headline alone is thousands of pixels at 1080p
if (best.count >= THRESHOLD) {
    console.log(`PASS: transition panel rendered (${best.name}, ${best.count} green pixels)`);
    process.exit(0);
}
console.log(`FAIL: popup state fired but the PANEL never rendered (max ${best.count} green pixels < ${THRESHOLD}) — the dead-gate class is back`);
process.exit(1);

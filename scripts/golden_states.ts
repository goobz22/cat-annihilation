// golden_states — per-state golden-image probe (menu / customize / playing /
// pause / gameover).
//
// Extends the smoke golden-image idea (tests/golden/smoke.ppm + the SSIM ctest
// gate) to one canonical screenshot per GAME STATE, so a silent regression in
// any screen (menu layout gone, pause modal missing, game-over overlay broken,
// world render black) surfaces as a machine diff instead of a human noticing.
//
// Design decisions:
// - Capture at the canonical 1920x1080 (every menu click coordinate is
//   calibrated there — a smaller window moves the layout and the script's
//   clicks miss), then DOWNSAMPLE 3x to 640x360 with a box filter before
//   storing/comparing. Downsampling keeps the checked-in baselines ~690 KB
//   each and absorbs subpixel jitter.
// - Comparator is mean-absolute-error, global + per-32px-block max. The
//   in-game states are NOT pixel-deterministic (dog spawn radii are random,
//   idle animations advance), so thresholds are per-state: tight for the
//   near-static menus, loose for live scenes — loose still catches the gross
//   failures this probe exists for (black frame, missing overlay, color
//   explosion), while the block-max catches a localized hole a global mean
//   would wash out.
// - ON-DEMAND / nightly probe, not a per-commit gate stage: swapchain output
//   varies across GPU/driver versions, so a baseline is only valid on the
//   machine that produced it. (Same policy tradeoff as the ctest smoke gate,
//   which WARN-skips when no candidate exists.)
//
//   bun scripts/golden_states.ts            # capture + compare vs baselines
//   bun scripts/golden_states.ts --update   # capture + overwrite baselines
//   bun scripts/golden_states.ts --selftest # prove the comparator works
//
// Exit 0 = all states within threshold; 1 = a diff; 2 = usage/infra error.

import { existsSync, mkdirSync, readFileSync, writeFileSync, rmSync } from "fs";
import { spawnSync } from "child_process";
import { join, dirname } from "path";

const repoRoot = dirname(import.meta.dir);
const goldenDir = join(repoRoot, "tests", "golden", "states");
const captureDir = join(repoRoot, "build-ninja", "headless", "golden-states");

// One state -> { script segment reaching it, MAE thresholds }. Menus are
// near-static (tight); live scenes vary run-to-run (loose).
const STATES: { name: string; globalMae: number; blockMae: number }[] = [
    { name: "menu", globalMae: 4, blockMae: 30 },
    { name: "customize", globalMae: 4, blockMae: 30 },
    { name: "playing", globalMae: 20, blockMae: 120 },
    { name: "pause", globalMae: 16, blockMae: 100 },
    { name: "gameover", globalMae: 20, blockMae: 120 },
];

const CAPTURE_SCRIPT = [
    "wait:3", "expect:state=MainMenu", "screenshot:golden_menu",
    "click:0.39,0.48", "wait:1.2", "screenshot:golden_customize",
    "click:0.654,0.664", "wait:2", "expect:state=Playing", "wait:1",
    "screenshot:golden_playing",
    "key:escape", "wait:1", "expect:state=Paused", "screenshot:golden_pause",
    "key:escape", "wait:0.5", "killplayer", "wait:1.5",
    "expect:state=GameOver", "screenshot:golden_gameover",
    "quit",
].join(";");

type Image = { width: number; height: number; pixels: Uint8Array };

function readPpm(path: string): Image {
    const buf = readFileSync(path);
    // P6\n<w> <h>\n255\n<raw>
    const header = buf.subarray(0, 64).toString("latin1");
    const match = /^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(header);
    if (!match) throw new Error(`not a binary P6 PPM: ${path}`);
    const width = parseInt(match[1], 10);
    const height = parseInt(match[2], 10);
    const dataStart = match[0].length;
    const pixels = new Uint8Array(buf.subarray(dataStart, dataStart + width * height * 3));
    if (pixels.length !== width * height * 3) throw new Error(`truncated PPM: ${path}`);
    return { width, height, pixels };
}

function writePpm(path: string, image: Image): void {
    const header = Buffer.from(`P6\n${image.width} ${image.height}\n255\n`, "latin1");
    writeFileSync(path, Buffer.concat([header, Buffer.from(image.pixels)]));
}

// 3x box-filter downsample (exact for 1920x1080 -> 640x360).
function downsample3x(src: Image): Image {
    const w = Math.floor(src.width / 3), h = Math.floor(src.height / 3);
    const out = new Uint8Array(w * h * 3);
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            for (let c = 0; c < 3; c++) {
                let sum = 0;
                for (let dy = 0; dy < 3; dy++) {
                    for (let dx = 0; dx < 3; dx++) {
                        sum += src.pixels[((y * 3 + dy) * src.width + (x * 3 + dx)) * 3 + c];
                    }
                }
                out[(y * w + x) * 3 + c] = Math.round(sum / 9);
            }
        }
    }
    return { width: w, height: h, pixels: out };
}

// Global MAE + the worst 32x32-block MAE.
function compare(a: Image, b: Image): { globalMae: number; blockMae: number } {
    if (a.width !== b.width || a.height !== b.height) {
        return { globalMae: 255, blockMae: 255 };
    }
    let total = 0;
    const block = 32;
    const blocksX = Math.ceil(a.width / block), blocksY = Math.ceil(a.height / block);
    const blockSums = new Float64Array(blocksX * blocksY);
    const blockCounts = new Float64Array(blocksX * blocksY);
    for (let y = 0; y < a.height; y++) {
        for (let x = 0; x < a.width; x++) {
            const i = (y * a.width + x) * 3;
            const d = Math.abs(a.pixels[i] - b.pixels[i]) +
                      Math.abs(a.pixels[i + 1] - b.pixels[i + 1]) +
                      Math.abs(a.pixels[i + 2] - b.pixels[i + 2]);
            total += d;
            const bi = Math.floor(y / block) * blocksX + Math.floor(x / block);
            blockSums[bi] += d;
            blockCounts[bi] += 3;
        }
    }
    let worstBlock = 0;
    for (let i = 0; i < blockSums.length; i++) {
        if (blockCounts[i] > 0) worstBlock = Math.max(worstBlock, blockSums[i] / blockCounts[i]);
    }
    return { globalMae: total / (a.width * a.height * 3), blockMae: worstBlock };
}

function capture(): boolean {
    rmSync(captureDir, { recursive: true, force: true });
    const proc = spawnSync("bun", [
        join(repoRoot, "scripts", "headless_run.ts"),
        "--script", CAPTURE_SCRIPT,
        "--out", captureDir,
        "--timeout", "60",
    ], { encoding: "utf8", windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
    if (proc.status !== 0) {
        console.error("golden_states: capture run FAILED — headless_run output tail:");
        console.error((proc.stdout ?? "").split("\n").slice(-8).join("\n"));
        return false;
    }
    return true;
}

function selftest(): number {
    // 64x64 spans a 2x2 grid of 32px blocks, so a corruption confined to one
    // block can spike blockMae above the global mean (a 6x6 fixture would fit
    // in ONE block and the two metrics would be equal by construction).
    const side = 64;
    const base: Image = { width: side, height: side, pixels: new Uint8Array(side * side * 3).fill(100) };
    const same = compare(base, base);
    const shifted: Image = { width: side, height: side, pixels: new Uint8Array(side * side * 3).fill(110) };
    const off = compare(base, shifted);
    const hole: Image = { width: side, height: side, pixels: new Uint8Array(side * side * 3).fill(100) };
    for (let y = 0; y < 32; y++) {
        // corrupt only the top-left 32x32 block
        hole.pixels.fill(255, (y * side) * 3, (y * side + 32) * 3);
    }
    const local = compare(base, hole);
    const wrongDims = compare(base, { width: 32, height: side, pixels: new Uint8Array(32 * side * 3) });
    let ok = true;
    if (same.globalMae !== 0 || same.blockMae !== 0) { console.error("selftest FAIL: identical images diff nonzero"); ok = false; }
    if (off.globalMae < 9 || off.globalMae > 11) { console.error("selftest FAIL: uniform +10 shift MAE " + off.globalMae); ok = false; }
    if (local.blockMae <= local.globalMae) { console.error("selftest FAIL: localized hole should spike blockMae above globalMae"); ok = false; }
    if (wrongDims.globalMae !== 255) { console.error("selftest FAIL: dimension mismatch must return 255"); ok = false; }
    console.log(ok ? "selftest PASS" : "selftest FAILED");
    return ok ? 0 : 2;
}

function main(): number {
    if (process.argv.includes("--selftest")) return selftest();
    const update = process.argv.includes("--update");
    if (!capture()) return 2;
    mkdirSync(goldenDir, { recursive: true });
    let failures = 0;
    for (const state of STATES) {
        const candidatePath = join(captureDir, `golden_${state.name}.ppm`);
        if (!existsSync(candidatePath)) {
            console.error(`golden_states: MISSING capture for '${state.name}' — the drive script broke`);
            failures++;
            continue;
        }
        const candidate = downsample3x(readPpm(candidatePath));
        const baselinePath = join(goldenDir, `${state.name}.ppm`);
        if (update || !existsSync(baselinePath)) {
            writePpm(baselinePath, candidate);
            console.log(`golden_states: ${update ? "updated" : "created"} baseline '${state.name}' (${candidate.width}x${candidate.height})`);
            continue;
        }
        const baseline = readPpm(baselinePath);
        const { globalMae, blockMae } = compare(baseline, candidate);
        const pass = globalMae <= state.globalMae && blockMae <= state.blockMae;
        console.log(`  ${pass ? "✓" : "✗"} ${state.name}: globalMae=${globalMae.toFixed(2)} (<=${state.globalMae}) blockMae=${blockMae.toFixed(2)} (<=${state.blockMae})`);
        if (!pass) failures++;
    }
    if (failures) {
        console.error(`golden_states: ${failures} state(s) diverged from baseline (candidates in ${captureDir})`);
        return 1;
    }
    console.log("golden_states: all states match their baselines");
    return 0;
}

process.exit(main());

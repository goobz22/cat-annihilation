// One-command headless game run: launch hidden, drive an input script,
// collect every artifact, and print a verdict a human (or agent) can read
// in five lines instead of grepping three files.
//
// This is the front door for ALL interactive testing on this machine — the
// owner directive (2026-07-16) is that automated runs must NEVER pop a
// window or touch the real cursor. The engine side of that contract is
// --hidden + --input-script (see game/main.cpp); this wrapper adds the
// operational half: a per-run output directory, automatic PPM→PNG
// conversion so screenshots are viewable, a parsed state-log summary
// (transitions + final state), surfaced EXPECT verdicts, and exit-code
// interpretation (0 ok, 3 another-instance, 4 assertions failed).
//
// Usage:
//   bun scripts/headless_run.ts --script "wait:3;screenshot:menu;expect:state=MainMenu;quit"
//   bun scripts/headless_run.ts --script-file tests/scripts/menu_flow.txt --out runs/menu_flow
//   Extra engine flags pass through after `--`:
//   bun scripts/headless_run.ts --script "..." -- --starting-wave 5
//
// Output layout (default out dir: build-ninja/headless/<timestamp>):
//   run.log       full engine log (--log-file)
//   state.jsonl   1 Hz + per-transition game-state timeline (--state-log)
//   *.ppm/*.png   every screenshot: checkpoint, PNG auto-converted
//   exit.ppm/.png final frame (--frame-dump)

import { existsSync, mkdirSync, readFileSync, readdirSync } from "fs";
import { spawnSync } from "child_process";
import { join, dirname, resolve } from "path";
import { convertPpmToPng } from "./ppm_to_png";

const repoRoot = dirname(import.meta.dir);
const buildDir = join(repoRoot, "build-ninja");
const exePath = join(buildDir, "CatAnnihilation.exe");

function fail(message: string): never {
    console.error(`headless_run: ${message}`);
    process.exit(2);
}

// ---- argv parsing ----------------------------------------------------------
const argv = process.argv.slice(2);
let script = "";
let outDir = "";
let timeoutSeconds = 120;
const passThrough: string[] = [];
for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--script") script = argv[++i] ?? "";
    else if (arg === "--script-file") script = readFileSync(argv[++i], "utf8").replace(/\r?\n/g, "").trim();
    else if (arg === "--out") outDir = argv[++i] ?? "";
    else if (arg === "--timeout") timeoutSeconds = Number(argv[++i]);
    else if (arg === "--") { passThrough.push(...argv.slice(i + 1)); break; }
    else fail(`unknown arg '${arg}' (use --script, --script-file, --out, --timeout, -- <engine flags>)`);
}
if (!script) fail("--script or --script-file is required");
if (!existsSync(exePath)) fail(`${exePath} not built — run: ninja -C build-ninja CatAnnihilation`);
if (!/(^|;)quit(;|$)/.test(script)) {
    // Without a quit the loop never ends and the wrapper timeout SIGKILLs
    // the run — the frame dump and clean-shutdown checks would be lost.
    fail("script has no 'quit' command — the run would only end by timeout kill");
}

if (!outDir) {
    const stamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
    outDir = join(buildDir, "headless", stamp);
}
// Absolute: the engine runs with cwd=build-ninja (asset resolution), so a
// relative --out would silently split the artifacts between two trees.
outDir = resolve(outDir);
mkdirSync(outDir, { recursive: true });

// ---- launch ----------------------------------------------------------------
const engineArgs = [
    "--hidden",
    "--input-script", script,
    "--dump-dir", outDir,
    "--state-log", join(outDir, "state.jsonl"),
    "--log-file", join(outDir, "run.log"),
    "--frame-dump", join(outDir, "exit.ppm"),
    ...passThrough,
];
console.log(`headless_run: launching hidden (out: ${outDir})`);
const result = spawnSync(exePath, engineArgs, {
    cwd: buildDir,               // engine resolves assets/ + shaders/ relative to cwd
    timeout: timeoutSeconds * 1000,
    stdio: "ignore",             // everything of value goes to run.log
});
const exitCode = result.status;
const timedOut = result.error?.name === "Error" && String(result.error?.message ?? "").includes("ETIMEDOUT");

// ---- collect: PPM → PNG ------------------------------------------------------
const pngNames: string[] = [];
for (const name of readdirSync(outDir)) {
    if (!name.endsWith(".ppm")) continue;
    const pngName = name.replace(/\.ppm$/, ".png");
    try {
        convertPpmToPng(join(outDir, name), join(outDir, pngName));
        pngNames.push(pngName);
    } catch (error) {
        console.error(`  ppm->png failed for ${name}: ${error}`);
    }
}

// ---- summarize: state timeline ----------------------------------------------
type StateRow = {
    t: number; event: string; state: string; wave?: number;
    enemiesRemaining?: number; kills?: number; hp?: number; maxHp?: number;
    alive?: boolean; level?: number; fps?: number;
};
const statePath = join(outDir, "state.jsonl");
let rows: StateRow[] = [];
if (existsSync(statePath)) {
    rows = readFileSync(statePath, "utf8")
        .split("\n").filter(Boolean)
        .map((line) => { try { return JSON.parse(line) as StateRow; } catch { return null; } })
        .filter((row): row is StateRow => row !== null);
}
const transitions = rows.filter((row) => row.event === "transition");
const finalRow = rows[rows.length - 1];
const fpsSamples = rows.map((row) => row.fps ?? 0).filter((fps) => fps > 0);
const minFps = fpsSamples.length ? Math.min(...fpsSamples) : 0;

// ---- summarize: expect verdicts + errors from the engine log -----------------
const logPath = join(outDir, "run.log");
const logText = existsSync(logPath) ? readFileSync(logPath, "utf8") : "";
const expectLines = logText.split("\n").filter((line) => line.includes("EXPECT "));
const expectFails = expectLines.filter((line) => line.includes("EXPECT FAIL"));
const errorLines = logText.split("\n").filter(
    (line) => line.includes("[ERROR]") && !line.includes("EXPECT FAIL"));

// ---- verdict -----------------------------------------------------------------
console.log("");
console.log(`exit code : ${timedOut ? "TIMEOUT (killed)" : exitCode}` +
    (exitCode === 4 ? "  (expect assertions failed)" :
     exitCode === 3 ? "  (another instance already running)" : ""));
if (transitions.length) {
    console.log("timeline  :");
    for (const row of transitions) {
        console.log(`  t=${row.t.toFixed(1)}s  -> ${row.state}` +
            (row.wave !== undefined ? `  wave=${row.wave} enemies=${row.enemiesRemaining} hp=${row.hp}/${row.maxHp} kills=${row.kills} lvl=${row.level}` : ""));
    }
}
if (finalRow) {
    console.log(`final     : t=${finalRow.t.toFixed(1)}s state=${finalRow.state} wave=${finalRow.wave} hp=${finalRow.hp}/${finalRow.maxHp} kills=${finalRow.kills} lvl=${finalRow.level}`);
    console.log(`fps       : min=${minFps.toFixed(1)} across ${fpsSamples.length} samples`);
}
for (const line of expectLines) {
    const trimmed = line.substring(line.indexOf("[input-script]"));
    console.log(`${trimmed.includes("FAIL") ? "  ✗ " : "  ✓ "}${trimmed}`);
}
if (errorLines.length) {
    console.log(`errors    : ${errorLines.length} [ERROR] line(s) in run.log (first below)`);
    console.log(`  ${errorLines[0].trim()}`);
}
console.log(`artifacts : ${outDir}`);
console.log(`  ${pngNames.sort().join(", ") || "(no screenshots)"}`);

// Absence is not a status: a green exit code with MISSING evidence is a
// broken harness, not a pass (this exact shape shipped once — a relative
// --out sent every artifact to a different directory and the verdict still
// printed PASS). If the script declared expectations, the log must contain
// their verdict lines; the state timeline must exist for any run at all.
const evidenceGaps: string[] = [];
if (rows.length === 0) evidenceGaps.push("state.jsonl missing or empty");
const declaredExpects = (script.match(/(^|;)expect:/g) ?? []).length;
if (declaredExpects > 0 && expectLines.length < declaredExpects) {
    evidenceGaps.push(`script declares ${declaredExpects} expect(s), log shows ${expectLines.length} verdict(s)`);
}
if (evidenceGaps.length) console.log(`evidence  : MISSING — ${evidenceGaps.join("; ")}`);

const verdictOk = exitCode === 0 && !timedOut && expectFails.length === 0 &&
    evidenceGaps.length === 0;
console.log(`verdict   : ${verdictOk ? "PASS" : "FAIL"}`);
process.exit(verdictOk ? 0 : 1);

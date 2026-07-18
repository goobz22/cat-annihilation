// lint-windows-hide — enumerate the "child process pops a console window on
// the operator's desktop" class and fail if any spawn site lacks the fix.
//
// THE CLASS (2026-07-18, operator directive): on Windows, child_process
// spawn/spawnSync shows the child's console window unless the options include
// `windowsHide: true`. The harness spawns the console-subsystem game exe plus
// ninja/bun/git children constantly, so every gate/headless run flashed cmd
// windows on the operator's desktop ("make it run in the background so i dont
// see cmd prompt opening up"). This is the same never-visible-test-windows
// directive that made the game itself take --hidden (2026-07-16).
//
// This lint scans every scripts/*.ts for spawn(/spawnSync( calls and requires
// `windowsHide` within the call's argument span. (RegExp .exec() and similar
// are not spawns and are ignored.) A new script that spawns without hiding
// fails the gate.
//
//   bun scripts/lint-windows-hide.ts            # lint the real files
//   bun scripts/lint-windows-hide.ts --selftest # prove the detector works
//
// Exit 0 = clean; 1 = a violation (prints file:line); 2 = usage/self error.

import { readFileSync, readdirSync } from "fs";
import { join, dirname, resolve } from "path";

const repoRoot = dirname(import.meta.dir);
const scriptsDir = resolve(repoRoot, "scripts");

// A child_process spawn call: `spawnSync(` or bare `spawn(` not preceded by
// a word character or dot (filters out RegExp .exec-style methods and
// identifiers like respawn()).
const SPAWN_CALL = /(?<![\w.])spawn(?:Sync)?\s*\(/g;

type Violation = { file: string; line: number; text: string };

// From the call's opening paren, take the balanced-paren span and require
// `windowsHide` inside it. Balanced-paren matching is robust to multi-line
// option objects; string-literal parens are rare in these calls and at worst
// make the span longer (never shorter), which cannot produce a false PASS.
function callSpan(source: string, openParen: number): string {
  let depth = 0;
  for (let i = openParen; i < source.length; i++) {
    if (source[i] === "(") depth++;
    else if (source[i] === ")") {
      depth--;
      if (depth === 0) return source.slice(openParen, i + 1);
    }
  }
  return source.slice(openParen);
}

function lintSource(source: string, label: string): Violation[] {
  const violations: Violation[] = [];
  let m: RegExpExecArray | null;
  SPAWN_CALL.lastIndex = 0;
  while ((m = SPAWN_CALL.exec(source)) !== null) {
    // Skip occurrences inside line comments.
    const lineStart = source.lastIndexOf("\n", m.index) + 1;
    const linePrefix = source.slice(lineStart, m.index);
    if (linePrefix.includes("//")) continue;
    const openParen = source.indexOf("(", m.index);
    const span = callSpan(source, openParen);
    if (!span.includes("windowsHide")) {
      const line = source.slice(0, m.index).split("\n").length;
      violations.push({
        file: label,
        line,
        text: source.slice(lineStart, source.indexOf("\n", m.index) === -1 ? undefined : source.indexOf("\n", m.index)).trim(),
      });
    }
  }
  return violations;
}

function selftest(): number {
  const good = `
const a = spawnSync('git', ['status'], { encoding: 'utf8', windowsHide: true });
const b = spawn(exe, args, {
  stdio: 'ignore',
  windowsHide: true,
});
regex.exec(source); // not a spawn
// spawnSync('cmd', ['/c']) in a comment is ignored`;
  const bad = `
const a = spawnSync('cmd', ['/c', 'mkdir', 'x'], { stdio: 'ignore' });`;
  const g = lintSource(good, "<good>");
  const b = lintSource(bad, "<bad>");
  let ok = true;
  if (g.length !== 0) { console.error(`selftest FAIL: good fixture flagged ${g.length}`); ok = false; }
  if (b.length !== 1) { console.error(`selftest FAIL: bad fixture flagged ${b.length} (expected 1)`); ok = false; }
  console.log(ok ? "selftest PASS" : "selftest FAILED");
  return ok ? 0 : 2;
}

function main(): number {
  if (process.argv.includes("--selftest")) return selftest();
  let files: string[];
  try {
    // Exclude this lint itself: its detection regex and selftest fixtures
    // contain spawn-shaped text that is not a real spawn call.
    files = readdirSync(scriptsDir).filter(
      (f) => f.endsWith(".ts") && f !== "lint-windows-hide.ts");
  } catch (error) {
    console.error(`lint-windows-hide: cannot read ${scriptsDir}: ${error}`);
    return 2;
  }
  const violations: Violation[] = [];
  for (const f of files) {
    const source = readFileSync(join(scriptsDir, f), "utf8");
    violations.push(...lintSource(source, join("scripts", f)));
  }
  if (violations.length === 0) {
    console.log("lint-windows-hide: OK — every child-process spawn passes windowsHide (no console windows on the operator's desktop)");
    return 0;
  }
  for (const v of violations) {
    console.error(`${v.file}:${v.line}: spawn without windowsHide ('${v.text}') — on Windows this pops a visible console window on the operator's desktop; add windowsHide: true to the options.`);
  }
  console.error(`lint-windows-hide: ${violations.length} violation(s)`);
  return 1;
}

process.exit(main());

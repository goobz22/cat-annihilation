// lint-skill-level-cap — enumerate the "progression cap not reconciled with the
// web's 99-max under parity" bug class and fail if any bare-literal cap remains.
//
// THE CLASS (2026-07-17 Round-4 audit, MED x3): the web levels the cat main
// level, the sword/bow weapon skills, and the per-element magic skills all to
// MAX_LEVEL = 99 with an unbounded level-up loop (gameStore.ts:735-760 /
// :836-842 / :805-812). Native left three DIFFERENT hardcoded caps behind —
// cat 50, weapon 20, magic 15 — even though the native XP curves already served
// 1-99 under WebParity::kEnabled (xp_tables.hpp). A sword/magic-main player froze
// early while the web build kept climbing. The audit finders flagged only the
// weapon (20) and magic (15) instances; the CAT-LEVEL (50) instance was surfaced
// only by enumerating the class — which is exactly why this lint exists.
//
// The fix routes every cap through a named constant (kMaxCatLevel /
// kMaxWeaponSkillLevel / kMaxElementalSkillLevel, each = WebParity::kMaxLevel
// under parity). This lint forbids a BARE-INTEGER skill-level cap
// (`.level >= 50`, `->level < 20`, ...) anywhere in leveling_system.cpp, so a
// future fourth skill type cannot silently reintroduce a divergent literal cap.
//
//   bun scripts/lint-skill-level-cap.ts            # lint the real file
//   bun scripts/lint-skill-level-cap.ts --selftest # prove the detector works
//
// Exit 0 = clean; 1 = a violation (prints file:line); 2 = usage/self error.

import { readFileSync } from "fs";
import { dirname, join, resolve } from "path";

const repoRoot = dirname(import.meta.dir);
const target = resolve(repoRoot, "game", "systems", "leveling_system.cpp");

// A skill/level cap comparison against a BARE integer literal: `.level >= 50`,
// `->level < 20`. Ability-unlock checks use `==` and are not caps, so only
// `>=`/`<`/`>`/`<=` against a literal are flagged (the cap shapes).
const BARE_CAP = /(?:\.|->)\s*level\s*(?:>=|<=|>|<)\s*[0-9]+/;

type Violation = { line: number; text: string };

function lintSource(source: string): Violation[] {
  const violations: Violation[] = [];
  const lines = source.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const code = lines[i].replace(/\/\/.*$/, "").replace(/\/\*.*?\*\//g, "");
    if (BARE_CAP.test(code)) {
      violations.push({ line: i + 1, text: lines[i].trim() });
    }
  }
  return violations;
}

function selftest(): number {
  const guarded = `
bool LevelingSystem::addXP(int amount) {
    if (stats_.level >= kMaxCatLevel) return false;
    while (stats_.xp >= stats_.xpToNextLevel && stats_.level < kMaxCatLevel) {}
    if (newLevel == 5) { /* ability unlock, not a cap */ }
}`;
  const unguarded = guarded.replace(
    "if (stats_.level >= kMaxCatLevel) return false;",
    "if (stats_.level >= 50) return false;",
  );

  const gv = lintSource(guarded);
  const uv = lintSource(unguarded);
  let ok = true;
  if (gv.length !== 0) {
    console.error(
      `selftest FAIL: guarded fixture flagged ${gv.length} (expected 0). Named-constant caps and == ability checks must NOT trip.`,
    );
    ok = false;
  }
  if (uv.length !== 1) {
    console.error(
      `selftest FAIL: unguarded fixture flagged ${uv.length} (expected exactly 1 bare-literal cap).`,
    );
    ok = false;
  }
  console.log(ok ? "selftest PASS" : "selftest FAILED");
  return ok ? 0 : 2;
}

function main(): number {
  if (process.argv.includes("--selftest")) return selftest();

  let source: string;
  try {
    source = readFileSync(target, "utf8");
  } catch (error) {
    console.error(`lint-skill-level-cap: cannot read ${target}: ${error}`);
    return 2;
  }

  const violations = lintSource(source);
  if (violations.length === 0) {
    console.log(
      "lint-skill-level-cap: OK — every skill/level cap routes through a named constant (no bare-literal caps)",
    );
    return 0;
  }
  for (const v of violations) {
    console.error(
      `${join("game", "systems", "leveling_system.cpp")}:${v.line}: bare-literal skill-level cap ('${v.text}') — route it through a named kMax*Level constant (= WebParity::kMaxLevel under parity) so it can't diverge from the web's 99-max.`,
    );
  }
  console.error(`lint-skill-level-cap: ${violations.length} violation(s)`);
  return 1;
}

process.exit(main());

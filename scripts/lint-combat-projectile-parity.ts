// lint-combat-projectile-parity — pin the fix for the 2026-07-18 bow-arrow
// hit-radius parity bug and guard the class.
//
// THE BUG: CombatSystem::updateProjectiles tested arrow-vs-enemy collision with
// the bare native member projectileHitRadius_ (1.0), NOT the web-parity
// WebParity::kProjectileHitRadius (1.5) the web gates every projectile hit on
// (GlobalCollisionSystem.tsx). Combined with the +1 arrow spawn height over
// ground-anchored dogs and the 3D distance test, that shrank the horizontal hit
// window to ~0.3 units and made arrows sail past enemies the web build would
// hit. The sibling spell path already consumed the parity constant; the bow path
// was left native. CombatSystem is ECS-coupled and not linked into unit_tests,
// so this structural check is the regression: every checkProjectileHit() call in
// CombatSystem.cpp must pass a PARITY-branched radius (kProjectileHitRadius under
// kEnabled), never the bare projectileHitRadius_ member.
//
//   bun scripts/lint-combat-projectile-parity.ts            # lint the real file
//   bun scripts/lint-combat-projectile-parity.ts --selftest # prove the detector
//
// Exit 0 = clean; 1 = a violation (prints file:line); 2 = usage/self error.

import { readFileSync } from "fs";
import { dirname, join, resolve } from "path";

const repoRoot = dirname(import.meta.dir);
const target = resolve(repoRoot, "game", "systems", "CombatSystem.cpp");

// A checkProjectileHit(...) call that passes the bare native member as the
// radius argument (3rd arg) — i.e. NOT a parity-branched value.
const BARE_RADIUS_CALL = /checkProjectileHit\s*\([^;]*\bprojectileHitRadius_\s*\)/;
// The parity constant must be referenced somewhere in the file (the branch exists).
const PARITY_CONST = /WebParity::kProjectileHitRadius/;

type Violation = { line: number; text: string };

function lintSource(source: string): { violations: Violation[]; hasParity: boolean } {
  const violations: Violation[] = [];
  const lines = source.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const code = lines[i].replace(/\/\/.*$/, "");
    if (BARE_RADIUS_CALL.test(code)) {
      violations.push({ line: i + 1, text: lines[i].trim() });
    }
  }
  return { violations, hasParity: PARITY_CONST.test(source) };
}

function selftest(): number {
  const good = `
const float hitRadius = WebParity::kEnabled ? WebParity::kProjectileHitRadius : projectileHitRadius_;
if (checkProjectileHit(projectile.position, transform->position, hitRadius)) { }`;
  const bad = `
if (checkProjectileHit(projectile.position, transform->position, projectileHitRadius_)) { }`;
  const g = lintSource(good);
  const b = lintSource(bad);
  let ok = true;
  if (g.violations.length !== 0 || !g.hasParity) {
    console.error(`selftest FAIL: good fixture flagged ${g.violations.length} / hasParity=${g.hasParity}`);
    ok = false;
  }
  if (b.violations.length !== 1) {
    console.error(`selftest FAIL: bad fixture flagged ${b.violations.length} (expected 1 bare-radius call)`);
    ok = false;
  }
  console.log(ok ? "selftest PASS" : "selftest FAILED");
  return ok ? 0 : 2;
}

// --- Enemy i-frame transparency (2026-07-18, the bow+spell class) -----------
// The shared HealthComponent invincibility window is native's SWORD gate; the
// web applies NO enemy-side cooldown to arrows or spell impacts. These pins
// require: (a) the projectile early-out is parity-gated (arrows fly through
// the window under kEnabled); (b) the bow applyDamage call passes the bypass;
// (c) applySpellDamage passes the bypass to applyDamageWithType. The bypass
// SEMANTICS (transparent, window-preserving) are unit-pinned on the real
// HealthComponent in test_web_parity_config.cpp — these structural pins cover
// the un-linkable call sites.
const IFRAME_EARLYOUT_GATED = /!WebParity::kEnabled\s*&&\s*isInvincible\s*\(\s*target\s*\)/;
const BOW_BYPASS = /applyDamage\s*\([^;]*ignoreTargetIFrame[^;]*WebParity::kEnabled\s*\)/s;
const SPELL_BYPASS = /applyDamageWithType\s*\([^;]*ignoreTargetIFrame[^;]*WebParity::kEnabled\s*\)/s;

function checkIFrameParity(combatSrc: string, spellSrc: string): string[] {
  const problems: string[] = [];
  if (!IFRAME_EARLYOUT_GATED.test(combatSrc)) {
    problems.push(
      "CombatSystem.cpp: the projectile isInvincible(target) early-out is not parity-gated ('!WebParity::kEnabled && isInvincible(target)') — under parity arrows must fly through the enemy's sword-gate window.",
    );
  }
  if (!BOW_BYPASS.test(combatSrc)) {
    problems.push(
      "CombatSystem.cpp: the bow projectile applyDamage call does not pass ignoreTargetIFrame=WebParity::kEnabled — a second arrow within 0.5s is silently absorbed by the enemy's sword-gate window.",
    );
  }
  if (!SPELL_BYPASS.test(spellSrc)) {
    problems.push(
      "elemental_magic.cpp: applySpellDamage's applyDamageWithType call does not pass ignoreTargetIFrame=WebParity::kEnabled — a spell right after a sword hit is silently absorbed.",
    );
  }
  return problems;
}

function main(): number {
  if (process.argv.includes("--selftest")) return selftest();
  let source: string;
  let spellSource: string;
  try {
    source = readFileSync(target, "utf8");
    spellSource = readFileSync(
      resolve(repoRoot, "game", "systems", "elemental_magic.cpp"), "utf8");
  } catch (error) {
    console.error(`lint-combat-projectile-parity: cannot read sources: ${error}`);
    return 2;
  }
  const { violations, hasParity } = lintSource(source);
  if (!hasParity) {
    console.error(
      `${join("game", "systems", "CombatSystem.cpp")}: WebParity::kProjectileHitRadius is never referenced — the bow projectile path must branch on it under parity (web gates hits on distance < 1.5).`,
    );
    return 1;
  }
  for (const v of violations) {
    console.error(
      `${join("game", "systems", "CombatSystem.cpp")}:${v.line}: checkProjectileHit passes the bare native projectileHitRadius_ ('${v.text}') — use the parity-branched radius (WebParity::kProjectileHitRadius under kEnabled) so arrows match the web's 1.5 hit window.`,
    );
  }
  const iframeProblems = checkIFrameParity(source, spellSource);
  for (const p of iframeProblems) console.error(p);
  if (violations.length === 0 && iframeProblems.length === 0) {
    console.log(
      "lint-combat-projectile-parity: OK — parity radius in use AND arrows/spells are transparent to the enemy sword-gate window",
    );
    return 0;
  }
  console.error(`lint-combat-projectile-parity: ${violations.length + iframeProblems.length} violation(s)`);
  return 1;
}

process.exit(main());

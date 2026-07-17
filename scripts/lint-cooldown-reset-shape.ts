// lint-cooldown-reset-shape — enumerate the "state transition re-arms a rate
// limit" bug class and fail if any instance reappears.
//
// THE CLASS (2026-07-17 Round-3 audit, MED): a per-actor cooldown timer is a
// RATE LIMIT — it must only be re-armed by the rate-limited action firing, never
// by a STATE TRANSITION. EnemyAISystem::transitionToState zeroed
// enemy.attackCooldownTimer on every entry into the Attacking state, so with the
// parity hysteresis band (enter at dist<=1.2, leave at >1.44) + zero enemy melee
// i-frames, kiting a dog out and back in re-cleared its cooldown and re-fired
// 15 dmg inside the web's 1.0s floor. The correct design: the timer initializes
// to 0 (so the FIRST action is immediate) and only a fire re-arms it; it survives
// state transitions. The web enforces exactly this (gate every swing on
// currentTime-lastAttackTime>=1000ms, written only on a fire).
//
// This lint scans every state-transition function (transitionToState / enter*State
// / *EnterState*) in game/systems/*.cpp and flags any assignment that zeroes a
// field whose name ends in "CooldownTimer" (or contains cooldown+Timer). A revert
// that re-introduces the zeroing — here or on any other enemy/actor timer — fails
// the gate.
//
//   bun scripts/lint-cooldown-reset-shape.ts            # lint the real files
//   bun scripts/lint-cooldown-reset-shape.ts --selftest # prove the detector works
//
// Exit 0 = clean; 1 = a violation (prints file:line); 2 = usage/self error.

import { readFileSync, readdirSync } from "fs";
import { dirname, join, resolve } from "path";

const repoRoot = dirname(import.meta.dir);
const systemsDir = resolve(repoRoot, "game", "systems");

// Function names that run on a STATE CHANGE (not on the rate-limited action).
const TRANSITION_FN = /\b(transitionToState|enter\w*State|\w*EnterState|onEnter\w*)\b/;
// A cooldown/rate-limit timer field being zeroed: `x.fooCooldownTimer = 0(.0f)`.
const COOLDOWN_ZERO =
  /\b[\w.>-]*(?:[Cc]ooldown[\w]*Timer|cooldown[\w]*_timer)\b\s*=\s*0(?:\.0*f?)?\s*;/;

type Violation = { file: string; line: number; text: string };

// Extract every function body in `source` whose signature line matches
// `nameRegex`, returning [{ bodyStartLine, body }]. Brace-matched from the
// def's opening `{`; string/char-literal braces are rare enough in these
// state-machine bodies to ignore (same simplification as the sibling lints).
function extractMatchingBodies(
  source: string,
  nameRegex: RegExp,
): { startLine: number; body: string }[] {
  const out: { startLine: number; body: string }[] = [];
  // Match an out-of-line member definition line ending in `) {` (optionally
  // `const`), e.g. "void EnemyAISystem::transitionToState(...) {".
  const defRegex = new RegExp(
    `[\\w:<>,&*\\s]+::(\\w+)\\s*\\([^;{]*\\)\\s*(?:const\\s*)?\\{`,
    "g",
  );
  let m: RegExpExecArray | null;
  while ((m = defRegex.exec(source)) !== null) {
    const fnName = m[1];
    if (!nameRegex.test(fnName)) continue;
    const braceOpen = source.indexOf("{", m.index);
    if (braceOpen < 0) continue;
    let depth = 0;
    let end = -1;
    for (let i = braceOpen; i < source.length; i++) {
      const ch = source[i];
      if (ch === "{") depth++;
      else if (ch === "}") {
        depth--;
        if (depth === 0) {
          end = i;
          break;
        }
      }
    }
    if (end < 0) continue;
    const startLine = source.slice(0, braceOpen).split("\n").length;
    out.push({ startLine, body: source.slice(braceOpen, end + 1) });
  }
  return out;
}

function lintSource(source: string, label: string): Violation[] {
  const violations: Violation[] = [];
  const bodies = extractMatchingBodies(source, TRANSITION_FN);
  for (const { startLine, body } of bodies) {
    const lines = body.split("\n");
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      // Ignore comment lines so a "do NOT reset ... = 0" explanation is safe.
      const codePart = line.replace(/\/\/.*$/, "").replace(/\/\*.*?\*\//g, "");
      if (COOLDOWN_ZERO.test(codePart)) {
        violations.push({
          file: label,
          line: startLine + i,
          text: line.trim(),
        });
      }
    }
  }
  return violations;
}

function selftest(): number {
  const guarded = `
void EnemyAISystem::transitionToState(EnemyComponent& enemy, AIState s) {
    enemy.state = s;
    switch (s) {
        case AIState::Attacking:
            // Do NOT reset attackCooldownTimer here.
            break;
    }
}
void EnemyAISystem::updateAttackingState(EnemyComponent& enemy) {
    enemy.attackCooldownTimer = enemy.attackCooldown;  // legit: on an actual fire
}`;
  const unguarded = guarded.replace(
    "            // Do NOT reset attackCooldownTimer here.",
    "            enemy.attackCooldownTimer = 0.0f;",
  );

  const gv = lintSource(guarded, "<selftest guarded>");
  const uv = lintSource(unguarded, "<selftest unguarded>");
  let ok = true;
  if (gv.length !== 0) {
    console.error(
      `selftest FAIL: guarded fixture flagged ${gv.length} (expected 0). The fire-time reset in updateAttackingState must NOT trip the lint.`,
    );
    ok = false;
  }
  if (uv.length !== 1) {
    console.error(
      `selftest FAIL: unguarded fixture flagged ${uv.length} (expected exactly 1 — the zero in transitionToState).`,
    );
    ok = false;
  }
  console.log(ok ? "selftest PASS" : "selftest FAILED");
  return ok ? 0 : 2;
}

function main(): number {
  if (process.argv.includes("--selftest")) return selftest();

  let files: string[];
  try {
    files = readdirSync(systemsDir).filter((f) => f.endsWith(".cpp"));
  } catch (error) {
    console.error(`lint-cooldown-reset-shape: cannot read ${systemsDir}: ${error}`);
    return 2;
  }

  const violations: Violation[] = [];
  for (const f of files) {
    const source = readFileSync(join(systemsDir, f), "utf8");
    for (const v of lintSource(source, join("game", "systems", f))) {
      violations.push(v);
    }
  }

  if (violations.length === 0) {
    console.log(
      "lint-cooldown-reset-shape: OK — no state-transition function re-arms a cooldown timer (only a fire may)",
    );
    return 0;
  }
  for (const v of violations) {
    console.error(
      `${v.file}:${v.line}: a state-transition function zeroes a cooldown timer ('${v.text}') — this re-arms a per-actor rate limit on state re-entry, letting the action bypass its cooldown floor. Only the firing action may reset it.`,
    );
  }
  console.error(
    `lint-cooldown-reset-shape: ${violations.length} violation(s)`,
  );
  return 1;
}

process.exit(main());

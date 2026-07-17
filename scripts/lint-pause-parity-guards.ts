// lint-pause-parity-guards — enumerate the "legacy UI path runs under web
// parity" bug class in the PauseMenu and fail if any instance is unguarded.
//
// THE CLASS (2026-07-17 audit, bug #3): under WebParity::kEnabled the pause
// modal is drawn AND interacted with entirely by renderWebParityModal (ImGui
// owns hover + click state). The legacy native surface — the m_buttons vector
// filled by initialize(), its per-frame hover hit-test in updateButtons(), its
// keyboard nav in handleInput(), and its bespoke draw in render() — must NOT
// also run, or it fires phantom feedback with no on-screen button. The bug we
// caught was update()→updateButtons() still running the legacy hover hit-test
// and firing m_audio.playMenuHover() when the cursor crossed a now-invisible
// stale rect. render() (:335) and handleInput() (:853) already short-circuited
// on kEnabled; update() did not. This is a CLASS, not a one-off: every legacy
// per-frame interaction entry point must branch on WebParity::kEnabled.
//
// This lint asserts each such entry point's body contains an
// `if constexpr (WebParity::kEnabled)` branch, so a future entry point (or a
// regression that drops a guard) fails the gate instead of shipping a phantom.
//
//   bun scripts/lint-pause-parity-guards.ts            # lint the real file
//   bun scripts/lint-pause-parity-guards.ts --selftest # prove the detector works
//
// Exit 0 = all guarded; 1 = a violation (prints file:line); 2 = usage/self error.

import { readFileSync } from "fs";
import { dirname, join, resolve } from "path";

const repoRoot = dirname(import.meta.dir);
const pauseMenuPath = resolve(repoRoot, "game", "ui", "PauseMenu.cpp");

// The legacy per-frame interaction entry points that MUST short-circuit (or
// branch) under WebParity::kEnabled. Keep this list in step with PauseMenu's
// public interaction surface — a new per-frame legacy method belongs here.
const GUARDED_METHODS = ["update", "handleInput", "render"];
const GUARD_TOKEN = "if constexpr (WebParity::kEnabled)";

type Violation = { method: string; line: number; reason: string };

// Extract the body of `<ret> PauseMenu::<method>(...) { ... }` by locating the
// definition and brace-matching from its opening `{`. Returns the body text
// plus the 1-based line where the definition starts, or null if not found.
function extractMethodBody(
  source: string,
  method: string,
): { body: string; defLine: number } | null {
  // Match the out-of-line definition, e.g. "void PauseMenu::update(float ...)".
  // [^;]* across the return type + params, stopping before the opening brace.
  const defRegex = new RegExp(
    `(?:[\\w:<>,&*\\s]+)PauseMenu::${method}\\s*\\([^;{]*\\)\\s*(?:const\\s*)?\\{`,
  );
  const match = defRegex.exec(source);
  if (!match) return null;

  const braceOpen = source.indexOf("{", match.index);
  if (braceOpen < 0) return null;

  // Brace-match to find the matching close, ignoring the trivial hazard of
  // braces inside strings/char-literals (PauseMenu bodies have none that would
  // unbalance the count before the real close; good enough for a source lint).
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
  if (end < 0) return null;

  const defLine = source.slice(0, match.index).split("\n").length;
  return { body: source.slice(braceOpen, end + 1), defLine };
}

function lintSource(source: string, label: string): Violation[] {
  const violations: Violation[] = [];
  for (const method of GUARDED_METHODS) {
    const found = extractMethodBody(source, method);
    if (!found) {
      violations.push({
        method,
        line: 0,
        reason: `PauseMenu::${method} definition not found in ${label} — the lint's method list is stale, reconcile it with the source`,
      });
      continue;
    }
    if (!found.body.includes(GUARD_TOKEN)) {
      violations.push({
        method,
        line: found.defLine,
        reason: `PauseMenu::${method} has no '${GUARD_TOKEN}' branch — a legacy interaction path would run under web parity and fire phantom feedback`,
      });
    }
  }
  return violations;
}

function selftest(): number {
  // A guarded body (passes) and an unguarded one (must be flagged) for each
  // shape, so the detector can't silently rot into always-green.
  const guarded = `
void PauseMenu::update(float) {
    if (!m_initialized) return;
    if constexpr (WebParity::kEnabled) { return; }
    updateButtons();
}
void PauseMenu::handleInput() {
    if constexpr (WebParity::kEnabled) { /* parity input */ return; }
    // legacy
}
void PauseMenu::render(X& p, uint32_t w, uint32_t h) {
    if constexpr (WebParity::kEnabled) { renderWebParityModal(w, h); return; }
    renderButtons(p);
}`;
  const unguarded = guarded.replace(
    "    if constexpr (WebParity::kEnabled) { return; }\n",
    "",
  );

  const guardedViolations = lintSource(guarded, "<selftest guarded>");
  const unguardedViolations = lintSource(unguarded, "<selftest unguarded>");

  let ok = true;
  if (guardedViolations.length !== 0) {
    console.error(
      `selftest FAIL: guarded fixture flagged ${guardedViolations.length} violation(s), expected 0`,
    );
    ok = false;
  }
  // Removing update()'s guard must flag exactly update() (the other two keep theirs).
  if (
    unguardedViolations.length !== 1 ||
    unguardedViolations[0].method !== "update"
  ) {
    console.error(
      `selftest FAIL: unguarded fixture flagged ${JSON.stringify(
        unguardedViolations.map((v) => v.method),
      )}, expected exactly ["update"]`,
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
    source = readFileSync(pauseMenuPath, "utf8");
  } catch (error) {
    console.error(`lint-pause-parity-guards: cannot read ${pauseMenuPath}: ${error}`);
    return 2;
  }

  const violations = lintSource(source, "PauseMenu.cpp");
  if (violations.length === 0) {
    console.log(
      `lint-pause-parity-guards: OK — all ${GUARDED_METHODS.length} legacy interaction entry points guard on WebParity::kEnabled`,
    );
    return 0;
  }
  for (const v of violations) {
    console.error(
      `${join("game", "ui", "PauseMenu.cpp")}:${v.line}: ${v.reason}`,
    );
  }
  console.error(
    `lint-pause-parity-guards: ${violations.length} violation(s) — a legacy PauseMenu path would run under web parity`,
  );
  return 1;
}

process.exit(main());

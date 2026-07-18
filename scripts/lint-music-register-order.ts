// lint-music-register-order — pin the fix for the 2026-07-17 audio gain-poison
// bug: in GameAudio::crossFadeMusic the mixer registration MUST happen before
// the fade-in gain is dropped to 0.
//
// THE BUG: AudioMixer::registerSource snapshots info.originalGain =
// source->getGain() at call time, then recomputes the live gain as
// originalGain * effectiveVolume on every volume/mute change (updateSourceVolume).
// crossFadeMusic used to `m_fadeInMusic->setGain(0.0f)` BEFORE calling
// registerSource, so the mixer captured 0 as the track's baseline — and the
// first time the player moved the Music/Master slider or toggled mute the
// gameplay music went permanently silent (0 * effectiveVolume = 0). Registering
// while the source is at its full baseline gain captures the correct baseline.
//
// This lint asserts that within crossFadeMusic the FIRST `registerSource(` call
// precedes the FIRST fade-in `setGain(0…)` — so a reorder that reintroduces the
// poison fails the gate. Audio is OpenAL-backed and not linked into unit_tests,
// so a structural pin is the available regression.
//
//   bun scripts/lint-music-register-order.ts            # lint the real file
//   bun scripts/lint-music-register-order.ts --selftest # prove the detector works
//
// Exit 0 = ordering OK; 1 = a violation; 2 = usage/self error.

import { readFileSync } from "fs";
import { dirname, join, resolve } from "path";

const repoRoot = dirname(import.meta.dir);
const target = resolve(repoRoot, "game", "audio", "GameAudio.cpp");

const FN = "crossFadeMusic";

// Extract the body of `<ret> GameAudio::crossFadeMusic(...) { ... }` by
// brace-matching from its opening `{` (comment/string braces are not a hazard
// in this body). Returns {body, startLine} or null.
function extractBody(source: string): { body: string; startLine: number } | null {
  const def = new RegExp(`GameAudio::${FN}\\s*\\([^;{]*\\)\\s*(?:const\\s*)?\\{`);
  const m = def.exec(source);
  if (!m) return null;
  const open = source.indexOf("{", m.index);
  if (open < 0) return null;
  let depth = 0;
  let end = -1;
  for (let i = open; i < source.length; i++) {
    if (source[i] === "{") depth++;
    else if (source[i] === "}") {
      depth--;
      if (depth === 0) {
        end = i;
        break;
      }
    }
  }
  if (end < 0) return null;
  return {
    body: source.slice(open, end + 1),
    startLine: source.slice(0, open).split("\n").length,
  };
}

// Returns the 0-based line index of the first match of `re` in `lines`,
// ignoring `//` line comments, or -1.
function firstCodeLine(lines: string[], re: RegExp): number {
  for (let i = 0; i < lines.length; i++) {
    const code = lines[i].replace(/\/\/.*$/, "").replace(/\/\*.*?\*\//g, "");
    if (re.test(code)) return i;
  }
  return -1;
}

const REGISTER = /\bregisterSource\s*\(/;
// A fade-in gain override to zero: `->setGain(0)` / `setGain(0.0f)` etc.
const SET_GAIN_ZERO = /setGain\s*\(\s*0(?:\.0*f?)?\s*\)/;

type Result = { ok: boolean; reason: string; line?: number };

function check(source: string, label: string): Result {
  const found = extractBody(source);
  if (!found) {
    return { ok: false, reason: `GameAudio::${FN} not found in ${label} — the lint target moved; reconcile it.` };
  }
  const lines = found.body.split("\n");
  const regIdx = firstCodeLine(lines, REGISTER);
  const zeroIdx = firstCodeLine(lines, SET_GAIN_ZERO);
  if (regIdx < 0) {
    return { ok: false, reason: `no registerSource(...) call found in ${FN}` };
  }
  // If there is no fade-in setGain(0), there is nothing to poison — OK.
  if (zeroIdx < 0) return { ok: true, reason: "no fade-in setGain(0); nothing to order" };
  if (zeroIdx < regIdx) {
    return {
      ok: false,
      reason: `${FN} drops the fade-in gain to 0 (body line ${zeroIdx + 1}) BEFORE registerSource (body line ${regIdx + 1}) — the mixer will snapshot originalGain=0 and a later volume/mute change permanently silences the music. Register first.`,
      line: found.startLine + zeroIdx,
    };
  }
  return { ok: true, reason: "registerSource precedes the fade-in setGain(0)" };
}

function selftest(): number {
  const good = `
void GameAudio::crossFadeMusic(const std::string& t, float d) {
    newMusic->setGain(1.0f);
    mixer.registerSource(newMusic, Channel::Music);
    if (fading) { m_fadeInMusic->setGain(0.0f); }
}`;
  const bad = `
void GameAudio::crossFadeMusic(const std::string& t, float d) {
    if (fading) { m_fadeInMusic->setGain(0.0f); }
    mixer.registerSource(newMusic, Channel::Music);
}`;
  const g = check(good, "<selftest good>");
  const b = check(bad, "<selftest bad>");
  let ok = true;
  if (!g.ok) { console.error(`selftest FAIL: good fixture flagged — ${g.reason}`); ok = false; }
  if (b.ok) { console.error("selftest FAIL: bad fixture (setGain(0) before register) NOT flagged"); ok = false; }
  console.log(ok ? "selftest PASS" : "selftest FAILED");
  return ok ? 0 : 2;
}

function main(): number {
  if (process.argv.includes("--selftest")) return selftest();
  let source: string;
  try {
    source = readFileSync(target, "utf8");
  } catch (error) {
    console.error(`lint-music-register-order: cannot read ${target}: ${error}`);
    return 2;
  }
  const r = check(source, "GameAudio.cpp");
  if (r.ok) {
    console.log(`lint-music-register-order: OK — ${r.reason}`);
    return 0;
  }
  console.error(`${join("game", "audio", "GameAudio.cpp")}:${r.line ?? "?"}: ${r.reason}`);
  return 1;
}

process.exit(main());

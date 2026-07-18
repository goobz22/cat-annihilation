// lint-audio-listener-tracking — pin the fix for the 2026-07-18 stationary-
// listener bug: the OpenAL listener must be tracked to the player every frame.
//
// THE BUG: AudioEngine::initialize() set the listener to the world origin ONCE
// and nothing ever repositioned it. The three world-positioned combat sounds
// (enemy death / enemy hit / projectile hit) publish the ENEMY's world position
// as the AL source position, and the distance model is
// AL_INVERSE_DISTANCE_CLAMPED (gain ~ 1/distance-from-listener) — so combat
// audio attenuated by distance from SPAWN, not from the cat: a fight 25 units
// out played at ~1/25 gain even with the dog dying at the player's feet.
//
// THE FIX (pinned here): GameAudio::setListenerPose() forwards the player
// position + camera forward to the engine listener, and CatAnnihilation's
// per-frame audio update calls it BEFORE gameAudio_->update(dt). Audio is
// OpenAL-backed and not linked into unit_tests, so this structural check is the
// regression: both halves of the wiring must exist.
//
//   bun scripts/lint-audio-listener-tracking.ts            # lint the real files
//   bun scripts/lint-audio-listener-tracking.ts --selftest # prove the detector
//
// Exit 0 = wired; 1 = a violation (prints which half broke); 2 = usage error.

import { readFileSync } from "fs";
import { join, dirname, resolve } from "path";

const repoRoot = dirname(import.meta.dir);
const gameAudioCpp = resolve(repoRoot, "game", "audio", "GameAudio.cpp");
const catCpp = resolve(repoRoot, "game", "CatAnnihilation.cpp");

// Half 1: GameAudio::setListenerPose exists AND forwards to the engine
// listener's setPosition (the actual repositioning, not just a stub).
const POSE_DEF = /GameAudio::setListenerPose\s*\(/;
const POSE_FORWARDS = /getListener\s*\(\s*\)/;
// Half 2: the game loop calls setListenerPose (the per-frame wiring).
const POSE_CALL = /->\s*setListenerPose\s*\(/;

type Check = { ok: boolean; reason: string };

function checkGameAudio(source: string): Check {
  if (!POSE_DEF.test(source)) {
    return { ok: false, reason: "GameAudio.cpp: setListenerPose() definition missing — the listener can never be repositioned and positional SFX attenuate from the world origin" };
  }
  // The forwarding must appear somewhere after the definition (same file).
  const defIdx = source.search(POSE_DEF);
  if (!POSE_FORWARDS.test(source.slice(defIdx))) {
    return { ok: false, reason: "GameAudio.cpp: setListenerPose() exists but never reaches the engine listener (no getListener() call after the definition) — it is a stub" };
  }
  return { ok: true, reason: "setListenerPose defined and forwards to the engine listener" };
}

function checkCaller(source: string): Check {
  if (!POSE_CALL.test(source)) {
    return { ok: false, reason: "CatAnnihilation.cpp: no ->setListenerPose(...) call — the listener is never tracked to the player at runtime" };
  }
  return { ok: true, reason: "the game loop calls setListenerPose" };
}

function selftest(): number {
  const goodAudio = `
void GameAudio::setListenerPose(const std::array<float, 3>& p, const std::array<float, 3>& f) {
    auto& listener = m_audioEngine.getListener();
    listener.setPosition(p);
}`;
  const stubAudio = `
void GameAudio::setListenerPose(const std::array<float, 3>& p, const std::array<float, 3>& f) {
    (void)p; (void)f;
}`;
  const goodCaller = `gameAudio_->setListenerPose(pos, fwd);`;
  const badCaller = `gameAudio_->update(dt);`;

  let ok = true;
  if (!checkGameAudio(goodAudio).ok) { console.error("selftest FAIL: good GameAudio flagged"); ok = false; }
  if (checkGameAudio(stubAudio).ok) { console.error("selftest FAIL: stub setListenerPose NOT flagged"); ok = false; }
  if (checkGameAudio("").ok) { console.error("selftest FAIL: missing definition NOT flagged"); ok = false; }
  if (!checkCaller(goodCaller).ok) { console.error("selftest FAIL: good caller flagged"); ok = false; }
  if (checkCaller(badCaller).ok) { console.error("selftest FAIL: missing caller NOT flagged"); ok = false; }
  console.log(ok ? "selftest PASS" : "selftest FAILED");
  return ok ? 0 : 2;
}

function main(): number {
  if (process.argv.includes("--selftest")) return selftest();
  let audioSrc: string, catSrc: string;
  try {
    audioSrc = readFileSync(gameAudioCpp, "utf8");
    catSrc = readFileSync(catCpp, "utf8");
  } catch (error) {
    console.error(`lint-audio-listener-tracking: cannot read sources: ${error}`);
    return 2;
  }
  const a = checkGameAudio(audioSrc);
  const c = checkCaller(catSrc);
  if (a.ok && c.ok) {
    console.log("lint-audio-listener-tracking: OK — the OpenAL listener is tracked to the player (definition + per-frame call both present)");
    return 0;
  }
  if (!a.ok) console.error(join("game", "audio", "GameAudio.cpp") + ": " + a.reason);
  if (!c.ok) console.error(join("game", "CatAnnihilation.cpp") + ": " + c.reason);
  return 1;
}

process.exit(main());

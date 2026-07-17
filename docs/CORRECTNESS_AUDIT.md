# Cat Annihilation — correctness audit log

Real-bug hunts across the native C++ (separate from the web-parity campaign,
which is about matching the reference; this is about actual crashes/UB/logic
errors). Each entry: a multi-agent audit (per-subsystem finders → adversarial
refutation → only survivors fixed), then each confirmed bug root-caused,
runtime- or code-verified, fixed at the root, and pinned with a regression.

## 2026-07-17 — Round 1 (7 subsystems, 9 confirmed bugs, all fixed)

Workflow: `cat-correctness-audit` (7 finders + adversarial verify). 3 finders
(progression, movement-camera, hud-ui-state) were transiently classifier-
blocked and re-run in Round 2.

| # | Sev | Bug | Fix | Commit |
|---|-----|-----|-----|--------|
| 1 | HIGH | `restart()`/`quitToMenu()` never reset WaveSystem — `wavesStarted_` latched, so a restart kept the pre-death wave + stale InProgress state; the next update spuriously auto-completed it and dumped the player into wave N+1 (verified: die wave 1 → Try Again → wave 2 with 0 kills in ~6s). | New `WaveSystem::reset()` clears the latch + all per-run state, called from both restart paths. Pinned in the gate menu-flow (`key:r` → expect fresh wave 1). | a4f322f |
| 2 | HIGH | `ExtractNodes` wrote `nodes[childIdx].parentIndex` with an unchecked asset-controlled child index — OOB heap write on a corrupt glTF. | Bounds-check + throw like the sibling guards. Hermetic-GLB regression (child index 5 on 4 nodes → throws). | 988a279 |
| 3 | MED | `ExtractBufferData`/`Joints`/`Weights` memcpy'd with no accessor-extent check — OOB read on a corrupt accessor. | All three take the buffer by ref + validate extent before memcpy. Hermetic-GLB regression. | 188d9a5 |
| 4 | MED | Melee/projectile weapon XP inflated 2-3× per swing: the 2-3-frame tolerance window re-ran the swing block against the same (now i-framed) dog, and the XP-awarding callback fired ungated. | `applyDamage` returns damage-landed; combo-commit + enriched callback gated on it. Replay regression (1 award/swing vs 3× pre-fix). | b083bf2 |
| 5 | MED | `--input-script` used raw `std::stof` → uncaught throw aborted the process on a malformed token. | `parseInputScriptFloat` (strtof, whole-token) skips + logs the bad command. | 9b8a903 |
| 6 | LOW | `enemiesForWave` computed `3 + wave*2` in 32-bit int → signed overflow (UB) for a fuzzed huge `--starting-wave`. | int64 + clamp to INT_MAX. Pinned. | 9b8a903 |
| 7 | LOW | `getScrollDelta()` always returned 0: the reset in `update()` (which runs after `pollEvents` populates it) zeroed the delta before the game read it. | New `clearScrollDelta()` called at frame top before `pollEvents`. (Latent — no consumer today.) | 98d4901 |
| 8 | MED | Enemy health bars projected with the camera's yaw/pitch (~2.6° off the scene's `lookAt`) + no shake → floated detached above the dogs. | `setEnemyBarCamera` takes the scene's exact eye/target; view = `lookAt(...)`. The projection-fix let the anchor revert to the web's cited 1.5. Before/after captures. | 71c16e0 |
| 9 | LOW | `enemiesForWave` overflow (folded into #6). | — | 9b8a903 |

All fixes: gate 4/4 green + unit suite green at each commit; every bug pinned
with a regression that fails if reverted.

## 2026-07-17 — Round 2 (the 3 re-run subsystems: progression, movement-camera, hud-ui-state)

Workflow `cat-audit-round2` (finders + adversarial refutation). 3 bugs
survived verification; each was runtime- or code-verified against the web
reference (the behavioral contract), fixed at the root, and pinned with a
regression that fails first if reverted.

| # | Sev | Bug | Fix | Commit |
|---|-----|-----|-----|--------|
| 1 | HIGH | Follow-camera mirrored to the FRONT of travel after any turn. The offset was `yawRot.rotate(cameraOffset_)` = `(+sinθ·d, h, cosθ·d)` — X wrong-handed vs the movement facing `(sinθ,0,−cosθ)`. At the spawn heading (θ=0, sinθ=0) it agreed with the correct offset, so every straight-walk check passed; after an A/D turn the camera flipped in front and W drove the cat toward the lens (verified: turn-left+walk → cat x=−12.1, camera x=−22.6, ahead of travel). | Compute the offset directly from the facing: `(−sinθ·cameraOffset_.z, cameraOffset_.y, cosθ·cameraOffset_.z)` → camera behind travel (post-fix cameraX=−1.7 ≈ playerX+10.4). Gate `menu-flow` now TURNS before asserting camera position (the prior straight-walk-only coverage was blind to the mirror). | a5cb947 |
| 2 | MED | Nine Lives revive never re-armed on restart. The leveling system survives a restart (level/XP/abilities carry over, matching the web's localStorage-restored reload), but the per-run `nineLivesUsed` latch was never cleared — a player who spent their revive last run began the next run unable to revive. The web's restart is `window.location.reload()` → fresh store with `nineLivesUsed=false`, so the revive IS available again. | `restart()` + `quitToMenu()` call `levelingSystem_->resetRevive()` (the run-scoped-reset invariant `WaveSystem::reset()` already upholds). Harness gained a `spendrevive` input-script verb + `reviveArmed` query; new gate stage `nine-lives-rearm` drives spend→GameOver→restart→re-armed. Fail-first proven (exit 4 with the fix disabled). | 609925f |
| 3 | LOW | Phantom hover SFX under web parity. The pause modal is ImGui-owned under `kEnabled`, and `render()`/`handleInput()` short-circuit — but `update()` still ran `updateButtons()`, whose per-frame hover hit-test against the stale legacy `m_buttons` rects fired `playMenuHover()` when the cursor crossed a now-invisible button. | Guard `update()` with the same `if constexpr (WebParity::kEnabled) return;`. Class-detector `scripts/lint-pause-parity-guards.ts` (`--selftest`, wired as gate stage 0) enumerates the "legacy PauseMenu path runs under parity" class across all three entry points; revert-refail proven. | bc8747e |

All fixes: gate green (now 7 stages: 2 lints + compile + build + cat-verify +
menu-flow + nine-lives-rearm) + unit suite (7,724,713 assertions / 1233 cases)
green at each commit.

### Harness investments made this round (per the owner's "give yourself more feedback" directive)

- `expect:` query **`reviveArmed`** — exposes `LevelingSystem::canRevive()` so revive lifecycle is scriptable.
- input-script verb **`spendrevive`** — injects "Nine Lives earned, revive spent" (reaching level 15 headlessly is impractical).
- `expect:` query **`cameraX/Y/Z`** (from Round 1) proved decisive here — the camera mirror was invisible to screenshots but a one-line assertion at a turned heading caught it.
- new class-detector lint **`lint-pause-parity-guards.ts`** — the first source-structural gate for the web-parity guard class.

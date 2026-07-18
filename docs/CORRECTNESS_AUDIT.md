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

### Investigated, NOT a bug (so it isn't re-dug — R16 proof-of-work)

**Player model facing after a turn (verifier's camera-fix follow-up).** Hypothesis:
the player mesh's `Quaternion(Y, yaw)` (PlayerControlSystem `processMovementInput`)
uses a different handedness than the movement facing `(sinθ,0,−cosθ)`, so the cat
would "moonwalk" (face the mirror of travel) after an A/D turn. **Verdict: not a
visible bug.** Evidence: (1) an engine-math probe (`Quaternion::rotate` +
`lookRotation`, the real headers) shows `Quaternion(Y,yaw)` only coincides with
the engine's `lookRotation(facing)` at yaw=0 — a genuine ABSTRACT inconsistency;
(2) BUT `lookRotation`/`lookAt` (which every enemy uses to face the player, and
they visibly do) is the tool for matching an EXTERNAL direction, whereas the
player's mesh yaw, the follow-camera offset (`cameraYaw_` hard-snaps to the mesh
yaw), and the travel facing ALL derive from the one `cameraYaw_` — they are
yaw-locked by construction, so a mesh-vs-travel mirror is structurally
unobservable through the follow camera; (3) rendered frames at spawn and after a
90° left turn + walk both show the cat's BACK to the camera (`.facing_shots/`,
regenerable). The inconsistency is harmless for the self-referential player rig;
it would only matter if the player mesh had to face an external target, which it
never does. No code change. If future work adds an external-facing player
orientation (e.g. lock-on), reuse `lookRotation`, not `Quaternion(Y,yaw)`.

## 2026-07-17 — Round 3 (5 un-audited subsystems: save-load, enemy-ai, physics-collision, projectiles-spells, animation-skinning)

Workflow `cat-audit-round3` (5 Opus finders → per-candidate adversarial
refutation, default REFUTED). 9 candidates surfaced; **5 CONFIRMED**, 4 refuted
(unbounded-alloc save read [checksum-gated], two spell-effect claims [by-element
hardcoding is intentional], AOE lead-aim whiff [autoplay-only]). physics-collision
came back clean. Each confirmed bug is fixed at the root with a fail-first
regression.

| # | Sev | Bug | Fix | Commit |
|---|-----|-----|-----|--------|
| 1 | HIGH | Non-looping animation clips (attack/jump/sit/death/hit) snap to frame 0 at clip end instead of HOLDING the last frame: `Animation::sample` unconditionally calls `normalizeTime(time, /*loop=*/true)`, so `fmod(duration,duration)==0` → frame 0; the Animator clamps `m_currentTime=duration` + stops, but `sample()` wraps it to 0 and `update()` then freezes there. A dead or seated cat visibly pops back to standing. | Thread a `loop` flag through `Animation::sample` (default true for blend-tree callers); the Animator forwards each state's `loop`. `loop=false` clamps → holds last keyframe. Animator regression (non-loop holds x=10, looping control wraps to x=5); fail-first proven. | a939142 |
| 2 | MED | Enemy attack-cooldown re-zeroed on every Chasing→Attacking transition (`transitionToState`), so with the parity hysteresis band (enter ≤1.2, leave >1.44) + zero enemy melee i-frames, kiting re-fires 15 dmg well inside the web's 1.0s floor. Web gates on `currentTime-lastAttackTime>=1000ms`, written only on an actual fire. | Remove the zero-on-entry — the timer inits to 0 (first attack still immediate) and decrements every frame in all states, so the floor survives a kite. Class lint `lint-cooldown-reset-shape.ts` (scans all state-transition fns for cooldown-timer zeroing; --selftest; gate-wired); revert-refail proven. | 4f68c40 |
| 3 | MED | `decompressData` RLE loop reads 2 bytes/iter but tests `inPos < compressedSize` once → odd `compressedSize` reads 1 byte past the buffer, BEFORE the CRC32 gate, from attacker-controlled save data. | Require a full pair in range (`inPos + 1 < compressedSize`); a valid stream is always even, so a corrupt odd length now stops and the size-mismatch throw rejects it. Regression: odd buffer throws (sentinel-padded so pre-fix hits no UB), valid stream round-trips; fail-first proven. | 7ae52fc |
| 4 | MED | `BinaryReader::read` treats EOF as success (`if(!good() && !eof())` never throws on a short read); `read<T>()` returns an indeterminate value. Reachable via the unguarded `getSaveHeader/readHeader` path → garbage save-slot UI + UB. | Detect short reads via `gcount()` and throw; `getSaveHeader` catches → clean default header, `loadFromFile` → load failure. Regression: read uint32_t from a 2-byte file throws; fail-first proven. | 7663a69 |
| 5 | LOW | Native enemies have no separation/flocking, so dogs stack on the identical player-seek point; the web applies a boid separation force (radius 1.5, force 3.0) so its dogs spread into a ring. Visual-only divergence (PARITY_MATRIX OPEN P2); `BalanceConfig SEPARATION_RADIUS` exists but is unused. | Add `kEnemySeparationRadius=1.5`/`kEnemySeparationForce=3.0` (web literals) + a boid separation force summed over neighbors, added to the seek velocity under `kEnabled` (matches web `moveX += separationX*delta`). Regression pins the constants + the pure `separationContribution` math (falloff, planar, div-0 guard). Closes PARITY_MATRIX enemy-separation P2. | 4e42828 |

All fixes: gate green (now **9 stages**: 4 lints [pause-parity + cooldown-reset, each with its selftest] + compile + build + cat-verify + menu-flow + nine-lives-rearm) + unit suite (7,724,738 assertions / 1237 cases) green at each commit. Two new class-detection lints this round (`lint-cooldown-reset-shape.ts`) and the save-load hardening moves the engine toward loading untrusted `.catsave` data without UB.

## 2026-07-17 — Round 4 (5 more un-audited subsystems: audio, day-night, projectile-lifetime, combo/weapon-skills, wave-transition/victory)

Workflow `cat-audit-round4` (aggregation fixed vs Round 3). 3 candidates
surfaced, **all 3 CONFIRMED**, 0 refuted. day-night, projectile-lifetime, and
wave-transition came back clean. Two of the three were the SAME class (skill
level caps not reconciled with the web's 99-max), so per T8 the class was
enumerated — which surfaced a THIRD instance (the cat main level cap) the audit
finders never flagged.

| # | Sev | Bug | Fix | Commit |
|---|-----|-----|-----|--------|
| 1 | MED | Cross-faded gameplay music is permanently silenced by any later volume/mute change: `crossFadeMusic` set the fade-in track's gain to 0 BEFORE `mixer.registerSource`, which snapshots `originalGain = getGain()` = 0, so `updateSourceVolume` then recomputes `0 * effectiveVolume = 0` forever. Only transitioned tracks poisoned; gated behind `CAT_AUDIO=1`. | Register the source while it is at its full baseline gain (1.0), THEN apply the fade-in gain. Structural lint `lint-music-register-order.ts` (audio is OpenAL-backed / not unit-testable) pins that `registerSource` precedes the fade-in `setGain(0)`; --selftest, gate-wired; revert-refail proven. | 29bb47b |
| 2 | MED | Weapon skill hard-capped at level 20 vs the web's 99 — sword damage pins at 230 (`40+(lvl-1)*10`) and the HUD skill bar freezes while the web keeps climbing. | Part of the class fix below. | e59ec43 |
| 3 | MED | Elemental-magic skill hard-capped at level 15 vs the web's 99 (`weaponSkills.magic[element]` levels unbounded to 99, gameStore.ts:805-812). | Part of the class fix below. | e59ec43 |
| — | (class) | **Enumerating #2+#3 per T8 surfaced a THIRD instance the finders missed: the cat MAIN level was capped at 50 vs the web's 99** (`xp_tables.hpp` itself documents "1-99 under web parity"). | Add `WebParity::kMaxLevel=99`; route every cap through a named constant (`kMaxCatLevel`/`kMaxWeaponSkillLevel`/`kMaxElementalSkillLevel`, each = `kMaxLevel` under parity, native fallbacks 50/20/15). Class lint `lint-skill-level-cap.ts` forbids any bare-literal skill cap in `leveling_system.cpp`; regression drives all three tracks to 99; fail-first proven. | e59ec43 |

All fixes: gate green (now **13 stages**: 6 lints + compile + build + cat-verify + menu-flow + nine-lives-rearm) + unit suite (7,724,744 assertions / 1238 cases) green. Two new class-detection lints this round (`lint-skill-level-cap.ts`, `lint-music-register-order.ts`). The class-first discipline paid off literally — the cat-level cap would have shipped as a silent parity divergence had the class not been enumerated.

### Coverage gap queued (R14)

`tests/unit/test_leveling_system.cpp` is a full LevelingSystem suite (XP, stats, ability unlocks, regen, nine-lives, weapon/elemental skills) that is **NOT in `UNIT_TEST_SOURCES`** — it never compiles or runs. Activating it (and fixing any drifted assertions) would add real coverage over exactly the areas Round 4 touched. Tracked as a follow-up.

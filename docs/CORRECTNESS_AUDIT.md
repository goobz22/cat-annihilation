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

## 2026-07-18 — Round 5 (5 more subsystems: rendering/skinning, terrain-collision, HUD, npc-dialog, quest-system)

Workflow `cat-audit-round5`. **0 REACHABLE bugs.** Four subsystems came back
CLEAN with zero candidates — rendering/skinning (bone-palette consumption),
terrain-collision (heightfield sampling + river/bridge), HUD (data binding /
bar-fill / hotbar bounds), and npc-dialog. A strong coverage signal that the
reachable surface is well-hardened after Rounds 1-4. quest-system surfaced 2
candidates, **both adversarially REFUTED as unreachable dormant code** and
independently re-verified here (T2).

### Latent-UB landmine documented (real, but unreachable — NOT a shipping bug)

`game/systems/quest_system.cpp` has a genuine **iterator-invalidation class** (2
instances) that is structurally real UB but **cannot execute in the current
game**: `update()` (line 46) and `updateQuestTimers()` (line 491) range-iterate
`activeQuestIds_` and call `completeQuest`/`failQuest`, which `erase`-remove from
that same vector mid-loop (lines 264, 297) — plus a self-alias (the `failQuest`
arg is a reference into the vector being erased). It never runs because
`activeQuestIds_` is **never non-empty in gameplay**: the only writers are
`activateQuest` (called only from tests — verified: the sole non-test reference
is in `QUEST_SYSTEM_INTEGRATION.md`, a doc) and `loadQuestState` (fed by
`getActiveQuestIds()` at save time, which is always empty since nothing ever
activates a quest). The web survival build has no quests, so this is also
off-mission. **Fix when the quest/story system is wired to gameplay:** iterate a
snapshot copy of `activeQuestIds_` in both loops (fixes both the invalidation and
the self-alias), and un-skip `tests/unit/test_quest_system.cpp` (currently
dropped from `UNIT_TEST_SOURCES` for `Clan::` enum drift) to pin it. Not fixed
now because a T4 fail-first regression needs the full quest-data + Clan
scaffolding, disproportionate for unreachable off-mission code.

## 2026-07-18 — Round 6 (5 reachable subsystems: combat-depth, status-effects, health-system, input-camera, wave-spawn-pacing)

Workflow `cat-audit-round6`. combat-depth surfaced most; status-effects and
input-camera were otherwise clean. **5 candidates, all 5 CONFIRMED** (3 fixed
this round, 2 LOW queued below with verified fix-shapes). Notable: the verifiers
flagged a **stale PARITY_MATRIX note claiming the bow is unreachable** — it is a
live hotbar weapon (Num3), and three bugs stem from the hotbar combat being wired
up without web parity.

| # | Sev | Bug | Fix | Commit |
|---|-----|-----|-----|--------|
| 1 | MED | Nine-Lives revive left the player stuck in the `layDown` corpse pose: `handleDeath` fires the revive callback BEFORE the death-pose block, whose only guard was `!deathPosed`, so it posed the now-alive player and latched the flag — walk/run/idle dead for the rest of the run after any L15+ lethal hit. | After the death callback, early-return if the entity `isAlive()` (a handler revived it) before the death-pose freeze. Harness gained `grantrevive`/`killplayer` verbs + `playerDeathPosed` query; new gate stage `revive-not-stuck` (grantrevive→killplayer→alive+30%HP+not-posed). Fail-first proven. | (revive) |
| 2 | MED | Spawn ring centered on the LIVE player position per staggered spawn, so a player fleeing during the ~1.4s spawn window was continually re-surrounded; the web freezes the ring at the wave-start snapshot. | Capture the player position once in `startWave()` (`waveAnchorPosition_`); under parity center the ring on that frozen anchor via the pure static `spawnRingCenter()` helper. Deterministic unit test (headless was confounded by enemy chase); fail-first proven. Adds enemy-introspection expect queries. | (spawn) |
| 3 | MED | Bow arrows used the native hit radius 1.0 instead of the web's 1.5 (`WebParity::kProjectileHitRadius`); with the +1 arrow spawn height over ground dogs + 3D distance test, the horizontal hit window shrank to ~0.3 units and arrows sailed past enemies the web would hit. | Branch the projectile hit radius on `WebParity::kEnabled` (1.5), mirroring the spell path. Constant already pinned; new structural lint `lint-combat-projectile-parity.ts` forbids the bare `projectileHitRadius_` in `checkProjectileHit` (CombatSystem is ECS-coupled / not unit-testable). Revert-refail proven. | (bow) |

Gate now **16 stages** (7 lints + compile + build + cat-verify + menu-flow +
nine-lives-rearm + revive-not-stuck); unit suite 7,724,750 assertions / 1239
cases. (Commit SHAs recorded in git log; the three fixes pushed sequentially.)

**Note on cat-verify:** the 30s autoplay perf gate (`fpsMin>=15`) began flaking
at the threshold boundary this session (cold-start stutter under the long
session's accumulated build+workflow load; `fpsMin` varied 6-19 and PASSED at 19
when the machine cooperated). It is a pre-existing machine-perf sensitivity, not
a regression from these one-branch changes (cat-verify was green through Rounds
4-5); every correctness stage passes directly. Queued for investigation.

### LOW combat-parity divergences queued (verified, fix-shapes documented)

Both CONFIRMED and reachable, but LOW and with subtle interaction semantics —
deferred to a focused pass rather than rushed:

- **Enemy 0.5s i-frame throttles bow/projectile hits (LOW).** `HealthComponent::damage()` arms a shared 0.5s `invincibilityTimer` for ALL damage sources, so a second arrow within 0.5s at one dog is dropped (0 dmg, 0 XP). The web has NO enemy projectile throttle — only the SWORD has a per-enemy 500ms gate (`lastSwordHitTime`); arrows are ungated (`GlobalCollisionSystem.tsx`). Rapid-firing the bow at one tanky dog gives 30 dmg native vs 60 web. **Fix-shape:** under parity, either separate the sword gate from the projectile path or zero the enemy's `invincibilityTimer` after a projectile lands (mirroring the enemy→player melee i-frame fix) — but native shares ONE timer across sword+projectile, so the fix must not un-gate the sword; needs the two gates separated. Deferred for that interaction analysis. `game/systems/CombatSystem.cpp:1040`.
- **Shield-bash fires on Space/left-click, which the web never performs (LOW).** Native's `performShieldBash()` runs when the shield hotbar slot is active and the player presses Space/click; the web's `performAttack()` triggers an attack ONLY for the sword (`CatCharacter/index.tsx:150-165` — the web shield is a defensive BLOCK, not an offensive bash). **Fix-shape:** gate `performShieldBash()` (or the Shield case in the attack dispatch) off under parity so the shield is defensive-only, matching the web. Verify the shield slot's survival-play reachability first. `game/systems/PlayerControlSystem.cpp:512`.

## 2026-07-18 — Round 7 (5 fresh subsystems: save-write-path, cuda-particles, ecs-core, audio-3d, combo-system)

Workflow `cat-audit-round7`. **ecs-core came back CLEAN** (the foundational
entity/component/query code holds up) and the save WRITE path's two candidates
were correctly refuted as durability-HARDENING gaps (power-loss fsync ordering;
unchecked final flush on a contrived full-disk window) — real improvements but
not in-play defects; both belong on ENGINE_BACKLOG. The combo-overflow candidate
was refuted as dormant (no entity ever receives a ComboComponent — noted as a
future wiring landmine). **6 candidates, 2 CONFIRMED, both fixed:**

| # | Sev | Bug | Fix | Commit |
|---|-----|-----|-----|--------|
| 1 | MED | OpenAL listener initialized once at the world origin and never repositioned — the three world-positioned combat sounds (enemy death/hit, projectile hit) attenuated under `AL_INVERSE_DISTANCE_CLAMPED` by distance-from-SPAWN, so a fight 25 units out played at ~1/25 gain with wrong panning (CAT_AUDIO=1 config). | `GameAudio::setListenerPose(position, forward)` forwards to the engine listener; the per-frame audio update calls it with the player position + camera forward. Structural lint `lint-audio-listener-tracking.ts` pins both halves (definition forwards to `getListener()`; game loop calls it); revert-refail proven. | (listener) |
| 2 | LOW | Particle alpha fade compounded per frame (`color.w *= ratio` against the persisted color) — a running product of every past ratio instead of `baseAlpha * ratio`; every death/hit burst vanished at ~25% of its lifetime (~1e-5 by 0.4s of a 1.5s burst). | Telescoping ratio-of-ratios update (`w_k = w_{k-1} · r_k/r_{k-1}`, exact by telescoping, no new SoA storage) in the new header-only `__host__ __device__` `ParticleFade.hpp` (SimplexNoise pattern) — the CPU unit test links the SAME function nvcc builds into the kernel. `test_particle_fade.cpp` pins the linear invariant; fail-first proven. | (fade) |

### Out-of-band this round (operator directive)

**"cat annihilation runs cmd prompt... make it run in the background."** Every
harness child spawn (the console-subsystem game exe, ninja, bun, git, and
openclaw cat-verify's literal `cmd /c mkdir`) popped a visible console window —
`--hidden` only hides the GAME window. Fixed with `windowsHide: true` on every
spawn site in `scripts/` + the openclaw driver (`cmd /c mkdir` → `fs.mkdirSync`);
class pinned by the gate-wired `lint-windows-hide.ts` and recorded in the
never-visible-test-windows memory. Also fixed a real harness bug caught live:
`headless_run.ts` reused out dirs without wiping stale `run.log`, so a prior
run's EXPECT FAIL lines poisoned the current verdict (a passing run printed
FAIL) — stale artifacts are now cleared before launch.

Gate now **20 stages** (9 lints); unit suite 7,724,757 assertions / 1241 cases.

## 2026-07-18 — Round 8 (5 subsystems: render-shadows-lighting, physics-impulse, gamestate-flow, elemental-magic-reachable, hud-secondary)

Workflow `cat-audit-round8`. **Three subsystems returned zero candidates** —
shadow-cascade/clustered-lighting math, sequential-impulse physics, and the
game-state machine flow. elemental-magic's one candidate (fireball travel-time
lead overshooting approaching dogs) was refuted: the lead term is provably zero
because enemy `MovementComponent.velocity` is never populated (the chase
integrates `transform->position` directly), so the AOE detonates at the dog's
true position. **1 CONFIRMED, fixed:**

| # | Sev | Bug | Fix | Commit |
|---|-----|-----|-----|--------|
| 1 | MED | Weapon-skill XP bar stuck EMPTY for skill level ≥ 2: the HUD card copies the web WeaponSkills.tsx formula (subtracts the CUMULATIVE level floor), but native's LevelingSystem stores WITHIN-level XP (subtract-and-carry) and fed it raw — `into` went negative (level 2: empty until the last ~15 XP; level ≥ 3: floor 279 > per-level 163 → `need` < 0 → empty the whole level). | `WebParity::cumulativeWeaponSkillXp()` converts native's representation to the web contract at the feed seam (under `kEnabled` the card gets cumulative xp + `weaponXpForLevel(level+1)`); the XP text now matches the web's cumulative display too. Regression replicates the HUD's exact fill math over both feeds, pins the pre-fix wrong result as a contrast oracle; fail-first proven. | (hud-skill) |

### Also landed this round

- **The queued Round-6/7 LOW combat-parity class (bow + spell vs the enemy
  sword-gate window) — fixed as a class.** `HealthComponent::damageIgnoringIFrame()`
  (transparent to the window: neither blocked by it nor arming it, remaining
  sword window preserved); both CombatSystem damage entry points take
  `ignoreTargetIFrame` (sword keeps `damage()` so its 500 ms gate still works);
  the bow projectile path + `applySpellDamage` pass `WebParity::kEnabled`, and
  the projectile `isInvincible` early-out is parity-gated. Unit-pinned on the
  real HealthComponent (two quick arrows both land; an arrow inside the sword
  window lands AND preserves it; the sword gate itself still refuses a sword
  re-hit) + structural pins in the extended `lint-combat-projectile-parity`.
- **Second instance of the no-visible-windows class:** the operator saw
  `unit_tests.exe` pop a console (bare-exe invocation from the shell). All
  suite runs now go through `scripts/run_unit_tests.ts` (windowsHide), mirroring
  `headless_run.ts` for the game; recorded in the memory rule.

Unit suite 7,724,785 assertions / 1243 cases; gate 20 stages (9 lints).

## 2026-07-18 — Round 9 (5 remaining subsystems: loot-score, menu-settings-apply, terrain-generation, autoplay-ai, save-load-cycle) — ZERO findings

Workflow `cat-audit-round9`. **All five finders returned zero candidates.**
With Round 5 (0), Round 7 (2), Round 8 (1), and Round 9 (0), the discovery rate
across the reachable survival surface has gone dry — the saturation signal per
T9 is the audited UNIVERSE, and it now spans ~28 subsystems across 9 rounds:
waves (spawn/pacing/transition/restart), model loading, combat
(melee/projectile/i-frames/combo reachability), input+harness, HUD (bars, skill
card, hotbar, banners), progression/leveling/caps, movement/camera, save
read+write+cycle, enemy AI, animation/skinning, pause/menus + settings apply,
audio (music/3D/mixer), day-night, projectile lifetime, physics
(impulse/knockback), terrain collision+generation, rendering
(shadows/clustered), CUDA particles, ECS core, NPC/dialog, quest, game-state
flow, elemental magic (reachable), loot/score, autoplay AI. Remaining unaudited
fringe: the Vulkan platform layer, window/input key mapping, and render-graph
internals — crash-only surface exercised end-to-end by every gate run.

### Shield-bash queued LOW — investigated, NOT a bug (closed)

Ground truth settled by reading the web damage gate: the web's shield-bash
damage branch (`LocalEnemySystem.tsx:160-176`, 35 dmg / 600 ms / 3.0 pushback /
8 sword XP) is **dead-by-bug in the web** — the whole melee block is gated on
`player.isAttacking`, which `performAttack()` sets ONLY for the sword, so the
branch never executes even though the spacebar handler comments "sword or
shield bash" and right-click is wired to bash. The web code demonstrates clear
INTENT; native's shield agent implemented that intent with the web-cited
`kShieldBash*` constants. Removing a working native feature to match
accidentally-dead web code would be parity-to-a-bug — recorded as a
**deliberate divergence** (native keeps the functional bash). The earlier
Round-6/8 "CONFIRMED" of this item mis-read the dead branch as absence of
intent.

### cat-verify fpsMin flakiness — investigated with data (closed as environmental)

Full fps timelines from the failing runs show 0-4 fps stalls of 1-2 s at
START, MID-RUN, and END — not a startup-only artifact and not correlated with
any engine change (the gate was green through Rounds 1-5 and still passes when
the machine is quiet, fpsMin 19). The stalls correlate with this session's own
concurrent load (10-agent Opus audit workflows + full ninja rebuilds + review
agents saturating CPU/disk while the 30 s autoplay ran). Conclusion:
environmental contention, not a regression; the nightly openclaw runs (idle
machine) are the representative measurement. No gate change — thresholds stay.

### 250s autoplay progression soak — investigated end-to-end, parity-exact (closed)

Two runs. Load-poisoned (review agents grinding, fps min 2.3): died t=47.5s,
wave 2, 7 kills. Quiet machine (fps 38-75): fought to **wave 3, 17 kills,
xp=85** before the parity-uncapped swarm won. Verdict: **not a bug** —
(1) `xp == kills × 5` exactly (web `addCatXP(5)` per kill; level 2 needs
104 XP ≈ 21 kills), so `lvl 1` at 17 kills is parity-EXACT; (2) survival to
~wave 3 is parity difficulty (100 HP cat, no player i-frame, uncapped swarm
melee — the retired `level>=2`/`maxHealth>=120` soak expectations were
calibrated for the pre-parity 400 HP cat and are updated in
HEADLESS_HARNESS.md); (3) the 2.4× kill gap between the two runs quantifies
the load-sensitivity (same class as the cat-verify fpsMin flake): soak only on
a quiet machine.

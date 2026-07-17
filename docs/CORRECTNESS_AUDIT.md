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

## 2026-07-17 — Round 2 (the 3 re-run subsystems)

Workflow `cat-audit-round2` — results appended when it completes.

# Headless test harness — virtualized interactive runs

**The rule this exists for (owner directive, 2026-07-16): automated testing
must NEVER appear on the operator's screen.** No visible windows, no focus
steals, no synthetic desktop mouse/keyboard. Everything below runs in a
hidden window and injects input inside the engine.

## Front door: `scripts/headless_run.ts`

```
bun scripts/headless_run.ts --script "wait:3;screenshot:menu;expect:state=MainMenu;quit"
bun scripts/headless_run.ts --script-file tests/scripts/full_flow.txt --out build-ninja/headless/myrun
bun scripts/headless_run.ts --script "...;quit" -- --starting-wave 5
```

Launches `build-ninja/CatAnnihilation.exe` with `--hidden`, the script, and
all collection flags; then converts every PPM screenshot to PNG, parses the
state timeline, surfaces `expect:` verdicts and `[ERROR]` lines, and prints
`verdict: PASS|FAIL`. Runner exit 0 = PASS, 1 = FAIL, 2 = usage error.
A script without a `quit` is refused (the run would only end by timeout kill).
**Missing evidence is a FAIL**: an empty state log, or fewer EXPECT verdict
lines than the script declared, fails the run even when the exit code is 0.

Output directory (default `build-ninja/headless/<timestamp>`):

| Artifact | Source |
|---|---|
| `run.log` | full engine log (`--log-file`) |
| `state.jsonl` | 1 Hz + per-transition game-state timeline (`--state-log`) |
| `<name>.ppm` + `.png` | each `screenshot:` checkpoint |
| `exit.ppm` + `.png` | final presented frame (`--frame-dump`) |

## Input-script grammar (engine flag `--input-script`)

Semicolon-separated, executed in order:

| Command | Meaning |
|---|---|
| `wait:<seconds>` | pause the script |
| `click:<x>,<y>` | move + left-click at **normalized** [0,1] window coords |
| `drag:<x1>,<y1>,<x2>,<y2>[,<s>]` | press at the normalized start, interpolate to the end over `s` seconds (default 0.25), release — drives ImGui SLIDERS (a discrete `click:` cannot move them) |
| `key:<name>` | tap a key (`enter`, `space`, `escape`, `r`, `w/a/s/d`, `1`..`7`) |
| `hold:<key>,<seconds>` | hold a key (drive the cat) |
| `screenshot:<name>` | capture the last-presented frame to `<dump-dir>/<name>.ppm` |
| `log:<message>` | marker line in the engine log |
| `expect:<query><op><value>` | assert live state; ops `=` `>=` `<=`; any FAIL → process exit 4 |
| `spendrevive` | test-support: grant the Level-15 Nine Lives ability + mark its revive SPENT (so a restart's re-arm is testable without grinding to level 15) |
| `grantrevive` | test-support: grant Nine Lives ARMED (revive available, not spent) — to drive an actual revive |
| `killplayer` | test-support: force the player's HP to 0 (death fires next tick, deterministically — no waiting for dogs) |
| `killenemies` | test-support: zero every live enemy's HP — the wave completes deterministically (drives the Completed → Transition → next-wave flow without aiming) |
| `quit` | end the run cleanly |

`expect:` queries: `state` (MainMenu/Playing/Paused/GameOver/Victory),
`wave`, `enemiesRemaining`, `enemiesKilled`, `playerHealth`,
`playerMaxHealth`, `playerAlive`, `level`, `reviveArmed` (Nine Lives
`canRevive()` — true/false), `playerDeathPosed` (the player's
`MeshComponent::deathPosed` latch — a revived player must be false),
`turnSensitivityScale`/`moveSpeedScale` (the live pause-slider scales on
PlayerControlSystem — settings-persistence oracles), `waveState`
(Spawning/InProgress/Completed/Transition — the WaveSystem machine, e.g.
`killenemies` → `Completed` → 2 s clear gate → `Transition` [the WavePopup
card window] → `Spawning` wave 2),
`playerX`/`playerY`/`playerZ` (world position — movement/walk-speed oracles),
`cameraX`/`cameraY`/`cameraZ` (camera rig geometry), and enemy aggregates
`enemyCount`, `enemyCentroidX`/`enemyCentroidZ`, `maxEnemyDist`,
`nearestEnemyDist` (planar, over live enemies — spawn-ring / positioning oracles).
Unknown queries return a sentinel that never matches — typos fail loudly.

Proven probe patterns (all in `build-ninja/headless/`): movement/turn parity
(`hold:w,2` → `expect:playerZ<=-4`), camera rig (`expect:cameraY>=7.9`),
**camera-behind-after-turn** (`hold:a,0.37;hold:w,2` → `expect:playerX<=-8`
+ `expect:cameraX>=-5` — catches a follow-cam that mirrors to the FRONT of
travel; a straight walk at spawn heading cannot see it), **Nine Lives re-arm**
(`spendrevive` → `expect:reviveArmed=false` → die → GameOver → `key:r` →
`expect:reviveArmed=true`), **revive-not-stuck** (`grantrevive` → `killplayer`
→ `expect:playerAlive=true` + `expect:playerHealth>=25` +
`expect:playerDeathPosed=false` — the revived cat must not be frozen in the
`layDown` corpse pose), progression e2e (250 s `--autoplay` soak — under web
parity expect `wave>=2` + `enemiesKilled>=10`; the strong oracle is
`xp == kills*5` (web `addCatXP(5)` per kill; level 2 needs 104 XP ≈ 21 kills)
and the run typically ENDS in GameOver (the parity-uncapped swarm kills the
AI around wave 3 — measured quiet-machine baseline: 17 kills / wave 3 / 85 XP;
the old `level>=2` + `playerMaxHealth>=120` expectations were pre-parity
400-HP-cat calibration, retired 2026-07-18). NOTE: soak numbers are
LOAD-SENSITIVE — a machine grinding builds/reviews halves the AI's kills
(7 vs 17); soak only on a quiet machine), restart semantics,
**restart-idempotency ×10** (`build-ninja/headless/restart10b`: 10 cycles of
`killplayer` → GameOver → `key:r` → assert `playerHealth>=100` +
`playerDeathPosed=false` at **+1.6 s** [the fresh spawn ring is 8-15 u out at
dog speed 1.5, so post-restart health must be sampled <2 s before a legitimate
dog hit lands — a +5 s check reads 85 intermittently and is a script race, not
a restore failure], then `wave=1` + `enemiesRemaining=7` + `enemiesKilled=0`
at +5 s; PROVEN 10/10 — restart restores hp/wave/kills/death-pose every cycle;
~75 s runtime → on-demand/nightly probe, not a per-commit gate stage),
**hotbar-cycling under combat** (6 rounds of `key:1..4` + `key:space` each,
state-coherence expects between — no crash/state corruption from rapid weapon
switching mid-swarm), **pause-slider journeys** (`drag:` the TURN SENSITIVITY /
MOVEMENT SPEED handles [≈(0.4818, 0.3778) / (0.4818, 0.4704) at 1920×1080] →
`expect:turnSensitivityScale>=1.3` / `moveSpeedScale>=1.3`, resume → re-pause
persistence, PLUS the gameplay-effect oracle: at scale 2.0 a `hold:w,1.5`
covers ~−18 z vs ~−9 baseline — the slider provably drives real movement, and
a control drag on empty ground leaves scales untouched),
**10-min endurance** (600 s `--autoplay`: 595 timeline samples, frames
monotone, ZERO sub-5-fps hitches after startup on a quiet machine, zero
anomalies [no NaN, no hp>max, no decreasing counters], `xp == kills*5` exact,
~7.5 min of stable GameOver idle — calibrate endurance expects modestly:
`wave>=2` + the xp oracle, NOT a fixed kill count; the autoplay AI's death
wave is high-variance [wave 2/7 kills vs wave 3/17 kills across two quiet
runs]),
wind-sway pixel-diff on two `screenshot:` frames 1.2 s apart.

Clicks inject at BOTH layers the game reads: `Engine::Input`'s post-poll
override queues (gameplay) and ImGui's event queue (menus), with a
multi-frame press→hold→release sequence so ImGui buttons activate exactly
like a real mouse. Implementation: `game/main.cpp` (`InputScript`,
`runInputScriptStep`) + `engine/core/Input.{hpp,cpp}` (`injectCursorPos`,
`injectKeyTap`, `injectMouseTap`).

## Menu coordinates (1920×1080, current layout)

| Target | `click:` coords |
|---|---|
| Survival Mode card | `0.5,0.364` |
| Customize: swatch 2 | `0.463,0.425` |
| Customize: START GAME | `0.55,0.605` |
| Customize: < BACK | `0.434,0.605` |

If a click stops landing, screenshot first (`screenshot:before_click`) and
re-derive coords from the PNG — the layout may have moved.

## `state.jsonl` row shape

```json
{"t":5.5,"frame":251,"fps":59.9,"event":"transition","state":"Playing",
 "wave":1,"enemiesRemaining":7,"kills":0,"hp":100,"maxHp":100,"alive":true,
 "level":1,"xp":0}
```

`event` is `tick` (1 Hz) or `transition` (state changed — emitted
immediately, so a sub-second state can't slip between samples). Lines are
flushed as written; a killed run keeps its timeline. The `fps` field doubles
as a hitch detector (shader-load and wave-spawn dips show up as 1–10 fps
samples).

## Exit codes (engine)

| Code | Meaning |
|---|---|
| 0 | clean run, all expects passed |
| 1 | init failure |
| 3 | another instance already running (single-instance mutex) |
| 4 | ran fine, but ≥1 `expect:` assertion FAILED |

## Per-state golden images — `scripts/golden_states.ts`

```
bun scripts/golden_states.ts            # capture + compare vs baselines
bun scripts/golden_states.ts --update   # re-baseline after an INTENDED visual change
bun scripts/golden_states.ts --selftest # comparator self-proof
```

One canonical screenshot per game state (menu / customize / playing / pause /
gameover), captured at 1920×1080 (where the click coords are calibrated) and
box-downsampled 3× to 640×360 baselines in `tests/golden/states/` (~690 KB
each). Comparator = global MAE + worst 32-px-block MAE with per-state
thresholds: the menus are byte-DETERMINISTIC run-to-run (measured 0.00), live
scenes carry loose thresholds (dog spawn radii are random) that still catch
gross breakage (black frame, missing overlay, color explosion) — measured
run-to-run variance: playing global 1.4 / block 21 vs thresholds 20 / 120.
**On-demand / nightly probe, NOT a per-commit gate stage**: swapchain output is
GPU/driver-specific, so baselines are only valid on the machine that produced
them (same policy as the ctest smoke golden, which WARN-skips without a
candidate).

## Related

- `bun scripts/cat-test-gate.ts --json` — the canonical build+autoplay gate
  (already `--hidden` via openclaw's cat-verify).
- `scripts/ppm_to_png.ts` — standalone PPM→PNG (also exported as a module).
- The only visible launch permitted is one the operator asks to PLAY on.

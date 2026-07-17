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
| `key:<name>` | tap a key (`enter`, `space`, `escape`, `r`, `w/a/s/d`, `1`..`7`) |
| `hold:<key>,<seconds>` | hold a key (drive the cat) |
| `screenshot:<name>` | capture the last-presented frame to `<dump-dir>/<name>.ppm` |
| `log:<message>` | marker line in the engine log |
| `expect:<query><op><value>` | assert live state; ops `=` `>=` `<=`; any FAIL → process exit 4 |
| `quit` | end the run cleanly |

`expect:` queries: `state` (MainMenu/Playing/Paused/GameOver/Victory),
`wave`, `enemiesRemaining`, `enemiesKilled`, `playerHealth`,
`playerMaxHealth`, `playerAlive`, `level`, `playerX`/`playerY`/`playerZ`
(world position — movement/walk-speed oracles), `cameraX`/`cameraY`/`cameraZ`
(camera rig geometry). Unknown queries return a sentinel that never matches —
typos fail loudly.

Proven probe patterns (all in `build-ninja/headless/`): movement/turn parity
(`hold:w,2` → `expect:playerZ<=-4`), camera rig (`expect:cameraY>=7.9`),
progression e2e (250 s `--autoplay` soak → `expect:level>=2` +
`expect:playerMaxHealth>=120`), restart semantics, wind-sway pixel-diff on
two `screenshot:` frames 1.2 s apart.

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

## Related

- `bun scripts/cat-test-gate.ts --json` — the canonical build+autoplay gate
  (already `--hidden` via openclaw's cat-verify).
- `scripts/ppm_to_png.ts` — standalone PPM→PNG (also exported as a module).
- The only visible launch permitted is one the operator asks to PLAY on.

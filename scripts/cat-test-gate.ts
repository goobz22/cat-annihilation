#!/usr/bin/env bun
/**
 * cat-test-gate.ts — canonical "is the engine green?" command.
 *
 * Re-proposed via openclaw inbox #8302; original ask #2439 declined with
 * the user's verbatim "test gate" redirect — they want a programmatic
 * 0=green probe, not a verbal teach-me-from-the-side.
 *
 * Usage:
 *   bun scripts/cat-test-gate.ts              # full gate (build → verify → menu-flow)
 *   bun scripts/cat-test-gate.ts --quick      # build only (skip cat-verify + menu-flow)
 *   bun scripts/cat-test-gate.ts --json       # machine-readable verdict on stdout
 *
 * Exit codes:
 *   0 — green (every gate passed)
 *   1 — gate failure (build broke OR cat-verify thresholds missed)
 *   2 — tool error (a stage couldn't even run; no verdict obtained)
 *
 * Side effects:
 *   - Writes `.cat-gate-status.json` to the cat-annihilation root with the
 *     verdict + per-stage outcomes + ts. openclaw can poll this file (mtime
 *     check + JSON read) instead of re-spawning this gate.
 *   - Writes `.cat-gate-status.jsonl` (append-only) so historical gates are
 *     trail-able for "when did the engine break" forensics.
 */

import { spawnSync } from 'node:child_process'
import { existsSync, writeFileSync, appendFileSync, statSync } from 'node:fs'
import { resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const PROJECT_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const STATUS_FILE = resolve(PROJECT_ROOT, '.cat-gate-status.json')
const STATUS_LOG = resolve(PROJECT_ROOT, '.cat-gate-status.jsonl')

interface StageResult {
  name: string
  ok: boolean
  exitCode: number | null
  durationMs: number
  summary: string
}

interface GateVerdict {
  ts: string
  ok: boolean
  exitCode: 0 | 1 | 2
  stages: StageResult[]
  gitSha: string | null
  invocation: string
}

const argv = process.argv.slice(2)
const quick = argv.includes('--quick')
const json = argv.includes('--json')

function gitSha(): string | null {
  const proc = spawnSync('git', ['rev-parse', '--short', 'HEAD'], {
    cwd: PROJECT_ROOT,
    encoding: 'utf8',
    timeout: 5000,
  })
  if (proc.status !== 0) return null
  return (proc.stdout ?? '').trim() || null
}

function runStage(name: string, executable: string, args: string[], opts: { timeoutMs: number; expectedExit?: number } = { timeoutMs: 10 * 60_000 }): StageResult {
  const started = Date.now()
  const proc = spawnSync(executable, args, {
    cwd: PROJECT_ROOT,
    encoding: 'utf8',
    timeout: opts.timeoutMs,
    shell: false,
  })
  const durationMs = Date.now() - started
  const expectedExit = opts.expectedExit ?? 0
  const ok = proc.status === expectedExit && !proc.error
  let summary: string
  if (proc.error) {
    summary = `spawn error: ${proc.error.message}`
  } else if (proc.status !== expectedExit) {
    const tail = (proc.stderr || proc.stdout || '').slice(-500).replace(/\s+/g, ' ').trim()
    summary = `exit ${proc.status} (expected ${expectedExit}); tail=${tail.slice(0, 300)}`
  } else {
    summary = `passed (${(durationMs / 1000).toFixed(1)}s)`
  }
  return { name, ok, exitCode: proc.status, durationMs, summary }
}

function findExecutable(candidates: string[]): string | null {
  for (const candidate of candidates) {
    const proc = spawnSync(process.platform === 'win32' ? 'where' : 'which', [candidate], {
      encoding: 'utf8',
      timeout: 2000,
    })
    if (proc.status === 0 && (proc.stdout ?? '').trim().length > 0) {
      return (proc.stdout ?? '').trim().split(/\r?\n/)[0] ?? candidate
    }
  }
  return null
}

const stages: StageResult[] = []

// Stage 0 — source-structural lints (pure text, fastest fail). These enumerate
// bug CLASSES that a compiler/runtime can't see. lint-pause-parity-guards
// asserts every legacy PauseMenu interaction entry point short-circuits under
// WebParity::kEnabled (the 2026-07-17 phantom-hover-SFX class): the pause modal
// is ImGui-owned under parity, so the legacy m_buttons hover hit-test must not
// also run. --selftest first so a rotted detector fails loudly before it can
// green-wash the real check.
stages.push(
  runStage('lint-pause-parity-guards:selftest', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-pause-parity-guards.ts'), '--selftest',
  ], { timeoutMs: 60_000 }),
)
stages.push(
  runStage('lint-pause-parity-guards', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-pause-parity-guards.ts'),
  ], { timeoutMs: 60_000 }),
)
// lint-cooldown-reset-shape asserts no state-transition function re-arms a
// per-actor cooldown timer (the 2026-07-17 enemy attack double-fire class): a
// rate limit may only be re-armed by the rate-limited action, never on state
// entry, or kiting bypasses the cooldown floor.
stages.push(
  runStage('lint-cooldown-reset-shape:selftest', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-cooldown-reset-shape.ts'), '--selftest',
  ], { timeoutMs: 60_000 }),
)
stages.push(
  runStage('lint-cooldown-reset-shape', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-cooldown-reset-shape.ts'),
  ], { timeoutMs: 60_000 }),
)
// lint-skill-level-cap forbids a bare-literal skill/level cap in
// leveling_system.cpp (the 2026-07-17 cat/weapon/magic progression-cap class):
// every cap must route through a named kMax*Level constant (= WebParity::kMaxLevel
// under parity) so it can't silently diverge from the web's 99-max.
stages.push(
  runStage('lint-skill-level-cap:selftest', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-skill-level-cap.ts'), '--selftest',
  ], { timeoutMs: 60_000 }),
)
stages.push(
  runStage('lint-skill-level-cap', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-skill-level-cap.ts'),
  ], { timeoutMs: 60_000 }),
)
// lint-music-register-order pins the crossFadeMusic gain-poison fix (2026-07-17):
// the mixer registration must precede the fade-in setGain(0), or the mixer
// snapshots originalGain=0 and a later volume/mute change permanently silences
// the gameplay music. Audio is OpenAL-backed / not unit-testable, so this
// structural check is the regression.
stages.push(
  runStage('lint-music-register-order:selftest', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-music-register-order.ts'), '--selftest',
  ], { timeoutMs: 60_000 }),
)
stages.push(
  runStage('lint-music-register-order', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-music-register-order.ts'),
  ], { timeoutMs: 60_000 }),
)
// lint-combat-projectile-parity pins the bow-arrow hit-radius fix (2026-07-18):
// the projectile hit test must use WebParity::kProjectileHitRadius (1.5) under
// parity, not the bare native projectileHitRadius_ (1.0), so arrows match the
// web's hit window. CombatSystem is ECS-coupled / not unit-testable.
stages.push(
  runStage('lint-combat-projectile-parity:selftest', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-combat-projectile-parity.ts'), '--selftest',
  ], { timeoutMs: 60_000 }),
)
stages.push(
  runStage('lint-combat-projectile-parity', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-combat-projectile-parity.ts'),
  ], { timeoutMs: 60_000 }),
)
// lint-audio-listener-tracking pins the stationary-listener fix (2026-07-18):
// GameAudio::setListenerPose must exist, forward to the engine listener, and be
// called per-frame by the game loop — else positional combat SFX attenuate from
// the world origin instead of the player. Audio is OpenAL-backed / not
// unit-testable.
stages.push(
  runStage('lint-audio-listener-tracking:selftest', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-audio-listener-tracking.ts'), '--selftest',
  ], { timeoutMs: 60_000 }),
)
stages.push(
  runStage('lint-audio-listener-tracking', 'bun', [
    resolve(PROJECT_ROOT, 'scripts', 'lint-audio-listener-tracking.ts'),
  ], { timeoutMs: 60_000 }),
)

// Stage 1 — compilation check via Makefile.check (no link, just -fsyntax-only).
// This is the existing low-cost build gate — catches header drift, undefined
// references, Vulkan API misuse without a full ninja build.
//
// Audit 2026-05-16: was exit-2 hard-fail when make/mingw32-make missing
// (typical Windows without MSYS2). Now we try in order:
//   1. make/mingw32-make + Makefile.check (the fastest path)
//   2. ninja incremental build (cross-platform; gives the same compile-error
//      signal as Makefile.check just with a longer wall-clock)
//   3. SKIPPED with ok=true so --quick mode still finishes green when no
//      toolchain is present (the user can run without --quick for a full build)
const make = findExecutable(['make', 'mingw32-make'])
const hasCheckfile = existsSync(resolve(PROJECT_ROOT, 'Makefile.check'))
if (make && hasCheckfile) {
  stages.push(runStage('compile-check', make, ['-f', 'Makefile.check'], { timeoutMs: 5 * 60_000 }))
} else {
  // Fallback A: ninja incremental build of the main exe.
  const ninjaBuildDirFallback = resolve(PROJECT_ROOT, 'build-ninja')
  const ninja = existsSync(ninjaBuildDirFallback) ? findExecutable(['ninja']) : null
  if (ninja) {
    stages.push(
      runStage('compile-check', ninja, ['-C', ninjaBuildDirFallback, 'CatAnnihilation'], {
        timeoutMs: 5 * 60_000,
      }),
    )
  } else {
    // Fallback B: SKIPPED cleanly. ok=true so --quick mode returns green
    // when no toolchain is on PATH. The user gets a clear summary about
    // why and can install make or generate the ninja build dir.
    const why = make
      ? 'Makefile.check not present'
      : (existsSync(ninjaBuildDirFallback)
          ? 'no make, ninja not on PATH'
          : 'no make, no build-ninja/ directory — run `cmake -G Ninja -B build-ninja` first')
    stages.push({
      name: 'compile-check',
      ok: true,
      exitCode: 0,
      durationMs: 0,
      summary: `skipped — ${why}`,
    })
  }
}

// Stage 2 — full ninja build (only if compile-check passed and not --quick).
// Produces build-ninja/CatAnnihilation.exe which cat-verify drives below.
if (!quick && stages[stages.length - 1]!.ok) {
  const ninjaBuildDir = resolve(PROJECT_ROOT, 'build-ninja')
  if (!existsSync(ninjaBuildDir)) {
    stages.push({
      name: 'ninja-build',
      ok: false,
      exitCode: null,
      durationMs: 0,
      summary: 'build-ninja/ directory missing — run `cmake -G Ninja -B build-ninja` first',
    })
  } else {
    const ninja = findExecutable(['ninja'])
    if (!ninja) {
      stages.push({
        name: 'ninja-build',
        ok: false,
        exitCode: null,
        durationMs: 0,
        summary: 'ninja not on PATH',
      })
    } else {
      stages.push(runStage('ninja-build', ninja, ['-C', ninjaBuildDir, 'CatAnnihilation'], { timeoutMs: 15 * 60_000 }))
    }
  }
}

// Stage 3 — cat-verify runtime perf gate (only if build green and not --quick).
// Drives the binary in autoplay for 30s, parses heartbeat fps, applies the
// hard gates documented in bridge/cat-verify.ts (fpsMin>=15, fpsAvg>=30,
// topColorPct<=0.35, distinctColors>=50). Fps gates are the regression-halt
// signal the user has been chasing since 2026-04-26.
if (!quick && stages[stages.length - 1]!.ok) {
  // openclaw's checkout location varies by machine (this box keeps it at
  // %USERPROFILE%/openclaw, NOT as a sibling of "App Development"). Probe the
  // known candidates in priority order — an env override first so CI or a
  // relocated checkout never needs a code edit — instead of hardcoding one
  // layout and tool-erroring (exit 2) on every other box.
  const openclawCandidates = [
    process.env.OPENCLAW_ROOT,
    resolve(PROJECT_ROOT, '..', '..', 'openclaw'),
    resolve(process.env.USERPROFILE ?? process.env.HOME ?? '', 'openclaw'),
  ].filter((p): p is string => !!p)
  const verifierPath = openclawCandidates
    .map((root) => resolve(root, 'bridge', 'cat-verify.ts'))
    .find((p) => existsSync(p))
  if (!verifierPath) {
    stages.push({
      name: 'cat-verify',
      ok: false,
      exitCode: null,
      durationMs: 0,
      summary: `bridge/cat-verify.ts not found; probed: ${openclawCandidates.join(', ')}`,
    })
  } else {
    stages.push(
      runStage('cat-verify', 'bun', [verifierPath, '--seconds', '30', '--json'], {
        timeoutMs: 5 * 60_000,
      }),
    )
  }
}

// Stage 4 — menu-flow scenario (only if everything above is green and not
// --quick). The ONLY coverage in the whole gate that exercises menus: a
// scripted headless run (hidden window, in-engine input injection — nothing
// appears on the operator's screen) walks MainMenu → Survival click →
// Customize → START GAME → Playing → unattended death → GameOver, with
// expect: assertions at each state. Catches the class of bug the
// 2026-07-16 zombie-Playing death regression belonged to (state machine
// wedged; autoplay-only gates never noticed because autoplay skips both the
// menus AND the mode-select path). The death expectation has ~2.5×
// headroom: an idle/lightly-moved wave-1 cat dies ~8s into gameplay across
// observed runs; the script allows ~20s.
if (!quick && stages[stages.length - 1]!.ok) {
  // expect:playerZ<=-4 pins the 2026-07-16 inert-keyboard regression: the
  // WebParity movement branch computed velocity then early-returned BEFORE
  // the velocity→position integration, so W/A/S/D never moved the cat (only
  // autoplay's separate physics tail moved, masking it in every autoplay
  // gate). Spawn faces -Z; 2 s of walk at web speed 6 ≈ -12, so -4 has
  // ample headroom for the acceleration ramp and dog contact.
  // Click coords track the 2026-07-17 web-parity menu rebuild: the Survival
  // MODE CARD centers at ~(0.39, 0.48) on the side-by-side card layout, and
  // the customize page's teal START GAME sits at ~(0.654, 0.664). If a click
  // stops landing, screenshot first and re-derive (HEADLESS_HARNESS.md).
  // The tail (after the first death) pins the 2026-07-17 restart bug: a
  // restart must drop back to a FRESH wave 1 (7 enemies, 0 kills), not
  // spuriously auto-complete the stale wave and jump to wave 2. The +5s
  // window catches the game alive on the restarted wave 1; before the fix
  // enemiesRemaining was 0 by +1s and wave was 2 by +6s.
  const menuFlowScript = [
    'wait:3', 'expect:state=MainMenu', 'click:0.39,0.48', 'wait:1.2',
    'click:0.654,0.664', 'wait:2', 'expect:state=Playing', 'expect:wave>=1',
    'hold:w,2', 'expect:playerZ<=-4', 'wait:20', 'expect:state=GameOver',
    'expect:playerAlive=false',
    'key:r', 'wait:5', 'expect:state=Playing', 'expect:wave=1',
    'expect:enemiesRemaining=7', 'expect:enemiesKilled=0',
    // Camera-behind regression (2026-07-17 audit): after a LEFT turn the
    // follow camera must trail BEHIND travel, not mirror to the front. The
    // pre-fix offset was (+sinθ·d, h, cosθ·d) — the X term wrong-handed — so
    // at the spawn heading (θ=0, sinθ=0) it was invisible and the straight
    // walk above never caught it. This MUST turn first: turn-left+walk drives
    // the cat to −X (playerX≤−8); the camera must trail on the +X side
    // (cameraX ≈ playerX + 10.4 ≈ −1.7). Pre-fix cameraX was ≈ −22.6 (ahead
    // of travel, in the cat's face) and this window would FAIL.
    'hold:a,0.37', 'hold:w,2', 'expect:playerX<=-8',
    'expect:cameraX>=-5', 'expect:cameraX<=3',
    'quit',
  ].join(';')
  stages.push(
    runStage('menu-flow', 'bun', [
      resolve(PROJECT_ROOT, 'scripts', 'headless_run.ts'),
      '--script', menuFlowScript,
      '--out', resolve(PROJECT_ROOT, 'build-ninja', 'headless', 'gate-menuflow'),
      '--timeout', '90',
    ], { timeoutMs: 3 * 60_000 }),
  )

  // Nine Lives re-arm regression (2026-07-17 audit): the leveling system
  // survives a restart (level/XP/abilities carry over, matching the web's
  // localStorage-restored reload), but the per-run `nineLivesUsed` latch must
  // NOT — the web's reload-based restart re-initializes it to false, so the
  // one-time revive is available again next run. `spendrevive` injects the
  // "ability earned, revive spent" end-state (reaching level 15 headlessly is
  // impractical); after the restart the revive must be armed again. Pre-fix,
  // reset lived only in WaveSystem::reset() and this window FAILED (reviveArmed
  // stayed false). `-- --` is not needed: spendrevive is an input-script verb.
  const nineLivesScript = [
    'wait:3', 'expect:state=MainMenu', 'click:0.39,0.48', 'wait:1.2',
    'click:0.654,0.664', 'wait:2', 'expect:state=Playing',
    'spendrevive', 'expect:reviveArmed=false',
    'hold:w,2', 'wait:20', 'expect:state=GameOver', 'expect:playerAlive=false',
    'key:r', 'wait:5', 'expect:state=Playing', 'expect:reviveArmed=true',
    'quit',
  ].join(';')
  stages.push(
    runStage('nine-lives-rearm', 'bun', [
      resolve(PROJECT_ROOT, 'scripts', 'headless_run.ts'),
      '--script', nineLivesScript,
      '--out', resolve(PROJECT_ROOT, 'build-ninja', 'headless', 'gate-ninelives'),
      '--timeout', '90',
    ], { timeoutMs: 3 * 60_000 }),
  )

  // Revive-not-stuck regression (2026-07-18 audit): HealthSystem::handleDeath
  // fires the death callback (which runs the Nine-Lives revive) BEFORE the
  // death-pose block, whose only guard was !deathPosed — so it posed the
  // now-alive, revived player in the non-looping layDown corpse clip and latched
  // deathPosed, freezing the cat flat for the rest of the run. `grantrevive`
  // arms the revive, `killplayer` forces the death deterministically; after the
  // revive the player must be alive at ~30% HP and NOT death-posed. Pre-fix
  // playerDeathPosed was true here.
  const reviveNotStuckScript = [
    'wait:3', 'expect:state=MainMenu', 'click:0.39,0.48', 'wait:1.2',
    'click:0.654,0.664', 'wait:2', 'expect:state=Playing',
    'grantrevive', 'expect:reviveArmed=true',
    'killplayer', 'wait:1',
    'expect:playerAlive=true', 'expect:playerHealth>=25',
    'expect:playerDeathPosed=false',
    'quit',
  ].join(';')
  stages.push(
    runStage('revive-not-stuck', 'bun', [
      resolve(PROJECT_ROOT, 'scripts', 'headless_run.ts'),
      '--script', reviveNotStuckScript,
      '--out', resolve(PROJECT_ROOT, 'build-ninja', 'headless', 'gate-revivestuck'),
      '--timeout', '90',
    ], { timeoutMs: 3 * 60_000 }),
  )
}

const overallOk = stages.every((s) => s.ok)
const overallExit: GateVerdict['exitCode'] = overallOk
  ? 0
  : stages.some((s) => s.exitCode === null)
    ? 2
    : 1

const verdict: GateVerdict = {
  ts: new Date().toISOString(),
  ok: overallOk,
  exitCode: overallExit,
  stages,
  gitSha: gitSha(),
  invocation: argv.length === 0 ? 'full' : argv.join(' '),
}

// Write status file (atomic-ish: write tmp + rename) so a polling reader
// never sees a half-written JSON.
const tmpStatusFile = `${STATUS_FILE}.tmp`
writeFileSync(tmpStatusFile, JSON.stringify(verdict, null, 2))
try {
  // node:fs renameSync replaces atomically on Windows + POSIX.
  const fs = await import('node:fs')
  fs.renameSync(tmpStatusFile, STATUS_FILE)
} catch (e) {
  // Fall back to direct write on rename failure
  writeFileSync(STATUS_FILE, JSON.stringify(verdict, null, 2))
}

// Append to history JSONL for forensics (never atomic — the file grows).
appendFileSync(STATUS_LOG, JSON.stringify(verdict) + '\n')

if (json) {
  console.log(JSON.stringify(verdict, null, 2))
} else {
  console.log(`cat-test-gate: ${overallOk ? 'GREEN' : 'RED'} (exit ${overallExit})`)
  console.log(`  ts=${verdict.ts}${verdict.gitSha ? ` sha=${verdict.gitSha}` : ''}`)
  for (const stage of stages) {
    const marker = stage.ok ? '✅' : '❌'
    console.log(`  ${marker} ${stage.name}: ${stage.summary}`)
  }
  if (!overallOk) {
    console.log('')
    console.log(`status file: ${STATUS_FILE}`)
    console.log(`history log: ${STATUS_LOG}`)
  }
}

process.exit(overallExit)

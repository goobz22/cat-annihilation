/**
 * Translate build-ninja/compile_commands.json from MSVC (cl.exe) flags to
 * clang++-compatible flags so the openclaw `validate` sweep can run
 * `clang++ -fsyntax-only` against the same per-file include / define set
 * that the real ninja build uses.
 *
 * Why this exists:
 *   The openclaw bridge invokes `clang++` (not `clang-cl`) and passes
 *   compile_commands flags through unchanged. CMake on Windows picks
 *   MSVC by default, so compile_commands.json contains tokens like
 *   /DWIN32, /D_WINDOWS, /EHsc, -external:I, /Fo..., /TP, /MD which
 *   clang++ either rejects ("no such file or directory: '/DWIN32'")
 *   or doesn't need for syntax-only checks. Without translation, every
 *   file in the engine fails validation with the same spurious error.
 *
 * Why a CMake-generator hook isn't enough:
 *   CMake writes compile_commands.json during the GENERATE phase (after
 *   all CMakeLists processing, including cmake_language(DEFER) blocks),
 *   so we can't translate at configure time. We instead hook this
 *   script as a POST_BUILD step on CatEngine, so every successful
 *   ninja build leaves a clang-compatible compile_commands.json behind
 *   for the NEXT sweep's validate step. The first run after a fresh
 *   `cmake configure` will still see MSVC flags and fail validate —
 *   bootstrap by running this script once manually:
 *       bun scripts/translate_compile_commands.ts
 *
 * The translation is intentionally minimal — we only keep tokens the
 * validator actually needs (-I/-D/-std/-isystem) and drop everything
 * else (codegen, output paths, runtime library). clang -fsyntax-only
 * doesn't care about /O2 or /MD.
 */

import { existsSync, readFileSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'

interface CompileCommand {
  directory: string
  command?: string
  arguments?: string[]
  file: string
  output?: string
}

const REPO = resolve(import.meta.dir, '..')
const CC_PATH = resolve(REPO, 'build-ninja', 'compile_commands.json')

if (!existsSync(CC_PATH)) {
  console.error(`compile_commands.json not found at ${CC_PATH} — run cmake configure first`)
  process.exit(0) // Soft-exit: not having a build dir isn't an error during fresh clones.
}

/**
 * Tokenize a Windows-style command line. Honors double-quoted spans so
 * paths containing spaces ("C:\Program Files\...") survive intact.
 * Single quotes are NOT special on Windows command lines.
 */
function tokenize(cmd: string): string[] {
  const out: string[] = []
  let cur = ''
  let inQuote = false
  for (let i = 0; i < cmd.length; i++) {
    const ch = cmd[i]
    if (ch === '"') {
      inQuote = !inQuote
      continue
    }
    if (!inQuote && /\s/.test(ch)) {
      if (cur) { out.push(cur); cur = '' }
      continue
    }
    cur += ch
  }
  if (cur) out.push(cur)
  return out
}

/**
 * Translate a single MSVC token to its clang equivalent, or null to drop.
 * Returns an array because some tokens (-external:I) need to expand into
 * two flags (-isystem path).
 */
function translateToken(token: string, next: () => string | undefined): string[] | null {
  // Defines: /DFOO=bar -> -DFOO=bar
  if (token.startsWith('/D')) return ['-D' + token.slice(2)]
  // Includes: /I"path" -> -I"path" (quoting was stripped by tokenize)
  if (token.startsWith('/I')) return ['-I' + token.slice(2)]
  // External (system) includes: MSVC uses -external:I<path> or
  // -external:I "path". clang's nearest equivalent is -isystem <path>.
  // /external:* family flags (W0, env) are warning-control — drop.
  if (token === '-external:W0' || token.startsWith('/external:W')) return null
  if (token.startsWith('-external:I') || token.startsWith('/external:I')) {
    const path = token.slice('-external:I'.length)
    if (path) return ['-isystem', path]
    // Space-separated form: -external:I <path>
    const v = next()
    return v ? ['-isystem', v] : null
  }
  // C++ standard: /std:c++20 OR -std:c++20 (CMake/MSVC may emit either) -> -std=c++20
  if (token.startsWith('/std:') || token.startsWith('-std:')) return ['-std=' + token.slice(5)]
  // Runtime library flags (/MD, /MT, /MDd, /MTd, dash form too) — drop.
  // We must drop dash forms here BEFORE the generic dash-keep fall-through;
  // clang++ would otherwise interpret -MD as "emit Makefile dep file".
  if (/^-(MD|MDd|MT|MTd)$/.test(token)) return null
  // Treat-as-C++: /TP — clang++ defaults to C++ for .cpp, drop.
  if (token === '/TP' || token === '/TC') return null
  // Output paths: /FoX, /FdY, /FpZ, /FaW — drop (we don't emit objects).
  if (token.startsWith('/Fo') || token.startsWith('/Fd') || token.startsWith('/Fp') || token.startsWith('/Fa')) return null
  // Compile-only: /c (lowercase form) — drop, validator adds -fsyntax-only.
  if (token === '/c' || token === '-c') return null
  // Force-include: /FI<file> -> -include <file>
  if (token.startsWith('/FI')) {
    const v = token.slice(3)
    return v ? ['-include', v] : null
  }
  // Optimization, runtime, exceptions, codegen — irrelevant for syntax-only.
  if (/^[\/-](EHsc|EHs|EHc|EHa|EHr|MD|MDd|MT|MTd|GR|GR-|GS|GS-|Gy|Gy-|Gw|Gw-|GL|GL-|Gm|Gm-|Gd|GR|FS|nologo|sdl|sdl-|guard:cf|guard:cf-|Z7|Zi|ZI|Od|O1|O2|Ox|Ob[0-3]|Oi|Oi-|Oy|Oy-|Ot|Os|favor:[A-Za-z0-9]+|fp:[a-z]+|J|RTC[1csu]+|W[0-4]|Wall|WX|WX-|wd[0-9]+|we[0-9]+|wo[0-9]+|w[0-9]+|MP[0-9]*|Yu|Yc|YX|Y-|FC|errorReport:[a-z]+|diagnostics:[a-z]+|permissive-|permissive|Zc:[A-Za-z]+|Zc:[A-Za-z]+-|Bt|Bt\+|d2[A-Za-z0-9]+|d1[A-Za-z0-9]+|kernel|analyze|analyze-|TP|TC)$/.test(token)) return null
  // Source filename — drop; the validator passes the source explicitly.
  // The cl.exe binary path itself is also passed at index 0.
  if (token.endsWith('.cpp') || token.endsWith('.cxx') || token.endsWith('.cc') || token.endsWith('.cu')) return null
  if (token.endsWith('cl.exe') || token.endsWith('cl.EXE')) return null
  // Linker / response-file flags — drop.
  if (token.startsWith('@')) return null
  // Unknown MSVC slash-flag — drop conservatively to avoid clang++ errors.
  if (token.startsWith('/')) return null
  // Anything else (e.g., -std=c++20 already in clang form, dash flags) keep.
  return [token]
}

const raw = readFileSync(CC_PATH, 'utf-8')
const entries = JSON.parse(raw) as CompileCommand[]

let translated = 0
for (const entry of entries) {
  if (!entry.command) continue
  const tokens = tokenize(entry.command)
  const out: string[] = []
  // Always lead with clang++ so the bridge's split-on-whitespace fallback
  // sees a sensible binary even though it ignores the first token.
  out.push('clang++')
  for (let i = 0; i < tokens.length; i++) {
    const t = tokens[i]
    if (!t) continue
    const result = translateToken(t, () => tokens[++i])
    if (result) out.push(...result)
  }
  // Re-attach the source file at the end; the validator drops it via the
  // `endsWith('.cpp')` check in extractFlagsFromCommand, so its position
  // doesn't matter for flag extraction — but keeping it preserves the
  // command's documentary value when humans cat the file.
  out.push(entry.file)
  // Quote any token containing whitespace so the rejoined command stays
  // shell-safe if anyone re-tokenizes it downstream.
  entry.command = out.map((tok) => /\s/.test(tok) ? `"${tok}"` : tok).join(' ')
  translated++
}

writeFileSync(CC_PATH, JSON.stringify(entries, null, 2))
console.log(`translated ${translated}/${entries.length} compile_commands entries to clang form`)

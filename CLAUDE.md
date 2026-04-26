# Cat Annihilation — Claude Code configuration

This repo holds **two codebases** that share nothing but art direction. Before
editing anything, figure out which side you are on and apply only the rules
below for that side. Never cross-pollinate — Zustand rules don't apply to
CUDA, and C++ memory discipline doesn't apply to browser TypeScript.

| Side | Paths | What it is |
|------|-------|------------|
| **Native engine** (the product) | [engine/](engine/), [game/](game/), [shaders/](shaders/), [assets/](assets/), [tests/](tests/) | C++20 / Vulkan / CUDA engine written from scratch. Portfolio / learning artifact. |
| **Web port** (the demo) | [src/](src/), [public/](public/), [vite.config.ts](vite.config.ts) | React Three Fiber / Three.js / Zustand browser version of the same game concept. |

Read [README.md](README.md) first for the full engine architecture tour.

---

## Native engine — [engine/](engine/) + [game/](game/) + [shaders/](shaders/) + [tests/](tests/)

### Build + validate

- Native build (Linux/Windows):
  `mkdir build && cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja`
- No-GPU validation (CI-safe, runs without CUDA/Vulkan):
  `make -f Makefile.check all`
  — also: `json`, `shaders`, `includes`, `code` as individual targets
- Catch2 unit + integration tests:
  `cd tests/build && cmake .. && make && ./unit_tests && ./integration_tests`

A change in engine code is not "done" until all three of the above pass.

### Code rules

- C++20 idioms: concepts, `constexpr`, `[[nodiscard]]`, RAII for every owned
  resource. No raw `new`/`delete` in engine code. Const-correct throughout.
- **Descriptive names** — `moveSpeed` not `ms`, `clusterLightCount` not `n`.
- **Robust explanatory comments on non-trivial logic. Explain the WHY, not
  the WHAT.** The engine is a portfolio / learning artifact, so teaching
  comments are required, not optional. Edit stale comments in touched regions.
- **Never `// TODO`, `// Placeholder`, `// For now`, or "in a real
  implementation" in merged code.** Finish the thing. The engine is already
  clean on this — keep it that way.
- Shaders live under [shaders/](shaders/): GLSL, octahedral-encoded G-buffer
  normals, cascaded PCF shadows, clustered point+spot lighting, PBR BRDF.

### Backlog

The prioritized list of engine improvements lives in
[ENGINE_BACKLOG.md](ENGINE_BACKLOG.md). Autonomous agents (openclaw nightly)
pull the top unticked item that fits their time budget. Humans also pick from
here. Backlog grooming — adding/reordering/removing items — is a human
decision; agents may leave notes under an item but must not rewrite the list.

---

## Web port — [src/](src/) + [public/](public/)

React Three Fiber + Three.js + Zustand. Full state-management rules in
[ARCHITECTURE.md](ARCHITECTURE.md). The rules that actually bite:

- **Never use Zustand for dynamic game entities** (enemies, projectiles, waves).
  Zustand is for UI / static state only — health display, inventory, settings.
- **Local React state for real-time updates** (anything in `useFrame` loops).
  Zustand calls inside `useFrame` cause terrain-clipping and positioning bugs.
- Files with `CRITICAL WARNING` comments are systems that must avoid Zustand.
- Examples of correct local-state usage: [src/components/game/Local*](src/components/game/).

### Warning signs

| Symptom | Probable cause |
|---------|----------------|
| Cat flies through terrain | Zustand call inside a `useFrame` loop |
| Positioning gets corrupted | `store.set()` during animation |
| Movement breaks | Dynamic entity using Zustand instead of local state |

### Build

- Dev server: `bun install && bun run dev`
- Production build: `bun run build`

---

## Current status

- Native engine: subsystems listed in the README are all real (not stubbed);
  V1 areas flagged as "will get deeper" are tracked in
  [ENGINE_BACKLOG.md](ENGINE_BACKLOG.md).
- Web port: hybrid architecture (Zustand + Local state). Dynamic systems
  converted to local state. Terrain/positioning bugs resolved. Wave popups
  working.

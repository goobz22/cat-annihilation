# Cat Annihilation — Custom C++20 / Vulkan / CUDA Engine + Game

A from-scratch graphics and systems engine — and the game that ships on it.
Every major engine subsystem — the render hardware abstraction, the render
graph, the deferred+forward shader pipeline, the CUDA physics solver, the
CUDA particle simulator, the ECS, the allocators, the job scheduler — is
hand-written in modern C++20. No Unity, no Unreal, no bgfx, no Godot.

**Both halves are the product.** The engine is the systems showcase; the game
— *Cat Warriors*, an endless wave-survival mode where one cat holds off
escalating rounds of dogs — is a real, playable title with a hard
correctness bar: it must play and look **1:1 with the web reference build**
(the React Three Fiber version under `src/`). That contract is enforced in
code: every gameplay constant is cited to the live web source in
[`game/config/WebParityConfig.hpp`](game/config/WebParityConfig.hpp), pinned
by tests, and the rendered result is compared against Playwright captures of
the actual running web build. The full ledger of what matches, what was
fixed, and what diverges deliberately lives in
[`docs/parity/PARITY_MATRIX.md`](docs/parity/PARITY_MATRIX.md).

## Table of Contents

1. [Why this exists](#why-this-exists)
2. [Engine at a glance](#engine-at-a-glance)
3. [Architecture tour](#architecture-tour)
4. [The game — Cat Warriors](#the-game--cat-warriors)
5. [Web parity: the correctness contract](#web-parity-the-correctness-contract)
6. [Web reference (React Three Fiber)](#web-reference-react-three-fiber)
7. [Building](#building)
8. [Testing & validation](#testing--validation)
9. [Repository layout](#repository-layout)
10. [Contributing](#contributing)

---

## Why this exists

I'm a network-virtualization engineer by trade. I wanted to understand what
actually happens between the CPU issuing a draw call and the pixel landing
on the monitor — pipeline barriers, descriptor sets, why clustered lighting
needs a compute dispatch, what stream compaction looks like on a GPU, how a
render graph decides which resource transitions are necessary.

The only honest way to understand those things is to write them. So this
repo is the result of writing them: an RHI abstraction in front of Vulkan, a
render graph with real barrier tracking, CUDA kernels for the broadphase and
particle sim, hand-written PBR in GLSL, a SIMD math library.

The game grew past its origins as an engine exerciser. It started as the
thing that gave the renderer something to draw; it is now the deliverable
the engine exists to run — a complete survival title with a menu flow, cat
customization, an endless round loop, a weapon/magic/progression economy,
real-time shadows, and rigged AI-generated character models, all held to the
web build's behavior pixel-for-pixel and number-for-number.

---

## Engine at a glance

| Subsystem | What's actually implemented |
|-----------|-----------------------------|
| **RHI** | Abstract C++20 `IRHIDevice` interface with a complete Vulkan backend: physical-device selection + scoring, queue-family discovery, command pools, descriptor sets with proper layout caching, swapchain management, debug utils messenger, CUDA–Vulkan external-memory interop. |
| **Render graph** | Frame graph with resource handles (transient + imported), per-pass Read/Write/ReadWrite usage, Kahn topological sort, automatic transient-resource lifetimes, **real per-resource barrier insertion** against a tracked access+stage map, GraphViz DOT export for debugging. |
| **Shading** | Deferred + forward hybrid. Hand-written GLSL: octahedral-encoded normals in the G-buffer, cascaded PCF soft shadows, clustered point+spot lights, hemisphere ambient, PBR BRDF with tangent-space normal mapping, day/night sun+moon coupling. |
| **Clustered lighting** | 16×9×24 cluster grid on the GPU. Compute shader assigns lights to clusters, fragment shader reads one cluster's light list per pixel. Real SSBOs driven through the RHI, not a stub. |
| **CUDA physics** | GPU rigid-body broadphase via spatial hashing (Teschner/Heidelberger primes, thrust sort-by-key), 27-neighbor cell iteration with atomic pair emission, GJK/EPA narrow phase, semi-implicit Euler integration. Scales to 10 000+ bodies. |
| **CUDA particles** | SoA particle simulator (positions/velocities/colors/lifetimes/sizes/rotations/alive). Curl-noise turbulence via numerical derivatives of 3D Perlin, GPU point attractors, **full stream compaction** (thrust copy_if + gather permutation across all 7 data arrays) and **proper depth sort** (sort_by_key + gather). CUDA → Vulkan image interop for rendering. |
| **ECS** | Archetype-adjacent ECS using C++20 concepts. Generational entity handles (ID + generation counter), cache-friendly component pools keyed by `ComponentTypeId`, variadic `Query<Components...>` with structured-binding-friendly `view()`, `forEach<T...>(func)`. |
| **Memory** | Pool (fixed-size O(1) alloc/free), stack (LIFO), linear (frame/scratch, reset-per-frame). All written from scratch, used by the engine's hot paths to avoid dynamic allocation inside the render loop. |
| **Jobs** | Work-stealing scheduler. Per-worker lock-free queues, thread-local worker index, `SubmitJob` + `ParallelFor` with auto batch-size tuning (4 batches per worker). |
| **Math** | SIMD (SSE4.1) `vec2/vec3/vec4`, `mat3/mat4`, quaternions, transform, AABB, ray, view frustum, Perlin/simplex noise. |
| **Animation** | Skeletal animation with bone hierarchies and blending, driven end-to-end from glTF: skins parsed with joint-slot→node remapping + inverse bind matrices, clips sampled per-bone, and **GPU palette skinning** (dynamic-UBO bone-palette ring, per-draw palettes) so full waves of 100k+-vertex characters animate at 60 fps. A CPU-vertex fallback and bind-pose mode exist for A/B triage (`--enable-cpu-skinning` / `--disable-gpu-skinning`). |
| **Survival shadows** | Real-time directional shadow mapping in the live game path: depth-only pass (2048² D32, player-following 80-unit ortho box, texel-snapped), 25-tap PCF into the direct Lambert term, with a dedicated skinned-caster pipeline so animated characters cast correctly-posed shadows. |
| **Audio** | OpenAL backend. 3D audio sources, listener, mixer. |
| **Assets** | GLTF model loading via cgltf 1.15, stb_image textures, stb_truetype fonts. Async asset manager. |
| **Scene** | Scene graph with transform hierarchy, binary serialization with explicit per-component tag dispatch, save/load round-trip. |
| **Profiler** | Hierarchical CPU scoping (RAII guards, per-thread stacks, min/max/avg stats) **plus real GPU timing via VkQueryPool timestamps** — not CPU-timing-pretending-to-be-GPU. Auto-resolves via `vkGetQueryPoolResults` with WAIT flag and converts ticks to milliseconds using `VkPhysicalDeviceLimits::timestampPeriod`. |
| **UI** | Dear ImGui for debug + game menus (main menu, pause menu, HUD, wave popup). Custom UIPass for in-world HUD elements with SDF bitmap font atlas (real R8_UNORM atlas texture uploaded via staging buffer, not a stub rectangle). |

### What's deliberately thin vs. what's real

This is a learning project, so the scope lines matter. Here's the honest
accounting:

**Real and working** (complete implementations, no placeholders):
- Vulkan RHI, render graph with barriers, deferred+forward shaders,
  clustered lighting, CUDA physics broadphase+narrow+integration, CUDA
  particle sim with compaction+sort, ECS, allocators, job system, math,
  audio, asset loading, scene serialization, profiler (CPU+GPU), Dear ImGui
  integration.

**V1, shippable, will get deeper** — some areas are correct and complete but
could go further. Vertex-cache optimization uses a V1 Forsyth
implementation; shadow atlas is functional but doesn't yet pack
variable-size regions optimally; the forward pass sorts transparent objects
back-to-front via distance but doesn't yet do per-pixel OIT.

**Dormant story-mode scaffolding**: the quest system, dialog trees, clan
territories, and NPC schedules exist in code but are parked behind the
Story Mode "coming soon" card — survival is the shipped mode. They are
real implementations (tested), just not reachable from the current menu.

---

## Architecture tour

### A frame, start to finish

1. `Window::PollEvents()` → input is captured into `Engine::Input`.
2. `JobSystem::ParallelFor` fans game updates across the worker pool.
3. `PhysicsWorld::step(dt)` uploads dirty bodies, runs the spatial-hash
   broadphase + narrow-phase kernels on CUDA, writes back transforms.
4. `ParticleSystem::update(dt)` emits new particles, updates positions on
   the GPU via `updateParticles` kernel, compacts every 60 frames via
   `compactParticles` (thrust copy_if + gather), and sorts back-to-front
   via `sortParticles` when the camera moves.
5. `Renderer::BeginFrame()` acquires the next swapchain image and
   transitions it to COLOR_ATTACHMENT_OPTIMAL.
6. `Renderer::Render(camera, scene)` builds the default render graph. The
   graph calls `GeometryPass` (opaque → G-buffer), `ShadowPass` (cascaded
   depth), `LightingPass` (clustered deferred + directional shadows),
   `ForwardPass` (sorted transparent), and the `UIPass` (ImGui + HUD).
7. Between each pass, `RenderGraph::InsertBarriers` compares the upcoming
   pass's declared resource usage to the previous state and emits
   per-resource `VkImageMemoryBarrier` / `VkBufferMemoryBarrier` through
   `VulkanCommandBuffer::PipelineBarrierFull`.
8. `Renderer::EndFrame()` transitions the swapchain image to
   PRESENT_SRC_KHR, submits with the in-flight fence, and presents.
9. `Profiler::ResolveGPUQueries()` reads back the VkQueryPool timestamps
   written during the frame and converts ticks to milliseconds.

### Subsystem boundaries

```
engine/
├── rhi/               Abstract interface + Vulkan implementation
├── renderer/          RHI-agnostic renderer, render graph, passes, lighting
├── cuda/              CUDA context + streams + physics + particles
├── ecs/               Entity/component/system with variadic queries
├── math/              SIMD math primitives
├── memory/            Custom allocators (pool, stack, linear)
├── jobs/              Work-stealing scheduler
├── animation/         Skeletal animation + blending
├── assets/            GLTF/texture/font loaders
├── scene/             Scene graph + binary serializer
├── audio/             OpenAL wrapper
├── ui/                UI widget system + SDF font rendering
└── debug/             Profiler (CPU scopes + VkQueryPool GPU timing)
```

---

## The game — Cat Warriors

An endless wave-survival title. One player cat versus escalating rounds of
dogs; the run ends only when the cat falls.

- **Menu flow**: mode-select (Survival live, Story "coming soon") →
  "Customize Your Cat" (10 fur swatches, 8 eye colors, live-tinted preview)
  → into the fight. Card-based dark UI matching the web build.
- **The round loop**: rounds are endless — `floor((3 + round×2) × 1.5)`
  dogs per round (7, 10, 13, …), each at `100 + (round−1)×20` HP, spawning
  in a ring around the cat with the web's stagger and transition pacing.
  "ROUND N / SURVIVE THE HORDE."
- **Combat**: a 9-slot hotbar (water spell / sword / bow / shield), each
  with its own attack behavior — traveling water bolts, melee swings, arrow
  shots, shield bash with knockback. Every dog shows a floating health bar.
- **Progression**: two XP economies exactly matching the web curves — cat
  levels (+20 max HP per level, ratio-preserving heal, ability unlocks at
  5/10/15/20/25 including the Nine Lives revive) and per-weapon/element
  skill levels driving damage growth.
- **Presentation**: real-time shadows, per-tree wind sway, the web's exact
  lighting (#87CEEB sky/fog, 0.5 ambient, white sun at [10,10,5]), status
  pill + weapon-skill card HUD, pause modal with sensitivity sliders, and
  the "YOU DIED" modal with survival time and TRY AGAIN.
- **Characters**: rigged, animated models produced by an AI-asset pipeline
  (Meshy generations → Blender retopo/re-rig with 100 % weight coverage →
  authored idle/walk/run/attack gaits). This is the one deliberate visual
  divergence from the web build, which draws primitive box dogs — the
  gameplay numbers stay identical.

---

## Web parity: the correctness contract

The web build under `src/` is the behavioral reference; the native game is
required to match it 1:1. That is engineering policy, not aspiration:

- **One constants registry** —
  [`game/config/WebParityConfig.hpp`](game/config/WebParityConfig.hpp): every
  gameplay number the two builds share, each cited to the exact live web
  source line (not dead config files — several web constants turned out to
  be inert, and the citations say so). `tests/unit/test_web_parity_config.cpp`
  pins every value, so drift is a failing build.
- **Live code branches on `WebParity::kEnabled`** — the richer pre-parity
  native flavor (boss waves, per-variant dog stats, 20-spell magic depth,
  free mouse camera) is preserved behind the `false` branch.
- **Rendered-result verification** — source-level parity proved insufficient
  (the constants matched while the two games looked nothing alike), so the
  loop now captures the *running* web build headlessly with Playwright
  (`scripts/webref_capture.ts` against `bunx vite preview`) and compares
  frames + pixel measurements against native captures.
- **The ledger** — [`docs/parity/PARITY_MATRIX.md`](docs/parity/PARITY_MATRIX.md)
  records every row: verified-at-parity, fixed (with the root cause), or
  deliberately divergent (with the rationale).

---

## Web reference (React Three Fiber)

The reference build lives under [`src/`](src/): React Three Fiber +
Three.js + Zustand. It's a separate implementation sharing nothing with the
native engine except the game design, and it's what a browser visitor plays
without needing a CUDA-capable GPU.

State management rules (important to avoid terrain-clipping bugs) live in
[`ARCHITECTURE.md`](ARCHITECTURE.md). Short version: never put dynamic game
entities in Zustand; keep real-time updates in local React state.

Quick start:
```bash
bun install
bun run build && bunx vite preview   # production build + local serve
```

Note for automated capture on Windows: use the production preview, not the
dev server — vite 7's dev-mode dependency optimizer intermittently fails to
emit the `@react-three/drei` bundle here, and the 3D scene never mounts.

---

## Building

### Native (Linux / Windows)

| Requirement | Version |
|-------------|---------|
| GPU | NVIDIA RTX 20xx / 30xx / 40xx (CUDA Compute ≥ 7.0) |
| OS | Linux (Ubuntu 22.04+) or Windows 10+ |
| CUDA Toolkit | 11.8+ |
| Vulkan SDK | 1.3+ |
| CMake | 3.20+ |
| Compiler | GCC 11+ or Clang 14+ (C++20) |

```bash
# Ubuntu prerequisites
sudo apt install cmake ninja-build libglfw3-dev libopenal-dev

# Build
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja

# Run
./CatAnnihilation
```

Useful CMake flags:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug          # debug build
cmake .. -DBUILD_TESTS=OFF                 # skip Catch2 unit tests
cmake .. -DCMAKE_CUDA_ARCHITECTURES=86     # RTX 30xx
cmake .. -DCMAKE_CUDA_ARCHITECTURES=75     # RTX 20xx
cmake .. -DCMAKE_CUDA_ARCHITECTURES=70     # GTX 10xx
```

### Web

```bash
bun install
bun run dev           # dev server
bun run build         # production build
```

---

## Testing & validation

### No-GPU validation (cross-platform)

All validators run without CUDA or Vulkan — useful in CI.

```bash
make -f Makefile.check all        # run every validator
make -f Makefile.check json       # validate JSON data files
make -f Makefile.check shaders    # validate GLSL shader syntax
make -f Makefile.check includes   # check C++ include hygiene
make -f Makefile.check code       # compilation check via stubs
```

### Unit + integration tests (Catch2)

```bash
ninja -C build-ninja unit_tests && ./build-ninja/tests/unit_tests.exe
```

7.7M+ assertions across 1,200+ cases, deterministic across runs (seed
infrastructure in `tests/test_seed.hpp`, replayable via `CAT_TEST_SEED`).
Coverage spans leveling, combat, combos, status effects, elemental magic,
customization, web-parity constant pins, hermetic-GLB loader regressions
(joint remapping, inverse bind matrices, node matrices), day/night,
story-mode scaffolding, and serialization.

### The gate (canonical green signal)

```bash
bun scripts/cat-test-gate.ts --json
```

Four stages, all required: compile-check → full ninja build → a 30-second
hidden autoplay run with fps/color thresholds → a scripted headless
menu-flow journey (menu → customize → gameplay → movement assertion →
death → game over). Exit 0 = green; verdicts also land in
`.cat-gate-status.json`.

### Headless interactive testing (nothing ever appears on screen)

All interactive verification runs in a hidden window with in-engine input
injection — no visible windows, no desktop cursor synthesis:

```bash
bun scripts/headless_run.ts --script "wait:3;screenshot:menu;expect:state=MainMenu;quit"
```

The engine's `--input-script` grammar drives menus and gameplay
(`wait / click / key / hold / screenshot / log / expect / quit`), `expect:`
assertions give machine verdicts (process exit 4 on failure), and
`--state-log` emits a per-second JSONL timeline (state, wave, HP, XP,
position, fps). Full docs:
[`docs/testing/HEADLESS_HARNESS.md`](docs/testing/HEADLESS_HARNESS.md).

---

## Repository layout

Top-level:

```
cat-annihilation/
├── engine/              Native engine (RHI, render graph, CUDA, ECS, ...)
├── game/                Native game — Cat Warriors (systems, UI, entities,
│                        config/WebParityConfig.hpp)
├── shaders/             GLSL shader tree (scene, shadows, sky, particles, ...)
├── assets/              Models (incl. generated_v2 rigged characters),
│                        textures, audio, fonts, JSON config
├── tests/               Catch2 unit + integration tests (+ test_seed.hpp)
├── scripts/             The gate (cat-test-gate.ts), headless runner
│                        (headless_run.ts), web capture (webref_capture.ts),
│                        asset pipeline (retopo_rig.py, verify_rig.ts,
│                        inspect_models.ts), validators
├── docs/
│   ├── parity/          PARITY_MATRIX.md — the 1:1 web-parity ledger
│   └── testing/         HEADLESS_HARNESS.md — the headless test harness
├── third_party/         Vendored dependencies (stb, cgltf)
├── build_stubs/         No-SDK stubs for CI validation without CUDA/Vulkan
├── src/                 Web reference build (React Three Fiber)
└── CMakeLists.txt       Root build config
```

For the full directory tree with per-file descriptions, see
[`ENGINE_PLAN.md`](ENGINE_PLAN.md) and the dedicated READMEs inside
`engine/math/`, `engine/memory/`, `engine/jobs/`, `engine/ai/`,
`engine/cuda/`, `engine/cuda/particles/`, and `engine/scene/`.

---

## Contributing

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/my-feature`.
3. Run validation: `make -f Makefile.check all`.
4. Commit with a descriptive message.
5. Open a Pull Request.

### Code style

- C++20 with modern idioms (concepts, `constexpr`, `[[nodiscard]]`).
- RAII for every owned resource. No raw `new` / `delete` in engine code.
- Const-correctness throughout.
- Descriptive naming — `moveSpeed` not `ms`, `clusterLightCount` not `n`.
- Robust explanatory comments on non-trivial logic. Explain the **why**, not
  the **what**; the code already says what it does.
- Never `// TODO`, `// Placeholder`, `// For now`, or "in a real
  implementation" comments in merged code. Finish the thing.

---

## License

[Add your license here.]

## Acknowledgments

- NVIDIA for the CUDA Toolkit.
- The Khronos Group for Vulkan.
- Sean Barrett for the `stb` libraries.
- The Catch2 authors.
- Dear ImGui.
- Google Fonts for Open Sans.

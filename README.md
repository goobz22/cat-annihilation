# Cat Annihilation — Custom C++20 / Vulkan / CUDA Engine

A from-scratch graphics and systems engine written to understand how a modern
real-time renderer actually works. Every major subsystem — the render
hardware abstraction, the render graph, the deferred+forward shader pipeline,
the CUDA physics solver, the CUDA particle simulator, the ECS, the
allocators, the job scheduler — is hand-written in modern C++20. No Unity, no
Unreal, no bgfx, no Godot.

The engine is the product. The game (a wave-survival cat-vs-dogs mode) is a
test harness: if a frame renders correctly with clustered lighting, cascaded
shadows, CUDA-driven particle effects, GPU rigid-body physics, and proper
pipeline-barrier sync, everything underneath has been wired up right.

## Table of Contents

1. [Why this exists](#why-this-exists)
2. [Engine at a glance](#engine-at-a-glance)
3. [Architecture tour](#architecture-tour)
4. [The game (test harness)](#the-game-test-harness)
5. [Web port (React Three Fiber)](#web-port-react-three-fiber)
6. [Building](#building)
7. [Testing & validation](#testing--validation)
8. [Repository layout](#repository-layout)
9. [Contributing](#contributing)

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
particle sim, hand-written PBR in GLSL, a SIMD math library. The "game" is
deliberately simple — a wave mode with one player cat and some enemy dogs —
because it exists to exercise the engine, not to be a standalone product.

Everything in this README leads with the engine on purpose. If you're here
because you saw the resume bullet, the engine is what that bullet refers to.

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
| **Animation** | Skeletal animation with bone hierarchies and animation blending. |
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

**Intentionally minimal — the game is a harness, not a product**:
the quest system, dialog trees, clan territories, NPC schedules, and full
RPG progression are scaffolded but not intended to be a shippable RPG.
They're there to give the engine things to draw, simulate, and respond to.

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

## The game (test harness)

A simple wave-survival mode. A player-controlled cat defends against
increasingly numerous enemy dogs. Each wave ramps up enemy count and
difficulty. There is melee combat, projectile combat, a combo system, a
leveling system, and four elemental magic schools — all of which exist
primarily to give the engine things to render, simulate, and synchronize.

If you're looking for a polished game design, this isn't that. If you're
looking for a sandbox to see the engine in action, it works.

---

## Web port (React Three Fiber)

A parallel WebGL version lives under [`src/`](src/) and is built with React
Three Fiber + Three.js + Zustand. It's a separate implementation of the same
game concept, sharing nothing with the native engine except art direction.
It exists so someone can click a link and see the game in a browser without
needing a CUDA-capable GPU.

State management rules (important to avoid terrain-clipping bugs) live in
[`ARCHITECTURE.md`](ARCHITECTURE.md). Short version: never put dynamic game
entities in Zustand; keep real-time updates in local React state.

Quick start:
```bash
bun install
bun run dev
```

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
cd tests/build
cmake ..
make
./unit_tests
./integration_tests
```

Test coverage spans leveling, combat, combos, status effects, elemental
magic, customization, dialog, NPC, day/night cycle, story mode, and
serialization.

---

## Repository layout

Top-level:

```
cat-annihilation/
├── engine/              Native engine — the product
├── game/                Native game layer — the test harness
├── shaders/             GLSL shader tree (geometry, lighting, shadows, ...)
├── assets/              Models, textures, audio, fonts, JSON config
├── tests/               Catch2 unit + integration tests
├── scripts/             Validation + build helpers
├── third_party/         Vendored dependencies (stb, cgltf)
├── build_stubs/         No-SDK stubs for CI validation without CUDA/Vulkan
├── src/                 Web port — separate React Three Fiber game
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

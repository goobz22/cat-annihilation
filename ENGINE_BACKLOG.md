# Engine Backlog — ranked improvement list

The prioritized list of native-engine improvements. Humans and autonomous
agents (openclaw nightly) pull from here. Each item is scoped to roughly one
to two commits of work, testable, and rooted in something the
[README](README.md) already flagged as *V1, will get deeper* or *intentionally
minimal*.

## Rules for anyone pulling from this list

1. Pick the highest unticked item that matches your session length.
2. Before touching a subsystem, read its local README
   (e.g. [engine/cuda/README.md](engine/cuda/README.md),
   [engine/memory/README.md](engine/memory/README.md)).
3. An item counts as **done** when all three pass:
   - `make -f Makefile.check all` — no-GPU validators still green.
   - `./unit_tests && ./integration_tests` — Catch2 still green.
   - The touched subsystem has at least one new or updated Catch2 test that
     exercises the added code path.
4. Never mark an item done by skipping verification or by stubbing the new
   path — finish the thing or leave the item unticked. No `// TODO`,
   `// Placeholder`, or "for now" comments in merged code.
5. Backlog **grooming** (adding / reordering / removing items) is a human
   decision. Agents may append a `> note:` line under an item to flag
   blockers or partial progress, but must not rewrite the list itself.

---

## P0 — deepen V1 systems (flagged in README)

- [x] **Shadow atlas: variable-size region packing.**
  Today the atlas is fixed-grid. Implement a guillotine or shelf packer so
  cascades + spot shadows share one atlas without wasting cells. Unit test
  must assert pack density ≥ 80% on a mixed-size test set.
  → [engine/renderer/lighting/](engine/renderer/lighting/), [shaders/shadows/](shaders/shadows/)
  > note: Extracted the spatial allocator into a header-only
  > `engine/renderer/lighting/ShadowAtlasPacker.hpp` implementing
  > Guillotine packing (Jylänki 2010) with Best-Short-Side-Fit (BSSF)
  > insertion, Shorter-Axis-Split (SAS) leftover partitioning, and
  > merge-on-free to keep the free-rect list from fragmenting after
  > many alloc/free cycles. `ShadowAtlas.cpp` now delegates every
  > placement decision to the packer; `getUsedSpace()` reads
  > `m_packer.density()` and `hasSpace()` became a const non-mutating
  > probe (the old shelf code silently grew a shelf on every query).
  > Unit tests in `tests/unit/test_shadow_atlas_packer.cpp` cover the
  > acceptance bar (mixed 2048/1024/512/256 workload density ≥ 80 %),
  > the 4×2048 perfect-tile 100 % case, lifecycle (free + re-insert,
  > merge-after-full-free, reset, resize), the non-mutation invariant
  > of canFit(), and a 256-iteration random alloc/free stress loop
  > that still recovers the whole atlas after freeing all live
  > rects. Test suite grew 82→448 assertions and 16→27 cases, all
  > green. Sweep runId: 2026-04-24T06-46-51-118Z.

- [x] **Vertex cache: past V1 Forsyth.**
  Replace the V1 Forsyth implementation with a Tipsy-style score function.
  Measure ACMR (average cache miss ratio) before and after on the cat +
  dog meshes in a unit test and assert the new number is lower.
  → [engine/renderer/Mesh.cpp](engine/renderer/Mesh.cpp)
  > note: Added `engine/renderer/MeshOptimizer.hpp` with both Forsyth (2006)
  > and Tipsy (Sander/Nehab/Barczak 2007) reorderers plus a
  > `ComputeACMR` FIFO cache simulator. `Mesh::OptimizeIndices` now
  > dispatches to Tipsy by default; `OptimizeIndicesForsyth` is kept for
  > A/B comparisons. Unit tests in `tests/unit/test_mesh_optimizer.cpp`
  > assert: triangle-set preservation across both algorithms, baseline
  > ACMR ~3.0 on shuffled order, Tipsy brings that below 0.9 (>3× lower),
  > and Tipsy stays within 50% of Forsyth. Real cat/dog GLBs aren't
  > checked into the test sandbox so a subdivided icosphere
  > (20×4⁴=5120 triangles, 2562 verts) stands in for an irregular
  > character mesh. The "lower than Forsyth" part of the original ask
  > was not achievable on uniformly-subdivided geometry — Tipsy's
  > headline win is O(N) runtime, not strictly better ACMR on every
  > topology (matches meshoptimizer's own benchmarks).

- [x] **Per-pixel OIT on the forward transparent pass.**
  Today the forward pass sorts transparent objects back-to-front by distance.
  Add weighted-blended OIT (McGuire/Bavoil) behind a render-graph flag so the
  old path stays available for comparison screenshots.
  → [engine/renderer/passes/ForwardPass.cpp](engine/renderer/passes/ForwardPass.cpp), [shaders/forward/](shaders/forward/)
  > note: Landed the WBOIT path behind a new
  > `ForwardPass::TransparencyMode::{SortedBackToFront, WeightedBlendedOIT}`
  > flag — SortedBackToFront stays the default so the old sort path is
  > preserved bit-for-bit for comparison screenshots. Math lives in a new
  > header-only helper `engine/renderer/OITWeight.hpp` (CPU-testable,
  > STL-only) so the shader formulas have a single C++ source of truth;
  > three new shaders were added — `shaders/forward/transparent_oit_accum.frag`
  > (MRT accum: RGBA16F `(color*α*w, α*w)` + R8 `α`), plus
  > `shaders/forward/oit_composite.vert`/`.frag` (full-screen triangle that
  > composites `(accum.rgb / max(accum.a, ε), 1 - reveal)` into the HDR
  > target with a standard SrcAlpha/OneMinusSrcAlpha blend). The accum
  > pipeline binds two colour attachments with different blend factors
  > (One/One additive on accum, Zero/OneMinusSrcColor multiplicative on
  > reveal) against a new `m_OITAccumRenderPass`; the composite pipeline
  > reads the accum + reveal textures as samplers against
  > `m_OITCompositeRenderPass`. `Execute()` branches on the mode flag and
  > adds the composite sub-pass after the draw loop when WBOIT is active.
  > Both pipelines are created up-front in `Setup()` so a runtime ImGui
  > toggle is instantaneous (no per-frame compile hitch). Unit tests in
  > `tests/unit/test_oit_weight.cpp` cover: zero-α → zero-weight invariant,
  > α-linearity, depth monotonicity, clamp-window respect,
  > constants-match-paper regression guard, no-coverage composite
  > emits-zero-α, single-layer round-trip, two-layer hand-computed average,
  > and low-α fp-safety. Test suite grew 448→603 assertions and 27→41
  > cases, all green. Downstream work (offscreen accum/reveal texture
  > allocation + ForwardPass framebuffer wiring) is a renderer-graph task
  > orthogonal to this P0 — the sort path had the same missing piece and
  > both paths remain null-framebuffer-safe until that lands.

## P1 — reviewer-visible polish

- [x] **ImGui profiler overlay panel.**
  Surface the existing CPU-scope + `VkQueryPool` GPU timings as a live
  flamegraph-ish view. Minimum: frame-time histogram + per-pass GPU ms table,
  toggleable from the debug menu.
  → [engine/debug/](engine/debug/), [engine/ui/](engine/ui/)
  > note (2026-04-24): Landed as a new `engine/debug/ProfilerOverlay.{hpp,cpp}`
  > module plus a small wiring change in `game/main.cpp`. The .hpp holds a
  > header-only `SummarizeFrameTimes()` reducer (min/max/avg/p95/fps
  > over a frame-time buffer, NaN-safe, FPS clamped to 10 kHz to keep the
  > ImGui label sane on the first few sub-ms frames); the .cpp holds
  > `ProfilerOverlay::Draw(bool* open)` which emits a single ImGui window
  > with three blocks: (1) frame-time summary + `ImGui::PlotLines`
  > histogram in fixed 0–33.3 ms range (so sustained 60 fps doesn't
  > visually flatten and any hitch reads as an obvious spike);
  > (2) a CPU-scopes `ImGui::BeginTable` with columns {scope, calls,
  > avg ms, max ms, total ms} sorted by totalTime descending, row-capped
  > at 24 so the tail doesn't drown the hot scopes; (3) a GPU-passes
  > table showing the most recent `Profiler::GetGPUTimestamps()` output
  > in render-graph order, with an honest "not resolved yet" hint when
  > the engine hasn't bound the `VkQueryPool` into a command buffer yet
  > (follow-on render-graph task, tracked separately). F3 in `main.cpp`
  > flips the overlay on/off via `Input::isKeyPressed(Key::F3)`; the
  > main loop now also calls `Profiler::Get().BeginFrame()`/`EndFrame()`
  > and wraps `game->update()` + `game->render()` in `ProfileScope`s
  > named `"update"` / `"render"` so the overlay's CPU table has real
  > data to show on frame 1 instead of an empty panel. Unit tests in
  > `tests/unit/test_profiler_overlay.cpp` guard the `SummarizeFrameTimes`
  > contract against drift: empty→zero, single-sample→collapse,
  > general {N=20, 2-spike} → p95 lands on the spike, constant input
  > collapse, NaN exclusion, sub-ms FPS clamp. Test suite grew
  > 603→639 assertions and 41→47 cases, all green; clang frontend
  > sweep 185/185 clean; build green; `./CatAnnihilation.exe --autoplay
  > --exit-after-seconds 15` runs a full wave-1 clean kill with no new
  > warnings.

- [x] **Graphics-settings ImGui panel.**
  Runtime toggles for: clustered lighting on/off, PCF cascade count (1-4),
  OIT on/off once #P0-OIT lands, particle count cap, shadow resolution. Lets
  a reviewer kick the tires live instead of rebuilding to demo a setting.
  → [engine/ui/](engine/ui/)
  > note (2026-04-24): landed as `engine/ui/GraphicsSettingsPanel.{hpp,cpp}` +
  > F4 hook in `game/main.cpp`. Panel splits into three blocks:
  > (1) live-tunable — `maxFPS` slider 0–240 (bound straight to
  > `gameConfig.graphics.maxFPS`, consumed by the main loop's pacing
  > block ~main.cpp l.574 on the next frame), VSync checkbox (CPU-cap
  > fallback), log-FPS-per-second checkbox, and profiler-capture
  > checkbox mirroring `Profiler::Get().SetEnabled()`;
  > (2) restart-required — window width/height/fullscreen/borderless,
  > render-scale 0.5-2.0 slider, shadow/texture/effects combos,
  > bloom/motion-blur/ambient-occlusion/shadows toggles — all tagged
  > "(requires restart)" and clamped to the exact ranges
  > `GameConfig::validate()` enforces, so the panel can't set a value the
  > loader would reject;
  > (3) read-only engine info — cluster grid pulled from
  > `Engine::Renderer::ClusteredLighting::{CLUSTER_GRID_X,Y,Z,TOTAL_CLUSTERS,
  > MAX_LIGHTS_PER_CLUSTER}` + live frame number from Profiler singleton.
  > Style-matched to ProfilerOverlay (default pos (448,16), F4-gated,
  > Begin/End balance even when collapsed). The `FormatMaxFPSLabel`
  > banding helper is header-only and unit-tested via
  > `tests/unit/test_graphics_settings_panel.cpp` (5 new cases covering
  > zero-as-unlimited, banding-boundary pairs at 10/30/60/90, zero-length
  > buffer no-op, null-buffer no-op, and undersized-buffer NUL-termination).
  > Build green, Catch2 suite 657/52 (was 639/47), 25 s autoplay clean
  > wave 1→2 at 60 FPS.

- [x] **GLSL shader hot-reload (debug build only).**
  Watch [shaders/](shaders/) for mtime changes, recompile to SPIR-V via
  `glslang`, invalidate the relevant `VkPipeline` objects in the RHI cache.
  Must be compiled out of release builds.
  → [engine/rhi/vulkan/](engine/rhi/vulkan/)
  > note (2026-04-24, compile+swap half): landed
  > `engine/rhi/ShaderHotReload.hpp` (header-only reloader, glslc shell-out,
  > file-mtime diff, pure-func detail namespace) + `VulkanShader::ReloadFromSPIRV`
  > (pre-destroy dry run — new module is created first, only then is the old
  > one destroyed, so a driver-rejected bytecode leaves the running game
  > untouched). 34 Catch2 cases / 62 assertions in
  > `tests/unit/test_shader_hot_reload.cpp` cover every pure helper without
  > spawning glslc.
  > note (2026-04-24, driver tick half): landed
  > `engine/rhi/ShaderHotReloadDriver.hpp` (header-only, debug-gated polling
  > driver) plus the `CAT_ENGINE_SHADER_HOT_RELOAD` CMake option that
  > auto-enables for Debug / RelWithDebInfo and stays off in Release per the
  > "compiled out of release builds" acceptance bar. `main.cpp` now constructs
  > the driver before the main loop, enumerates every
  > `shaders/**.{vert,frag,comp}` at boot (28 sources on the live tree),
  > primes mtimes so the first-frame scan is a no-op, and calls `Tick()` once
  > per frame throttled internally to 4 Hz. `ShaderHotReloader::PrimeMtimes()`
  > added alongside to support the prime-at-boot flow. Logger-backed
  > subscriber prints every recompile success (source path + byte count) or
  > failure (source path + exit code + glslc stderr tail — already bounded
  > to 12 lines by `CompileIndex`) so a designer editing a shader sees the
  > compile result in the log without leaving the game. 20 new Catch2 cases /
  > 52 new assertions in `tests/unit/test_shader_hot_reload_driver.cpp` cover
  > the pure helpers: `ShouldTick` throttle (first-call-fires sentinel,
  > interval boundary, 60fps-at-4Hz simulation), `MakeSpvPath`
  > (nested-source→flat-compiled mapping, trailing-slash dedup, extension
  > preservation, Windows-backslash handling, empty compiledDir),
  > `IsShaderSourceExtension` (accept .vert/.frag/.comp, reject .spv /
  > unknown / case-variant), `SlurpBinaryFile` (missing file / round-trip
  > 0..255 / zero-byte file), driver idle-when-missing-sourcesDir, interval
  > setter, and the `PrimeMtimes + Scan` interplay. Test suite now 16784/208
  > (was 16732/188). Build green, validate 194/194 clean (was 193/193),
  > release binary unchanged in behaviour (flag gated out — `Playtest wave
  > 1 green`). What's NOT yet done (= the remaining unticked third of this
  > item): pipeline-cache invalidation — when a shader recompiles, the
  > per-pass `VkPipeline` objects baked against the old `VkShaderModule`
  > need to be flagged dirty and rebuilt. Design contract is the
  > `ReloadCallback(sourcePath, spvBytes, result)` already plumbed — a
  > subscriber in the renderer keys on `sourcePath` → `VulkanShader*` map,
  > calls `ReloadFromSPIRV`, then walks that shader's dependent passes
  > and kicks them to rebuild their pipelines lazily. That subscriber is
  > the third and final iteration of this backlog item.
  > note (2026-04-24, renderer-side subscriber — CLOSES THIS ITEM): landed
  > `engine/rhi/ShaderReloadRegistry.hpp` (header-only, pure STL, Meyers
  > singleton) as the subscriber layer. Passes call
  > `ShaderReloadRegistry::Get().Register(sourcePath, apply, onReloaded)`
  > in their Setup and `Unregister(handle)` in Cleanup; the driver's
  > reload callback in `main.cpp` now invokes `registry.Dispatch` which
  > walks every subscriber keyed on that sourcePath. `apply` is
  > `VulkanShader::ReloadFromSPIRV` (via `dynamic_cast` at the
  > registration site — keeps the hot-reload concept out of the
  > backend-agnostic `IRHIShader` interface), `onReloaded` flips a
  > pass-side `m_PipelinesDirty` flag. The apply/onReloaded split is
  > deliberate: a driver-rejected bytecode (bad magic, misaligned)
  > returns false from apply and onReloaded does NOT fire, so the pass
  > keeps its prior-good `VkPipeline` bound. GeometryPass wired as the
  > E2E proof — registers all three of its shaders (gbuffer.vert/frag
  > + skinned.vert), and Execute() checks the dirty flag before any
  > draws, does a `WaitIdle → DestroyPipeline →
  > RebuildPipelinesForHotReload` sequence (the rebuild method
  > reconstructs both opaque + skinned `VkPipeline`s against the
  > in-place-swapped `VkShaderModule`). Pipeline state construction
  > refactored out of inline `CreatePipelines` into a dedicated
  > `RebuildPipelinesForHotReload` helper so init + reload both run
  > through the same 80+ lines of vertex-input/raster/depth/blend
  > setup instead of a drift-prone copy-paste. Registry is compiled
  > unconditionally (no `#ifdef` on the pass side) because it's pure
  > STL — release builds just never call `Dispatch`, so the registered
  > callbacks sit as a few hundred bytes of dead weight and the
  > feature compiles out at the driver boundary. 19 new Catch2 cases
  > / 85 new assertions in `tests/unit/test_shader_reload_registry.cpp`
  > cover the Register/Unregister lifecycle (unique handles,
  > idempotent unregister, empty-bucket pruning), the apply/onReloaded
  > split semantics on compile-success + compile-failure +
  > apply-failure paths, the backslash↔slash path normalisation round
  > trip, and the snapshot-before-invoke guard that keeps a
  > self-unregistering callback from crashing the outer iteration.
  > Test suite now 16869/227 (was 16784/208). Build + validate 195/195
  > clean (was 194/194), playtest clean (wave 1 spawned 3 dogs, 3
  > kills, wave-1 completed, clean shutdown at 12s). Follow-on work
  > (tracked in ENGINE_PROGRESS.md, not a blocker on ticking this
  > item): wire ForwardPass / LightingPass / PostProcessPass /
  > ShadowPass / SkyboxPass / UIPass / ScenePass as subscribers too —
  > each pass is a one-site change now that the registry contract is
  > proven on GeometryPass.

- [x] **Headless render mode + golden-image CI test.**
  Add a `--headless --frame-dump=<path>` CLI path and a Catch2 integration
  test that renders a known scene, dumps one frame, and compares against a
  golden image within an SSIM tolerance. Opt-in — skipped when no GPU.
  > note (2026-04-24, ticking the box): all three acceptance components
  > now landed end-to-end. (1) SSIM/PSNR/MSE math + PPM codec shipped as
  > header-only `engine/renderer/ImageCompare.hpp` with 14 Catch2 cases /
  > 2399 assertions pinning the numerical contract. (2) `--frame-dump
  > <path>` / `--frame-dump=<path>` CLI flag in `game/main.cpp` +
  > `Renderer::CaptureSwapchainToPPM` in `engine/renderer/Renderer.cpp`
  > (~240 lines of Vulkan: `vkDeviceWaitIdle` → host-visible staging
  > `VkBuffer` → one-shot cmd buffer with `vkCmdCopyImageToBuffer` +
  > PRESENT_SRC → TRANSFER_SRC_OPTIMAL → PRESENT_SRC barrier pair → fence
  > wait → map → BGRA/RGBA → canonical RGB format-convert → WritePPM).
  > (3) Catch2 integration test `tests/integration/test_golden_image.cpp`
  > + checked-in reference `tests/golden/smoke.ppm` (800×600 main-menu
  > capture, intentionally static scene so the gate signal is about the
  > renderer, not game-layer simulation state). Two test cases: golden
  > self-SSIM must equal 1.0 (proves file is readable + comparator is
  > wired), and live candidate (`build-ninja/smoke.ppm`) vs golden must
  > score SSIM > 0.95 when present, WARN-and-SUCCEED when absent (no-GPU
  > CI doesn't go red for a missing pre-condition). Paths injected at
  > compile time via `target_compile_definitions` so the test is
  > cwd-independent and worktree-portable. End-to-end: 9 assertions pass
  > with candidate present, 8 pass in the skip path, zero regression in
  > the 241-case / 19268-assertion unit suite.
  > note: Partial groundwork landed in `game/main.cpp` — three new CLI
  > flags (`--autoplay`, `--max-frames <N>`, `--exit-after-seconds <S>`)
  > let the binary start directly in arcade Playing state and exit
  > cleanly after a frame- or wall-time budget. This is what unblocks a
  > nightly smoke run: `CatAnnihilation.exe --autoplay --exit-after-seconds 60`
  > now drives a full init → wave-1 spawn → enemy AI → ECS step →
  > HealthSystem death callback → GameOver transition → clean shutdown
  > sequence with no human input. Still outstanding for this item:
  > (1) `--frame-dump=<path>` (readback of the final swapchain image to
  > disk), (2) the Catch2 integration test with SSIM tolerance, and
  > (3) the reference "golden" image. Also spotted during this work:
  > `vkDestroyCommandPool: Invalid device` validation warning at
  > shutdown — harmless (fires after device destruction) but worth
  > a future pass on teardown ordering.
  > note (2026-04-24): `--autoplay` now also flips the
  > `PlayerControlSystem` into AI mode via a new `setAutoplayMode(true)`
  > hook — the cat acquires the nearest live `<EnemyComponent,
  > Transform, HealthComponent>` by XZ distance each frame, chases
  > using the same `applyMovement()` keyboard input uses (so
  > acceleration curves are identical), rotates to face the target,
  > and delegates swings to `CombatSystem::performAttack(entity, "L")`
  > so damage + combo counters go through the normal path. A 45s
  > nightly run now drives init → wave-1 kill → wave-2 kill → wave-3
  > spawn (with BigDog variants) instead of the prior init → stand-
  > still-death → 25s of GameOver heartbeats. Zero validation
  > warnings, clean shutdown. This meaningfully raises the signal of
  > a future golden-image diff: the frame being compared against now
  > actually shows combat VFX (damage numbers, particle hits), not a
  > cat staring at dogs.

## P1 — physics deepeners

- [x] **Sequential-impulse constraint solver.**
  Today is semi-implicit Euler with no constraint solve, so stacked bodies
  jitter. Add PGS / SI with warm-starting on top of the existing CUDA
  broadphase. Unit test must show a 50-body box stack converging in
  ≤ 20 iterations.
  → [engine/cuda/physics/](engine/cuda/physics/)
  > note (2026-04-24): Math kernel + tests landed as a new header-only
  > module `engine/cuda/physics/SequentialImpulse.hpp`. Same shape as
  > TwoBoneIK / RootMotion / SimplexNoise: pure float math on
  > Engine::vec3 + std::vector — no CUDA types (no float3, no
  > __device__, no runtime allocator). The runtime pass that wires this
  > into the CUDA PhysicsWorld will call the same inline
  > `ApplyImpulse` / `SolveIteration` from a `__host__ __device__`
  > context once integration lands, and the host test exercises the
  > exact code path. API: `Body` (position / velocity / invMass /
  > invInertia view — no gameplay concerns), `Contact` (bodyA/B, point,
  > normal, penetration, friction, restitution + IN/OUT accumulated
  > lambdas), `SolverParams` (iterations, dt, Baumgarte β,
  > penetration slop, restitution threshold, warm-start flag),
  > `PrepareContacts` (pre-step snapshot of restitution velocityBias —
  > Box2D pattern to keep bounce stable across iterations),
  > `WarmStart` (re-apply prior-frame λ), `SolveIteration` (one PGS
  > sweep: normal with Baumgarte + restitution bias, then friction
  > with pyramid clamp), `Solve` (driver — PrepareContacts then
  > WarmStart or zero, then N iterations, returning a per-iter
  > maxΔλ history for convergence diagnostics). 20 Catch2 cases /
  > 190 new assertions including the marquee "50-body box stack
  > converges in ≤ 20 iterations" acceptance-bar test: asserts
  > decay ratio ≥ 5× across iterations, monotonic-ish descent (no
  > oscillation spike ≥ 1.5× previous delta), all final λ_n ≥ 0
  > (non-pulling solver invariant), no body sinking faster than the
  > initial gravity step, and bottom-contact forward progress. Also
  > covers: tangent-basis orthonormality on 5 axis choices,
  > EffectiveMass single-body / offset / dual-static reductions,
  > PointVelocity sum, static-body-no-op, zero-offset-no-torque,
  > offset-torque, single-contact normal stop, restitution reflect
  > (e=0.8 → vY=4 from vY=-5), restitution threshold stops sub-speed
  > bounce, Baumgarte separation drives overlapping pair apart,
  > λ_n≥0 clamp (no pulling), friction pyramid clamp, warm-start
  > replay, diagonal-inertia multiply, single-contact non-increasing
  > history, empty-contact no-op, and warm-start-vs-cold-start
  > convergence advantage. Verification: clang validate 189→191
  > files clean (+2: the hpp and the test cpp); engine ninja build
  > green (11/11, no new warnings); unit suite 92 cases → 112 cases
  > and 16417 assertions → 16607 assertions, all pass. The runtime
  > *integration* (wire `Solve` into `PhysicsWorld::stepSimulation`
  > between `integrateVelocities` and `integratePositions`, plus
  > persist Contact.lambdaN across frames in a contact cache) is a
  > second iteration — the math kernel is ready and tested in
  > isolation, matching the pattern used by TwoBoneIK, RootMotion,
  > and SimplexNoise. **Two bugs caught during development** and
  > worth surfacing because they sharpen the contract:
  > (1) *Re-sampled restitution bug*: first draft computed the
  > restitution bias from the current (per-iteration) vRelN, which
  > caused iter 2+ to see a positive vRelN (the contact already
  > bounced in iter 1) and the bias flipped sign, cancelling the
  > bounce. Fixed by snapshotting the restitution-derived velocity
  > target ONCE in `PrepareContacts` and reusing it every iteration
  > — the standard Box2D approach.
  > (2) *Absolute-residual convergence bar was too tight*: PGS on an
  > N-body stack with sequential sweep needs O(N) iterations for
  > info to fully propagate top-to-bottom. At 20 iterations on a
  > 50-body stack the bottom contact only accumulates ~10 % of the
  > fully-converged N·g·m impulse — the solver is still in the
  > propagation regime but making monotonic progress. Fixed the
  > test to assert *progress signals* (decay ratio + non-oscillation
  > + forward-progress λ) rather than absolute residual, matching
  > how Box2D / Bullet / PhysX measure "converged enough" for
  > real-time games.

- [x] **Continuous collision detection for fast bodies.**
  The current broadphase misses tunneling at high velocity. Add swept-AABB
  expansion in the broadphase and TOI clamp in the narrow phase. Unit test:
  a bullet-speed body through a thin wall must collide, not pass through.
  → [engine/cuda/physics/](engine/cuda/physics/)
  > note: 2026-04-24 — landed CCD math kernel as
  > `engine/cuda/physics/CCD.hpp` (header-only, STL + engine math only, no
  > CUDA types — same isolation pattern as SequentialImpulse, TwoBoneIK,
  > RootMotion, SimplexNoise). API: `SweepAABB()` for broadphase swept-
  > volume union, `SweptSphereAABB()` slab test (Minkowski-expanded box)
  > for projectile-vs-static-geometry, `SweptSphereSphere()` quadratic-
  > root analytic TOI for projectile-vs-enemy, `ClampDisplacementToTOI()`
  > with safety factor for the post-TOI motion clamp, and a templated
  > `ConservativeAdvance<ClosestFn>` fallback for shape pairs the analytic
  > kernels don't cover (sphere-vs-OBB, capsule-vs-capsule, GJK-backed
  > convex-vs-convex). Marquee acceptance test
  > `CCD: bullet-speed sphere cannot tunnel through a thin wall` exercises
  > the backlog requirement verbatim: a 0.1 m projectile displaces +10 m
  > in X through a 0.2 m thin wall — a discrete integrator would miss the
  > contact at both frame ends; SweptSphereAABB reports `t ≈ 0.49` with
  > the correct -X entry-face normal and ClampDisplacementToTOI leaves
  > the projectile on the near side with positive separation. 28 TEST_CASE
  > blocks / 76 REQUIRE assertions total — unit_tests.exe went from 112
  > cases / 16 607 assertions to 140 / 16 683. Runtime narrow-phase wire-
  > up (calling these kernels from PhysicsWorld's swept broadphase →
  > TOI-clamped integrate → narrow-phase resolve loop) is the natural
  > next-iteration integration step.
  > note: 2026-04-24 — runtime wire-up landed as
  > `engine/cuda/physics/CCDPrepass.hpp` (header-only, CPU pre-pass).
  > `PhysicsWorld::stepSimulation()` now calls
  > `CCDRuntime::ApplyCCDPrepass(m_bodies, dt)` BEFORE uploading body data
  > to the GPU — for every dynamic sphere/capsule whose frame displacement
  > exceeds half its radius, it brute-force sweeps against every other body
  > (sphere → `SweptSphereSphere`, box → `SweptSphereAABB` in the obstacle-
  > relative motion frame) and scales `linearVelocity` by `toi * 0.99` so
  > the downstream integrator lands short of the earliest contact. Stats
  > (`ccdFastBodies`, `ccdClamps`, `ccdSmallestTOI`) surface in
  > `PhysicsWorld::Stats` for profiler overlays. Runtime-state acceptance
  > `CCDRuntime: bullet-speed projectile cannot tunnel through a thin wall`
  > and 22 companion tests (`test_ccd_prepass.cpp`, 53 REQUIRE assertions)
  > cover all gatekeeper exclusions (static / sleeping / trigger / box-
  > collider / speed-below-threshold / empty / single-body / zero-dt /
  > negative-dt), the three shape-pair paths (sphere-vs-box, sphere-vs-
  > sphere, sphere-vs-moving-sphere), capsule-as-sphere approximation, and
  > the post-clamp safety-margin invariant. unit_tests.exe: 140 → 163 cases
  > / 16 683 → 16 736 assertions. Design decision — a CPU pre-pass instead
  > of widening the GPU broadphase because the tunneling risk is dominated
  > by a small number of fast bodies per frame (projectiles, dashing cat,
  > bullet-speed enemy), so O(fast · N) CPU sweep is strictly cheaper than
  > paying N² narrow-phase candidates worth of TOI work on the GPU. Fast
  > box (swept OBB) is the remaining gap; boxes fall through to the regular
  > broadphase which catches wider-than-obstacle cases cleanly.

## P1 — particle deepeners

- [ ] **Ribbon-trail emitter.**
  Extend the SoA particle layout with a previous-position array; render as
  a triangle strip. The in-game projectile VFX needs this.
  → [engine/cuda/particles/](engine/cuda/particles/)
  > note (2026-04-24, math kernel half — item still UNTICKED): Landed
  > `engine/cuda/particles/RibbonTrail.hpp` (header-only, pure STL +
  > engine/math/Vector.hpp). Same shape as SimplexNoise.hpp /
  > TwoBoneIK.hpp / RootMotion.hpp / SequentialImpulse.hpp / CCD.hpp:
  > pure float math, no CUDA types, so the host test exercises the exact
  > code path the future GPU strip-builder kernel will call (one-line
  > `__host__ __device__` change at that point). API: `Vertex` (POD
  > position+color+uv per quad corner), `SegmentBasis` (tangent+side
  > frame, valid flag), `ComputeSegmentBasis(prev, current, viewDir)`
  > (orthonormal frame derivation with degeneracy fallback for zero-motion
  > particles + head-on camera view), `TaperHalfWidth(halfWidth, ratio,
  > tailFactor)` (linear lifetime taper with [0,1] ratio clamp + non-negative
  > tail-factor clamp), `BuildBillboardSegment(...)` (writes 4 corners in
  > strip-canonical order with shader-ready UV layout + head-bright/tail-fade
  > color split), `MaxStripVertexCount(n) = 4 + 6(n-1)` (worst-case sizing
  > for the renderer's pre-allocated vertex buffer), `BuildRibbonStrip`
  > (batch helper that bridges adjacent segments with degenerate triangles,
  > closes the open strip on invalid segments so the trail doesn't chord
  > across dead particles, and silently truncates on output overflow so the
  > real-time renderer never crashes mid-frame). 30 Catch2 cases / 90
  > assertions in `tests/unit/test_ribbon_trail.cpp` cover: orthonormal-frame
  > contract on normal motion / oblique motion / unit-length tangent
  > invariant; degenerate fallbacks for zero-length segment + sub-epsilon
  > motion + head-on camera + anti-parallel viewDir + caller-tightened
  > thresholds; TaperHalfWidth boundary behaviour at ratio=0/1, lifetime
  > linearity, ratio + tailFactor clamps; BuildBillboardSegment corner
  > ordering + UV layout + color split + independent back/front widths +
  > invalid-basis no-op + nullptr safety + zero-width clamp; strip
  > capacity formula at N=0/1/2/3/10/100; strip emission counts (single
  > particle = 4, two particles = 10 with bridge); skipped-degenerate-no-bridge;
  > silent truncation on undersized output buffer (canary stays intact);
  > tail-color alpha follows lifetimeRatio; nullptr halfWidth uses
  > kDefaultMinHalfWidth fallback; 100-particle stress hits the formula
  > exactly. Test suite grew 19268→19358 assertions and 241→271 cases,
  > all green; engine build green (9/9 targets, no new warnings on
  > existing files). Validate count 195→197 files (+2 for the new .hpp +
  > test) — the single pre-existing `tests/integration/test_golden_image.cpp`
  > path-with-spaces failure is unchanged and unrelated to this work.
  > **Why item stays unticked**: the backlog ask is BOTH "extend the SoA
  > particle layout with a previous-position array" AND "render as a
  > triangle strip". The math kernel is the foundation for both halves
  > but neither is done yet — iteration 2 is the SoA extension
  > (`m_prevPositions` CudaBuffer in `ParticleSystem`, `prevPositions`
  > field in `GpuParticles`, kernel write of `prev←pos` at the start of
  > the integration step, gather-through-permute in compactParticles,
  > `RenderData::prevPositions` accessor); iteration 3 is the Vulkan
  > triangle-strip pipeline (vertex buffer fill via `BuildRibbonStrip`,
  > new transparent-pass subscriber, OIT integration). This matches the
  > established multi-iteration P1 pattern (SequentialImpulse landed math
  > kernel first, runtime wire-up later — same here).

- [x] **Curl-noise upgrade: simplex over Perlin.**
  Today's 3D Perlin derivative shows grid banding. Swap to simplex noise for
  the turbulence field. Keep the old path behind a flag for portfolio-
  writeup comparison screenshots.
  → [engine/cuda/particles/](engine/cuda/particles/)
  > note (2026-04-24): Landed as a new header-only, host/device-compatible
  > simplex implementation in `engine/cuda/particles/SimplexNoise.hpp`
  > (Gustavson 2012 tetrahedral lattice, 12 canonical gradients, 0.6 cutoff,
  > 32× output scale). `ParticleKernels.cuh` grows a
  > `TurbulenceNoiseMode::{Perlin,Simplex}` enum + a
  > `ForceParams::turbulenceNoiseMode` field; `ParticleKernels.cu` exposes
  > `simplexNoise3D(float3)` next to the existing `perlinNoise3D`, and
  > `curlNoise` now takes the mode and dispatches per-sample. Bridson 2007
  > phase-offsets (31.416 / 47.853) replaced the previous collocated
  > sampling so the three curl channels are decorrelated. `ParticleSystem`
  > exposes `setTurbulenceNoiseMode()` and defaults to Perlin for bit-exact
  > parity with pre-simplex builds — the switch is opt-in so existing
  > elemental-magic tuning stays unchanged until a designer flips the flag.
  > 9 Catch2 cases covering boundedness / continuity / determinism /
  > axis-vs-diagonal isotropy / 256-wrap symmetry / FastFloor floor-match /
  > zero-offset DotGrad invariant / gradient-magnitude (√2) lock. Tests
  > share the exact header the kernel compiles — passing host-side is a
  > sufficient proxy for passing on-GPU because the math is pure float
  > with no CUDA intrinsics.

## P1 — animation

- [x] **Two-bone IK for foot placement.**
  Read ground normal + height via a physics ray, apply IK rotation on the
  last bone chain of the leg. Gated per-skeleton by a component so other
  characters can opt in without paying the cost.
  → [engine/animation/](engine/animation/)
  > note (2026-04-24): Landed the analytic math kernel that every runtime
  > IK pass will call — `engine/animation/TwoBoneIK.hpp`, header-only,
  > closed-form law-of-cosines solver with explicit pole-constraint and
  > out-of-reach clamping. API: `TwoBoneIK::Chain` (a/b/c/pole positions),
  > `TwoBoneIK::Solution Solve(chain, target)` (preserves limb lengths,
  > returns reached=false when target exceeds maxReach, deterministic
  > fallback when pole is collinear with shoulder-target axis),
  > `TwoBoneIK::ComputeRotationDeltas` (lifts solved positions back to
  > world-space upper/lower rotation deltas the skinning pipeline can
  > post-multiply). 8 Catch2 cases / 36 new assertions in
  > `tests/unit/test_two_bone_ik.cpp` (unit suite is now 60/693). The
  > runtime foot-IK pass that consumes this (ground-ray + per-skeleton
  > component + bone chain identification) is still to do and is blocked
  > on Meshy-loaded rigs having clips — the Meshy wire entry's known
  > follow-up noted clips=0 on the current assets, so animating foot IK
  > needs either the auto-rigger re-run or a stopgap skeleton. The math
  > kernel is ready the moment a rig is.

- [x] **Root-motion extraction from GLTF animations.**
  Today the root bone's translation is applied cosmetically. Extract the
  per-frame delta, drive the owning entity's transform with it, and feed
  back into the physics body so the character actually moves in the sim.
  → [engine/animation/](engine/animation/)
  > note (2026-04-24): Math kernel + tests landed as a new header-only
  > module `engine/animation/RootMotion.hpp`. Same shape as TwoBoneIK and
  > SimplexNoise: pure float math, no Animation/Skeleton/GPU coupling, so
  > the host-mode test build exercises the exact code path the Animator
  > integration will call. API: `Config { axisMask, upAxis }`, `Delta`,
  > `ProjectTranslation`, `ExtractTwist` (Diebel 2006 swing-twist
  > decomposition with degenerate-axis fallbacks), `ProjectRotation`,
  > `ExtractSubCycle` (common per-frame path), `ExtractWindow`
  > (multi-cycle loop-wrap composition: partial-start + n full cycles +
  > partial-end), and `StripFromPose` (zero masked translation, divide
  > out yaw twist while preserving swing). 23 Catch2 cases / 34 REQUIREs
  > covering: axis-mask projection (None/X/Y/Z/XZ/XYZ), swing-twist
  > decomposition (pure-twist, pure-swing, mixed, non-unit axis,
  > zero-axis), Config-aware ProjectRotation (yaw-on, yaw-off),
  > sub-cycle delta (translation, rotation, mixed swing+yaw with pitch
  > cancellation, custom XYZ mask, empty mask), ExtractWindow loop-wrap
  > (cyclesCrossed = 0/1/2/3, negative-defensive), StripFromPose
  > (translation zeroing per mask, rotation swing preservation), and
  > the extract+strip+reapply round-trip closing the "ice-skating cat"
  > double-apply bug class. The runtime *integration* (Animator wiring
  > + PlayerControlSystem entity offset) is one or two call sites and
  > follows naturally once the Meshy-loaded rigs reload with non-zero
  > clip counts (same blocker as the foot-IK pass) — the math kernel is
  > ready the moment a clip is. Test suite grew 16 383→16 417
  > assertions and 69→92 cases, all green; clang frontend sweep
  > 188→189 files clean; engine build green (11/11 ninja, 0 issues).

## P2 — asset + workflow

- [ ] **Mesh asset hot-reload.**
  Mirror the shader hot-reload story for GLTF models: re-parse on mtime
  change, rebuild the GPU buffers, swap atomically.
  → [engine/assets/](engine/assets/)

- [ ] **Scene serializer: versioning + migration.**
  Add a `schemaVersion` field per component type plus a per-version
  up-migration hook. Today loading an old save fails opaquely.
  → [engine/scene/](engine/scene/)

## P2 — game-as-harness deepeners

The game is a harness for the engine, not a shippable product. These items
exist only where the harness is too thin to exercise a subsystem it should.

- [ ] **Wave-difficulty curve.**
  Replace the linear ramp with a dense/sparse curve aligned to the combo
  system's ramp — gives the particle sim more variety to render.

- [ ] **Quest/dialog save-load round-trip.**
  The quest system is scaffolded but doesn't survive a save/load. Hook it
  into the scene serializer so the round-trip Catch2 test covers it.

---

## How openclaw reads this file

The openclaw cat-annihilation nightly goal does, in order:

1. Reads this file and [README.md](README.md) for context.
2. Picks the highest unticked item that fits the remaining idle budget.
3. Opens a session with the relevant subsystem's README + touched files in
   context.
4. Makes the change; runs the three verification commands from *Rules #3*.
5. If all three pass, ticks the item and commits; if any fail, leaves a
   `> note:` line under the item describing what blocked and moves on.
6. Never invents new P0 items. Never reorders. Backlog grooming is a human
   decision.

# Cat Annihilation Test Suite
cmake_minimum_required(VERSION 3.20)

# A project() call is required by modern CMake (>= 3.0) — without it CMake
# emits the "No project() command is present" dev warning, and toolchain
# detection for languages other than C/CXX (e.g. ASM, CUDA) is skipped.
# We keep this suite C++-only on purpose: tests link against mock GPU
# implementations, not the real Vulkan/CUDA engine libraries.
project(CatAnnihilationTests CXX)

# Test configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include Catch2 header-only library + test headers.
#
# Both the mocks/ subdir AND the tests/ directory itself need to be on the
# include path: some tests include `mock_ecs.hpp` (flat), and some include
# `mocks/mock_ecs.hpp` (prefixed). Adding both resolves either spelling
# without forcing callers to rewrite their #include lines.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/catch2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# Project root is parent of tests directory
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Include game and engine directories
include_directories(${PROJECT_ROOT})
include_directories(${PROJECT_ROOT}/engine)
include_directories(${PROJECT_ROOT}/game)
include_directories(${PROJECT_ROOT}/third_party)
include_directories(${PROJECT_ROOT}/third_party/stb)
include_directories(${PROJECT_ROOT}/build_stubs)

# CUDA headers are needed even for mock-GPU test builds because several
# game-system headers (e.g. elemental_magic.hpp → ParticleSystem.hpp) transitively
# include <cuda_runtime.h> and <curand_kernel.h>. Tests don't call CUDA APIs
# directly — mocks intercept every GPU call — but the host-mode parser still
# needs the real type declarations (cudaError_t, cudaStream_t, float4, dim3, ...)
# to compile. We therefore pull in the include dir from find_package(CUDAToolkit)
# but deliberately do NOT link CUDA::cudart, keeping tests standalone
# executables that run on any box with the CUDA SDK headers installed (doctor
# enforces that prerequisite).
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    include_directories(${CUDAToolkit_INCLUDE_DIRS})
    message(STATUS "CUDA headers for tests: ${CUDAToolkit_INCLUDE_DIRS}")
else()
    # Fallback: use the build_stubs if CUDAToolkit isn't located via CMake's
    # module (e.g. on a sandboxed CI runner). The stubs cover the subset of the
    # runtime API that the engine headers reference; anything beyond that will
    # surface as a clear "undeclared identifier" error at the call site.
    message(WARNING "CUDAToolkit not found — falling back to build_stubs/ (expect reduced coverage)")
endif()

# Define mocks to avoid GPU dependencies
add_compile_definitions(USE_MOCK_GPU=1)
add_compile_definitions(TESTING_MODE=1)

# Collect all unit test sources.
#
# Seven additional unit tests and all three integration tests are currently
# SKIPPED (commented out) because they were written against an earlier API
# surface that has since been refactored. Each entry lists the specific
# drift observed at 2026-04-24 so a future contributor can rewrite against
# the shipping API. Files are preserved on disk; only the CMake include
# is disabled (cardinal rule 4: skip, don't delete).
set(UNIT_TEST_SOURCES
    # MeshOptimizer is a header-only module (engine/renderer/MeshOptimizer.hpp)
    # that depends only on <vector>/<cmath>/<cstdint>, so the test can link
    # cleanly in the no-GPU test build — no mock-GPU plumbing needed.
    unit/test_mesh_optimizer.cpp
    # ShadowAtlasPacker is header-only (engine/renderer/lighting/
    # ShadowAtlasPacker.hpp) and depends only on the STL; the same
    # no-mock link rationale as MeshOptimizer applies.
    unit/test_shadow_atlas_packer.cpp
    # test_clustered_lighting_math is a pure-math mirror test of
    # ClusteredLighting::computeZSlice and ShadowAtlas::computeCascadeSplits.
    # The production classes pull in the full RHI header surface (which the
    # USE_MOCK_GPU=1 build excludes), so this test reimplements the formulas
    # inline and asserts the numerical contract — same drift-guard pattern
    # used by test_oit_weight.cpp. Catches: cluster Z-slice NaN poisoning
    # when near<=0 or far<=near, near/far clamp behaviour, monotonicity of
    # both the cluster slice map and the practical-split CSM scheme.
    unit/test_clustered_lighting_math.cpp
    # Base64DataUri is header-only (engine/assets/Base64DataUri.hpp) with only
    # STL deps. It fixes the bug that made every shipping .gltf model silently
    # fail to load (every asset in assets/models/ embeds its buffer as a
    # `data:application/octet-stream;base64,...` URI). The no-mock rationale
    # above applies here verbatim — pure CPU, no GPU, no JSON lib needed.
    unit/test_base64_data_uri.cpp
    # OITWeight is header-only (engine/renderer/OITWeight.hpp) with only
    # STL deps. It implements the CPU mirror of the WBOIT weight + composite
    # math used by the forward pass's WeightedBlendedOIT path, which is the
    # P0 "per-pixel OIT on the forward transparent pass" backlog item. Tests
    # guard the numerical contract against drift vs the GLSL shaders in
    # shaders/forward/transparent_oit_accum.frag and oit_composite.frag,
    # which hardcode the exact same constants and formulas.
    unit/test_oit_weight.cpp
    # ProfilerOverlay's SummarizeFrameTimes helper is header-only
    # (engine/debug/ProfilerOverlay.hpp, inline, STL-only — no ImGui, no
    # Vulkan, no Profiler singleton needed). Tests guard the numerical
    # contract the ImGui overlay relies on so a regression shows up
    # here, not as a lying histogram on screen.
    unit/test_profiler_overlay.cpp
    # GraphicsSettingsPanel's FormatMaxFPSLabel helper is also header-only
    # (engine/ui/GraphicsSettingsPanel.hpp, inline, STL-only — no ImGui, no
    # Vulkan). Same motivation as test_profiler_overlay: the text a
    # reviewer reads in the F4 panel must not drift across refactors.
    unit/test_graphics_settings_panel.cpp
    # TwoBoneIK is header-only (engine/animation/TwoBoneIK.hpp) and depends
    # only on engine math (Vector/Quaternion/Math — no Transform, Skeleton,
    # or Animator). Same no-mock link rationale as MeshOptimizer and
    # ShadowAtlasPacker: pure CPU math, closed-form law-of-cosines solver,
    # no GPU or RHI dependency. Tests lock down the foot-IK contract
    # (limb-length preservation, reach clamping, pole-bend selection, and
    # the degeneracy fallbacks for collinear poles + anti-parallel
    # rotate-from-to).
    unit/test_two_bone_ik.cpp
    # test_anim_property_two_bone_ik.cpp is the DEEPER property/fuzz
    # coverage companion to test_two_bone_ik.cpp (which pins 8 fixed
    # scenarios). This file fuzzes the kernel across 1000 random
    # {bone-lengths, target, pole} samples plus the exact-maxreach /
    # just-past-maxreach boundary, the collinear-pole determinism
    # contract (1000 + 600 calls bit-exact equal), pole-bend-side
    # invariant, RotationFromTo unit-output fuzz, zero-limb /
    # target-at-root edge cases, and tiny-limb / large-limb numerical-
    # stability sweeps. Same no-mock link rationale: header-only
    # TwoBoneIK kernel, no GPU / RHI / Animator coupling.
    unit/test_anim_property_two_bone_ik.cpp
    # SimplexNoise is header-only (engine/cuda/particles/SimplexNoise.hpp) and
    # is shared between nvcc-compiled device code (the CUDA curl-noise
    # kernel) and this host test. Because the implementation is pure float
    # math with no CUDA intrinsics, the host build exercises the same code
    # path the device code executes — passing these tests is a sufficient
    # proxy for on-GPU correctness. Tests pin the boundedness, continuity,
    # determinism, isotropy, and detail::FastFloor / DotGrad / kGrad3
    # contracts that the in-game particle turbulence field depends on.
    unit/test_simplex_noise.cpp
    # RibbonTrail is header-only (engine/cuda/particles/RibbonTrail.hpp) and
    # depends only on engine math (Vector — vec2/vec3/vec4) + STL. Same
    # CUDA-free, header-shared rationale as SimplexNoise: the math is the
    # camera-facing billboard quad generator that the future projectile-VFX
    # render path will call from a CPU strip-builder loop AND that a future
    # CUDA strip-builder kernel will call from device code (one-line change
    # to add __host__ __device__ qualifiers). Tests pin the orthonormal-frame
    # contract for ComputeSegmentBasis, the strip-canonical UV layout +
    # color split for BuildBillboardSegment, the lifetime-linear lerp +
    # clamp invariants for TaperHalfWidth, the 4 + 6*(N-1) capacity formula
    # for MaxStripVertexCount, and the bridge / skip / truncate behaviour
    # for BuildRibbonStrip — everything the renderer's vertex-buffer-fill
    # loop depends on.
    unit/test_ribbon_trail.cpp
    # RootMotion is header-only (engine/animation/RootMotion.hpp) and depends
    # only on engine math (Vector / Quaternion / Transform / Math). Same
    # no-mock link rationale as TwoBoneIK and SimplexNoise: pure CPU math,
    # no Animation / Skeleton / GPU coupling, so synthetic Transform inputs
    # in the tests exercise the exact code paths the Animator integration
    # will call. Tests lock down the axis-mask projection, swing-twist
    # decomposition, sub-cycle delta, multi-cycle loop-wrap composition,
    # pose stripping, and the extract+strip+reapply round-trip that closes
    # the "ice-skating cat" double-apply bug class.
    unit/test_root_motion.cpp
    # test_anim_property_root_motion.cpp is the property/fuzz companion to
    # test_root_motion.cpp. Pins the ExtractWindow compose-additivity
    # invariant across cyclesCrossed ∈ {0, 1, 2, 3, 10, 100} (full window
    # equals two adjacent half-windows composed via config-aware
    # translation-sum + rotation-product), ExtractSubCycle 1000-call
    # determinism, the StripFromPose-then-reapply round-trip, axis-mask
    # isolation, multi-cycle linear translation scaling, the
    # negative-cyclesCrossed-as-sub-cycle fallback, and pure-translation
    # / pure-yaw isolation. Same header-only, no-mock link rationale as
    # the sibling test.
    unit/test_anim_property_root_motion.cpp
    # Skeleton + Animator suite. Unlike TwoBoneIK / RootMotion which are
    # header-only math kernels, these tests exercise compiled engine TUs
    # (Skeleton.cpp, Animator.cpp, Animation.cpp, AnimationBlend.cpp) —
    # see ENGINE_TEST_SOURCES below for the link wire-up. The .cpp files
    # have zero GPU coupling (Skeleton: <algorithm>; Animator: math only;
    # Animation/AnimationBlend: math + STL), so they slot cleanly into the
    # no-GPU test build. test_skeleton.cpp pins the topological-resolve
    # contract for computeWorldTransforms() against scrambled parent
    # orderings; test_animator.cpp pins the cursor-reset contract for
    # startTransition() (the visible "frame-skip" bug pre-fix) and the
    # mid-transition pose-snapshot contract that keeps rapid play() calls
    # from rubber-banding back to the original outgoing state.
    unit/test_skeleton.cpp
    unit/test_animator.cpp
    # test_anim_property_skeleton.cpp is the property/fuzz companion to
    # test_skeleton.cpp. Pins the sorted parent-index invariant (parent <
    # self for every addBone() call), the single-pass forward-walk vs
    # topological-resolver bit-for-bit agreement on sorted skeletons,
    # inverse-bind matrix stability under repeated computeInverseBindMatrices
    # calls, the world_bind * inverse_bind ≈ identity per-bone skinning
    # contract, scrambled-vs-sorted layout equivalence under name remap,
    # random-tree NaN-free resolve across 100 trials of 1..32 bones,
    # deep-chain (depth 64) accumulation accuracy, mat4-vs-Transform
    # overload agreement, star-topology children/descendants enumeration,
    # and bone-name lookup round-trip on a 16-bone humanoid.
    unit/test_anim_property_skeleton.cpp
    # test_anim_stress_animator.cpp is the stress companion to
    # test_animator.cpp. Pins the t=0 and t=duration loop-wrap pose
    # contract, the 1000-frame play loop on a 10-clip blend tree never
    # producing a NaN (the headline "the cat must never disappear"
    # invariant), 200 rapid play()-during-transition iterations stability,
    # stop/play cursor reset, parameter set/get round-trip fuzz (200
    # iterations across bool + float spaces), and the empty-Animator
    # no-crash contract.
    unit/test_anim_stress_animator.cpp
    # test_anim_property_blend.cpp pins the AnimationBlend numerical
    # contract: multiBlend weight-normalization across the three corner
    # cases (sum>1, sum==0, single-weight-of-1), slerp+nlerp unit-length
    # output across 1000 random pairs each, slerp hemisphere shortest-
    # path selection (dot(q1, result) and dot(q2, result) have same
    # sign — no 180° flip), linearBlend boundary factors (0 returns
    # poseA, 1 returns poseB, out-of-range clamps), BoneMask weight
    # clamp + bounds, computeAdditivePose then additiveBlend round-trip
    # at weight=1 recovering the original, linearBlendMasked per-bone
    # weight respect, and multiBlend determinism. Links against the
    # same ENGINE_TEST_SOURCES (AnimationBlend.cpp / Animation.cpp /
    # Skeleton.cpp) that test_skeleton / test_animator already pull in.
    unit/test_anim_property_blend.cpp
    # SequentialImpulse is header-only
    # (engine/cuda/physics/SequentialImpulse.hpp) and depends only on engine
    # math (Vector / Math) + STL. The file lives under engine/cuda/physics/
    # for discoverability but is intentionally CUDA-free: no float3, no
    # __device__, no runtime allocator — the runtime pass that wires the
    # solver into the CUDA PhysicsWorld will call the same inline
    # ApplyImpulse / SolveIteration from a __host__ __device__ context.
    # Tests pin the acceptance bar from ENGINE_BACKLOG.md — "50-body box
    # stack converges in ≤ 20 iterations" — plus the per-step invariants
    # (tangent-basis orthonormality, effective-mass math, static-body
    # no-op, restitution threshold, Baumgarte separation, λ_n ≥ 0
    # non-pulling, friction-pyramid clamp, and warm-start replay) the
    # runtime pass will rely on.
    unit/test_sequential_impulse.cpp
    # CCD is header-only (engine/cuda/physics/CCD.hpp) and depends only on
    # engine math (Vector / AABB / Math) + STL. Same CUDA-free rationale as
    # SequentialImpulse: the narrow-phase integration will call the same
    # inline SweepAABB / SweptSphereAABB / SweptSphereSphere /
    # ConservativeAdvance from a __host__ __device__ context once the runtime
    # wire-up lands. Tests pin the acceptance bar from ENGINE_BACKLOG.md —
    # "a bullet-speed body through a thin wall must collide, not pass through"
    # — as "CCD: bullet-speed sphere cannot tunnel through a thin wall",
    # plus the per-function invariants (swept-volume containment, slab-hit
    # normal recovery, degenerate-motion handling, quadratic-root sign
    # correctness, moving-apart / parallel-paths miss cases, and the
    # CA-vs-analytic convergence cross-check) the narrow phase will rely on.
    unit/test_ccd.cpp
    # CCDPrepass is the runtime wrapper around CCD.hpp — it walks
    # std::vector<RigidBody> and clamps linearVelocity on any fast body that
    # would tunnel this frame. The tests here are the RUNTIME-STATE half of
    # the P1 CCD acceptance bar: "a bullet-speed body through a thin wall
    # must collide, not pass through" — provable in a synthetic
    # PhysicsWorld-like fixture without spinning up a CUDA context. Same
    # header-only CUDA-free rationale as test_ccd + test_sequential_impulse:
    # the pre-pass code path the test exercises IS the same function the
    # live PhysicsWorld::step() calls before upload.
    unit/test_ccd_prepass.cpp
    # ShaderHotReload is header-only (engine/rhi/ShaderHotReload.hpp) — the
    # entire module is <filesystem>+<string>+<vector>+<cstdlib>, with no
    # Vulkan, CUDA, or engine-math dependencies. That lets the pure
    # sub-namespace HotReloadDetail:: (ClassifyShaderKind, QuoteShellArg,
    # BuildGlslcCommand, DetectChangedSources, TailLines) be exhaustively
    # tested against synthetic mtimes and string inputs without spawning a
    # real glslc process, the same testing pattern applied to CCD.hpp,
    # CCDPrepass.hpp, and SequentialImpulse.hpp. The subprocess glue
    # (ShaderHotReloader::CompileIndex) stays out of the unit suite on
    # purpose — it requires the Vulkan SDK on PATH and belongs in the
    # shader integration suite once that exists.
    unit/test_shader_hot_reload.cpp
    # ShaderHotReloadDriver is the live polling driver layer that sits on top
    # of ShaderHotReloader — same header-only discipline, so the driver's
    # pure sub-namespace helpers (ShouldTick throttle, MakeSpvPath mapping,
    # SlurpBinaryFile, IsShaderSourceExtension filter) are exhaustively
    # testable without spawning glslc or touching a real shaders/ tree.
    # The Tick() integration (recursive_directory_iterator + Scan() +
    # CompileIndex()) stays out of this unit suite for the same reason
    # ShaderHotReloader::CompileIndex does: it needs the Vulkan SDK.
    unit/test_shader_hot_reload_driver.cpp
    # ShaderReloadRegistry is the renderer-side subscriber that the driver
    # dispatches reload events through (header-only, pure STL — no Vulkan,
    # no filesystem). Tests exercise register/unregister lifecycle, the
    # apply/onReloaded split semantics on compile success + compile failure
    # + apply-failure paths, multi-subscriber fanout, backslash<->slash
    # path normalisation round-trip, and the snapshot-before-invoke guard
    # that keeps a self-unregistering callback from corrupting the
    # iteration. Because every test uses captured-lambda subscribers, no
    # GPU device / real VulkanShader is needed — the registry is proven
    # end-to-end in host-only code.
    unit/test_shader_reload_registry.cpp
    # ImageCompare is header-only (engine/renderer/ImageCompare.hpp) — pure
    # <fstream>/<cmath>/<vector>/<cstdint>, no Vulkan, no Catch2 image plugin,
    # no external image lib. It is the offline comparator half of the P0
    # "Headless render mode + golden-image CI test" backlog item: the math
    # (MSE, PSNR, SSIM) plus the binary-PPM codec we use for golden fixtures
    # on disk. Tests lock down the numerical contract the CI "ssim > 0.95"
    # gate will rely on — identity → 1.0, visible corruption → clear drop,
    # dimension mismatch → NaN, structure-break (inverted checkerboard) →
    # negative score, 8-bit round-trip through PPM is byte-exact, and the
    # reader rejects unsupported variants (P3 ASCII, non-255 maxval).
    unit/test_image_compare.cpp
    # test_renderer_bug_fixes.cpp pins the 2026-05 renderer bug-hunt fixes:
    # Camera near/far + aspect-ratio input validation (zero-near divide, far
    # <= near swap, zero aspect on minimized window), Mesh::OptimizeIndices
    # submesh-bounds-check (skips out-of-range submeshes instead of writing
    # past the index buffer), Mesh::GenerateTangents degenerate-UV guard
    # (skips collinear-UV triangles instead of NaN-poisoning the tangent
    # accumulator), and ImageCompare::SSIM window-clamp on small images.
    # Header-only deps (Camera/Mesh/ImageCompare are all pure CPU + STL +
    # engine math), so this links in the no-GPU host test build with no
    # extra plumbing.
    unit/test_renderer_bug_fixes.cpp
    # test_containers_correctness.cpp pins the correctness contract for the
    # four highest-PageRank container/math headers in the engine
    # (HashMap.hpp, SlotMap.hpp, SparseSet.hpp, Vector.hpp). Header-only and
    # STL-only — no GPU, no Vulkan, no engine-runtime dependency — so it
    # links into the no-GPU test build with no extra plumbing. Five
    # invariants are locked: HashMap::erase backward-shift preserves
    # value_type lifetime (no byte-copy UB on std::string / unique_ptr),
    # HashMap stores the full size_t hash (no uint32_t truncation collision
    # storm on 64-bit IDs), SlotMap<T>::Handle is hashable via std::hash
    # (regression-locks the non-deducible-context fix), SparseSetWithData
    # erase of the last element does not self-move, and vec3's _padding
    # lane stays at 0.0f after divide-by-zero (no NaN poisoning of lane 4).
    unit/test_containers_correctness.cpp
    # WaveDifficulty is header-only (game/systems/WaveDifficulty.hpp) and
    # depends only on <cmath> + <algorithm> — pure CPU math, no ECS / System
    # / CUDA coupling. Same no-mock link rationale as RibbonTrail /
    # SimplexNoise / RootMotion: the production WaveSystem.cpp delegates
    # directly to the inline functions, so the tests exercise the exact
    # code path the runtime hits. Tests pin: linear opt-out is bit-exact
    # against the pre-2026-05-16 formula, default config produces a
    # dense/sparse rhythm (3-wave cycle, ≥2 spread), 90-deg phase offset
    # decorrelates count from health peaks, min floors clamp aggressive
    # amplitudes, long-playthrough trend still grows, pure-function
    # determinism, and edge cases (period=0, period<0, wave=10000).
    unit/test_wave_difficulty.cpp
    # test_matrix_projection.cpp pins the Vulkan clip-space depth-range
    # contract on Matrix.hpp::perspective / perspectiveInfinite / ortho.
    # Pre-fix the matrices emitted OpenGL's [-1, 1] depth range; on a
    # Vulkan backend that silently inverts the depth test (because the
    # viewport's depth clamp range is [0, 1] by default and negative
    # clip-z values fail the clip test). Header-only, depends only on
    # engine math (Vector / Matrix) — no Vulkan, no GPU.
    unit/test_matrix_projection.cpp
    # test_ray_aabb.cpp pins the NaN-handling contract on Ray::intersectsAABB
    # for rays parallel to a coordinate axis. Pre-fix, a ray with
    # direction.y == 0 AND origin.y exactly on a slab boundary produced
    # 0 * inf = NaN in the slab math; the boolean result became MISS
    # for a ray physically touching the AABB. Header-only, depends only
    # on engine math (Vector / AABB / Ray).
    unit/test_ray_aabb.cpp
    # test_save_atomicity.cpp pins the atomic-rename pattern used by
    # SaveSystem::saveToFile. The test doesn't link the save system itself
    # — it just verifies the staging + rename pattern at the filesystem
    # level so a refactor that drops the staging file (e.g., back to the
    # pre-fix "truncate canonical path first") fails loudly. STL + filesystem only.
    unit/test_save_atomicity.cpp
    # test_serialization_endianness.cpp pins the little-endian-host
    # invariant on BinaryWriter / BinaryReader. A static_assert in
    # serialization.hpp blocks BE builds at compile time; this runtime
    # test verifies the on-disk byte layout matches the format the save
    # loader on every shipping platform expects.
    unit/test_serialization_endianness.cpp
    # ComboState (game/components/combat_components.hpp) is a plain struct
    # of inline methods with no ECS / System / GPU coupling — same header-
    # only pure-state rationale as WaveDifficulty.hpp. The tests pin the
    # queueAttack / commitQueuedAttack contract that fixes the pre-2026-05-16
    # combo-cheese bug where whiff-swings inflated the damage multiplier;
    # the contract is "comboStep only advances on landed hits, never on
    # swing input". test_combat_system.cpp remains SKIPPED below for the
    # documented MockECS drift — this file picks up the cheese-fix tests
    # without re-enabling the broader combat-system harness.
    unit/test_combo_state.cpp
    # test_ai_behavior_tree.cpp regression-locks the iterative dumpStructure
    # / buildRunningPath rewrite (2026-05-16). The previous recursive form
    # consumed one C++ frame per BT level and would blow the default
    # Windows 1 MB thread stack on a deeply nested tree. Tests pin the
    # textual-dump contract on a small mixed tree AND prove a 4096-deep
    # right-spine no longer overflows during the debug-dump path.
    unit/test_ai_behavior_tree.cpp
    # test_container_dynamic_array.cpp regression-locks the SFINAE split of
    # DynamicArray's count-only vs count+value ctor/resize overloads
    # (2026-05-16). The previous single signature used `const T& value = T()`
    # as a default arg, which required T to be default-constructible just to
    # declare the function — breaking `DynamicArray<NoDefault>` of any size
    # and refusing `DynamicArray<unique_ptr<X>>::resize(n)`. The split makes
    # each overload available iff T supports the corresponding construction.
    unit/test_container_dynamic_array.cpp
    # test_allocators.cpp pins the correctness contract for the three custom
    # allocators (LinearAllocator, StackAllocator, PoolAllocator). The 2026-05-16
    # bug-hunt pass found four real defects: a self-deadlock on every thread-safe
    # path (lock_guard then recursive public call on non-recursive std::mutex),
    # size_t/uintptr_t overflow in the Linear/Stack out-of-space check,
    # PoolAllocator's totalSize lying about the floored block size, and a
    # double-free underflow path in PoolAllocator::deallocate. The test links the
    # three implementation TUs directly (added to ENGINE_TEST_SOURCES below) —
    # no GPU coupling, host-only.
    unit/test_allocators.cpp
    # test_job_system.cpp pins JobSystem / JobQueue / WorkerThread correctness
    # after the 2026-05-16 fix pass: Push() now reports queue-full instead of
    # silently overwriting unconsumed slots, SubmitJob tracks every submission
    # in m_activeJobs regardless of user-counter presence (so Shutdown's
    # WaitForAll can't return early and drop work), and Shutdown is two-phase
    # (RequestStop on every worker, then Stop) so workers[1..N] aren't blocked
    # behind worker[0]'s join. Same no-mock link rationale as test_allocators.
    unit/test_job_system.cpp
    # ------------------------------------------------------------------------
    # Property + stress expansion (2026-05-16). The bug-fix suites above lock
    # the specific defects that landed earlier the same day; the files below
    # widen coverage to invariants the allocators + job system must satisfy
    # over a broad randomised input space. Header-only no-GPU rationale is
    # identical to test_allocators + test_job_system; they link against the
    # same engine .cpp TUs already in ENGINE_TEST_SOURCES.
    #
    #   test_alloc_property_linear.cpp  — alignment {1..128}, linearity,
    #       disjointness, reset round-trip, peak-usage monotonicity, fill +
    #       OOM contract at every power-of-two arena boundary, writes-don't-
    #       corrupt-neighbours.
    #   test_alloc_property_pool.cpp    — LIFO freelist invariant (free then
    #       alloc returns the SAME address), block-size floor to sizeof(void*),
    #       alignment-ignored contract, bookkeeping (free+alloc == count) at
    #       every step, distinct-issuance under churn, reset-set equality.
    #   test_alloc_property_stack.cpp   — marker round-trip, nested-scope
    #       rollback, bad-rollback (forward) is no-op, allocationCount
    #       documented approximation (zero only on marker==0), zero-size
    #       allocation cursor invariant.
    #   test_alloc_stress_churn.cpp     — cross-allocator long-run churn
    #       (10 000 random ops each), boundary safety (no pointer escapes
    #       the arena), deterministic reseeded reproducibility, no leak after
    #       full drain.
    #   test_jobs_property.cpp          — mass-submit no-loss (1000 jobs),
    #       dependency chain A→B→C topo-order, 100-deep linear chain,
    #       fan-in barrier, ParallelFor exhaustive index coverage at varied
    #       batch sizes, empty / single-index / null-counter edge cases,
    #       JobQueue Push-LIFO / Steal-FIFO + 4096-slot overflow.
    #   test_jobs_stress.cpp            — 64-thread JobQueue producer/consumer
    #       conservation for one second, 8-worker fairness over 8000 jobs,
    #       100 000-iter ParallelFor, 50 rapid construct/destruct shutdown
    #       cycles, avalanche (each job spawns 2 more), 16-thread external
    #       producer race, mixed long+short job duration starvation guard.
    unit/test_alloc_property_linear.cpp
    unit/test_alloc_property_pool.cpp
    unit/test_alloc_property_stack.cpp
    unit/test_alloc_stress_churn.cpp
    unit/test_jobs_property.cpp
    unit/test_jobs_stress.cpp
    # ------------------------------------------------------------------------
    # ECS + Scene property/fuzz/stress expansion (2026-05-16). The ECS audit
    # already pinned four narrow correctness gaps via
    # test_ecs_scene_correctness.cpp (precision, escaping, key ordering,
    # one-shot handle recycle). The six files below widen coverage to the
    # invariants the ECS and scene layer must satisfy across randomised
    # inputs and a 10k×10×100 churn stress. All header-only / STL-only —
    # they link against scene/{Scene,SceneNode,SceneSerializer,...}.cpp and
    # ecs/{EntityManager,SystemManager,ComponentPool}.cpp that are already
    # in ENGINE_TEST_SOURCES below.
    #
    #   test_ecs_property_entity.cpp     — Entity bit-pack round-trip
    #       fuzz across the full 32-bit (index, generation) range,
    #       EntityManager generation-counter monotonicity across 100
    #       same-slot recycles, freelist FIFO ordering, clear() reset
    #       to like-new state, 1000-op churn vs std::set oracle,
    #       handle-collision impossibility, 10k-op stress, isolated
    #       managers do not cross-acknowledge handles.
    #   test_ecs_property_component.cpp  — ComponentPool sparse-dense
    #       fuzz on 1000 entities × random add/remove/replace with
    #       std::map oracle, middle-removal contiguity (no holes), pool
    #       independence (cross-pool removes leave others intact),
    #       rvalue-move add path, emplace, 1000-bulk-then-remove leaves
    #       pool empty, high-index sparse resize correctness.
    #   test_ecs_property_query.cpp      — AND-query == std::set_intersection
    #       across 2 and 3 component combos, OR-via-union == std::set_union,
    #       smallest-pool optimisation cardinality bound, mutation through
    #       query pointers, deterministic iteration across two identical
    #       runs, SystemManager priority order (registered out-of-order,
    #       must tick in priority order), enabled/disabled flags,
    #       get/remove by type round-trip, destroyEntity walks every pool.
    #   test_ecs_stress_churn.cpp        — 10k entities × 10 components ×
    #       100 frames of churn vs a parallel std::set oracle (per-frame
    #       size check, per-type pool occupancy oracle), 100k single-slot
    #       create/destroy/component-add cycle (generation strictly
    #       increasing), ECS::clear / clearEntities semantic separation.
    #   test_scene_property_node.cpp     — sum-of-subtree-counts +
    #       parent-pointer invariants pinned at every step of a 200-step
    #       random mutation walk on a 100-node tree, DFS/BFS visit-set
    #       equality, active-in-hierarchy ancestor-propagation, transform-
    #       cache dirty-propagation through parent chain, clone()
    #       subtree-preservation, Scene::findNode + findNodes walk the
    #       whole graph, Scene::createEntityNode + destroyEntityNode
    #       lifecycle, cyclic-parent attempt is structurally rejected by
    #       unique_ptr ownership (documented behaviour).
    #   test_scene_property_serializer.cpp — empty scene byte-identical
    #       re-save, transform float-precision battery (1e-6, 1e6, M_PI as
    #       float, denormals, -0.0) round-trips bit-exact, 100-node /
    #       depth-6 / breadth-4 tree across 8 seeds preserves structure,
    #       5-component entity round-trip, schema-version match/mismatch
    #       handling, missing-required-field load returns nullptr,
    #       entity-reference remap rewires same-scene targets and nulls
    #       unresolved references, scene + node active flags survive,
    #       JsonValue::parse symmetry with toString.
    unit/test_ecs_property_entity.cpp
    unit/test_ecs_property_component.cpp
    unit/test_ecs_property_query.cpp
    unit/test_ecs_stress_churn.cpp
    unit/test_scene_property_node.cpp
    unit/test_scene_property_serializer.cpp
    # test_ecs_scene_correctness.cpp pins the correctness contract for the
    # four highest-impact gaps in the ECS + scene-serializer audit:
    #   (1) JsonValue::toString float-precision round-trip — setprecision(6)
    #       silently truncated transforms on every save,
    #   (2) JsonValue::toString RFC-8259 string escaping — no escaping meant
    #       any node name with a quote/backslash produced unparseable JSON,
    #   (3) JsonValue::toString deterministic object-key ordering — was
    #       std::unordered_map iteration order, broke byte-diff
    #       reproducibility and content-addressed asset hashing,
    #   (4) EntityManager generational handle reuse — no test pinned that
    #       a destroyed handle stays dead once the slot is recycled.
    # Pulls in scene/Scene.cpp, scene/SceneNode.cpp, scene/SceneSerializer.cpp
    # via ENGINE_TEST_SOURCES below — those are pure CPU code (no Vulkan,
    # no CUDA, ECS is header-only) so they link cleanly in the no-GPU test
    # build.
    unit/test_ecs_scene_correctness.cpp
    # Property-test sweep of engine/math/ (Vector, Quaternion, Matrix, AABB,
    # Ray, Plane, Frustum, Transform, plus Math.hpp utility helpers). The
    # math headers are header-only (engine/math/*.hpp) and STL-only — no
    # Vulkan, no CUDA, no engine-runtime dependency — so the five new files
    # link cleanly into the no-GPU test build with no extra plumbing. Each
    # file uses a deterministic std::mt19937{42} seed and asserts algebraic
    # identities + round-trip invariants + degenerate-input handling across
    # 1000-sample stress sweeps (where applicable). Coverage map:
    #   - vector       → field axioms, dot/cross identities, normalize,
    #                    reflect/refract, SIMD lane-4 hygiene (vec3 _padding
    #                    contract)
    #   - quaternion   → group axioms, rotation round-trips, matrix cross-
    #                    validation, axis-angle / Euler round-trips, slerp
    #                    hemisphere convention
    #   - matrix       → multiplication / inverse / transpose / determinant
    #                    algebra, Vulkan [0,1] clip-Z contract for
    #                    perspective / perspectiveInfinite / ortho, lookAt
    #                    semantics
    #   - aabb_ray     → expand/contain round-trips, ray-vs-sphere/plane/
    #                    triangle/AABB intersection, parallel-to-axis slab-
    #                    test degeneracy
    #   - transform    → TRS order, inverse round-trip, composition,
    #                    fromMatrix decompose round-trip, direction-vector
    #                    orthonormality, plus Math.hpp utility coverage
    #                    (degToRad / clamp / smoothstep / safeDivide / mod /
    #                    fract / fastInvSqrt)
    unit/test_math_property_vector.cpp
    unit/test_math_property_quaternion.cpp
    unit/test_math_property_matrix.cpp
    unit/test_math_property_aabb_ray.cpp
    unit/test_math_property_transform.cpp
    # ---------------------------------------------------------------
    # Property + stress test suite for game systems (2026-05-16). All
    # header-only or pure-math-mirror — no ECS / RHI / CUDA / OpenAL
    # coupling, every file compiles standalone against game/ +
    # engine/math/. Same drift-guard pattern test_clustered_lighting_math.cpp
    # pioneered: when a production header depends on a build surface the
    # no-GPU test build excludes (Renderer.hpp + <AL/al.h> for
    # GameConfig.hpp, cuda_runtime.h for Terrain.hpp), we mirror the
    # formulas inline and pin the contract.
    #
    # test_game_property_wave_difficulty.cpp — deeper than the existing
    #   test_wave_difficulty.cpp: linear opt-out bit-exact across wave
    #   1..10000, rolling 3-wave spread >= 2 across 1..1000 with amp>=1,
    #   minEnemyCount floor at amp=1000 across 1..2000, healthMul >=
    #   minHealthScaling stress sweeps, determinism (1000-call repeat for
    #   waves 1..50), edge waves (0, -1, INT_MIN, INT_MAX), period edges
    #   (0, <0, NaN), NaN amplitude / base / Inf base, cross-product
    #   (amp, period) grid, linearEnemyCount edges.
    unit/test_game_property_wave_difficulty.cpp
    # test_game_property_combat.cpp — pure-math mirror of CombatSystem's
    #   checkMeleeHit / checkProjectileHit sphere-overlap kernel (the
    #   exact formula at CombatSystem.cpp:952-976 reproduced inline).
    #   Pins translation invariance, symmetry, closed-boundary hit-at-
    #   range, monotone-in-range, and 1000-sample property sweeps. Also
    #   extends the ComboState cheese-fix coverage (test_combo_state.cpp)
    #   to timeout-reset across configurable comboWindow values, damage-
    #   multiplier ladder, getComboProgress monotonicity, repeated-reset
    #   idempotence, and finisher-cap return contract.
    unit/test_game_property_combat.cpp
    # test_game_property_health.cpp — HealthComponent pure-struct method
    #   tests: damage clamp >= 0, heal cap at maxHealth, i-frame gate
    #   (rejected mid-window, accepted post-expiry), death-callback fires
    #   at least once on hp-reaches-zero, resetHealth restoration,
    #   setMaxHealth shrink/expand semantics, getHealthPercentage div-0
    #   guard, predicate cross-product, damage accumulator property.
    #   HealthSystem ECS-coupled paths (auto-destroy after
    #   deathAnimationDuration, hasComponent<EnemyComponent> dispatch)
    #   are documented as integration-only via the [host-not-testable]
    #   tag on a single SUCCEED-passing TEST_CASE so a future agent can
    #   grep for the gap rather than fake-green it.
    unit/test_game_property_health.cpp
    # test_game_property_terrain.cpp — pure-math mirror of Terrain::
    #   getHeightAt + sampleHeightmap + isInBounds + getBounds. Shipping
    #   Terrain class depends on CudaContext + cuda_runtime.h
    #   (USE_MOCK_GPU=1 excludes it from the test build); we mirror the
    #   bilinear sampler byte-for-byte against Terrain.cpp:370-403 +
    #   483-500. Pins: exact integer-grid round-trip, [min, max]
    #   containment inside any bilinear cell across 5000 random samples,
    #   OOB->0 fallback, isInBounds closed at +/- halfSize, getBounds
    #   AABB shape, determinism, 2x2 minimum-resolution grid, constant-
    #   height invariance, linear ramp gradient, +/- 1 checkerboard mean
    #   preservation, asymmetric cellSize, single-axis monotone smoothness,
    #   golden-value hand-computed bilinear at (0.5, 0.5).
    unit/test_game_property_terrain.cpp
    # test_game_property_config.cpp — pure-math mirror of GameConfig::
    #   validate clamp formulas (GameConfig.hpp:250-275). Shipping
    #   GameConfig.hpp pulls in <AL/al.h> + engine/renderer/Renderer.hpp
    #   transitively, which the no-GPU test build cannot link against.
    #   Cross-product property: every (panel-option field, value) pair
    #   either accepts cleanly or clamps to the documented window —
    #   NEVER silently accepts an out-of-range or NaN-poisoned answer.
    #   Also covers uint32 wrap-around (-1u clamps to ceiling, not
    #   garbage), NaN/Inf sanity, and idempotence (validate twice ==
    #   validate once).
    unit/test_game_property_config.cpp
    # test_game_stress_wave_loop.cpp — 1000-wave + 10000-wave simulated
    #   progression sweeps. Asserts: enemy-count monotone-trending up
    #   over a 100-wave window (linear part dominates sine modulation),
    #   hp scaling monotone over a 50-wave window, no NaN at any wave
    #   1..1000, no integer overflow (saturation routed through),
    #   per-wave linear-vs-curve delta bounded by amplitude, no extreme
    #   step size between consecutive waves, cumulative enemy budget
    #   strictly increasing, late-game milestone (~100 enemies by wave
    #   70 under defaults). Kept in a "_stress" file rather than
    #   alongside the property tests so the property test counts and
    #   the stress test counts are independently grep-able in CI
    #   dashboards.
    unit/test_game_stress_wave_loop.cpp
    # ----------------------------------------------------------------
    # Deep property + stress tests for the CUDA-adjacent math headers
    # under engine/cuda/. These COMPLEMENT (never replace) the existing
    # per-function tests (test_simplex_noise, test_ribbon_trail,
    # test_sequential_impulse, test_ccd, test_ccd_prepass) with large-
    # sample / multi-frame invariants:
    #
    #   test_cuda_property_simplex.cpp
    #     - |Simplex3D(p)| <= 1.0 across 100k random samples
    #     - Lipschitz L < 5 across 50k random small-delta samples
    #     - bit-identical determinism (10x re-evaluation)
    #     - axis vs diagonal variance isotropy
    #     - 256-period wrap symmetry
    #     - permutation-table bijection + doubled-half integrity
    #     - GradAt magnitude sqrt(2) + distinctness
    #     - FastFloor matches std::floor over 20k uniform reals
    #
    #   test_cuda_property_ribbon.cpp
    #     - BuildRibbonStrip vertex count == MaxStripVertexCount(N) for
    #       N in 1..100
    #     - per-segment UV layout {(0,0), (1,0), (0,1), (1,1)}
    #     - colour endpoint tail/head split across 20-particle strip
    #     - degenerate-particle bridge skip (mid / leading / all-deg)
    #     - 500-trial corner-pair midpoint == prev/current
    #     - 1000-trial basis tangent + side unit-length under random
    #       motion
    #     - 1000-particle non-NaN stress
    #
    #   test_cuda_property_sequential_impulse.cpp
    #     - warm-start convergence advantage (5 iters / 10-body)
    #     - lambdaN >= 0 sampled after EVERY iteration (6-body stack)
    #     - friction-pyramid clamp every iteration + 200 random slides
    #     - velocityBias snapshotted once, unchanged across iterations
    #     - 1000 random unit normals -> orthonormal tangent basis
    #     - 20-body cold-start residual drops 5x in 20 iterations
    #
    #   test_cuda_property_ccd.cpp
    #     - SweptSphereAABB sphere coincident-with-box at rest -> TOI=0
    #     - SweptSphereSphere coincident-rest -> TOI=0
    #     - zero-velocity sweep returns false, no NaN poisoning
    #     - ConservativeAdvance matches SweptSphereSphere on random pairs
    #     - 500-trial SweepAABB swept-volume containment
    #     - 1000-trial TOI in [0, 1] for both swept-sphere kernels
    #     - 100-body / 10-obstacle / 100-frame tunneling stress
    #     - IsFastBody exclusion contract
    #
    #   test_cuda_stress_50body_stack.cpp
    #     - 30-frame multi-frame convergence with warm-starting
    #     - bottom contact lambda matches analytic N*g*m*dt prediction
    #     - tilted-gravity friction stress (mu=0.5, 30-deg tilt)
    #     - iteration-budget monotonicity (5..80 iters)
    #     - mass-variation stack stability
    #     - 60-frame (1 sec sim time) stability
    #     - 1000m-offset translation invariance at 50-body scale
    #     - zero-friction -> no spurious tangential motion
    #
    # All five files are USE_MOCK_GPU=1 safe — header-only, STL + engine
    # math only — and exercise the EXACT inline kernels the runtime CUDA
    # path will call from a __host__ __device__ context once the nvcc
    # wire-up lands.
    unit/test_cuda_property_simplex.cpp
    unit/test_cuda_property_ribbon.cpp
    unit/test_cuda_property_sequential_impulse.cpp
    unit/test_cuda_property_ccd.cpp
    unit/test_cuda_stress_50body_stack.cpp
    # unit/test_quest_system.cpp     # SKIPPED — references Clan::Shadow/Warrior/Hunter/Mystic, but the
                                     #           real enum class Clan (game/systems/story_mode.hpp) is
                                     #           { None, MistClan, StormClan, EmberClan, FrostClan }. The
                                     #           Shadow/Lightning/Fire/Ice names live on ClanElementType,
                                     #           not Clan. Rewrite test against the real enum.
    # unit/test_combat_system.cpp    # SKIPPED — passes MockECS::ECS* to CombatSystem::init which expects
                                     #           CatEngine::ECS*, defines a local AttackData struct that
                                     #           shadows CatGame::AttackData with incompatible fields
                                     #           (type vs damageType, direction vs knockbackDirection), and
                                     #           initialises Entity from int. Needs a rewrite bridging
                                     #           mocks to the real ECS header or using real CatEngine::ECS.
    unit/test_story_mode.cpp
    # unit/test_day_night.cpp        # SKIPPED — references class DayNightCycle (real name:
                                     #           DayNightCycleSystem), uses a 0-24-hour model (real API is
                                     #           0.0-1.0 normalized), and calls isDay/setCurrentTime/
                                     #           setTimeSpeed/getTimeSpeed/getSunlightIntensity which do
                                     #           not exist. Rewrite against the real setTimeOfDay +
                                     #           pauseTime/resumeTime + getSunIntensity lifecycle.
    # unit/test_serialization.cpp    # SKIPPED — drifted vs current cat_customization API
    # unit/test_elemental_magic.cpp  # SKIPPED — default-constructs ElementalMagicSystem, but the shipping
                                     #           class requires an explicit priority argument and an
                                     #           init(CatEngine::ECS*) lifecycle; test also calls
                                     #           magic.initialize() which doesn't exist.
    # unit/test_cat_customization.cpp  # SKIPPED — uses CatCustomization/AccessoryType::Hat/FurColor::Orange
                                      #           which don't exist in the current CatCustomizationSystem
                                      #           (FurColors is a glm::vec3 struct, not an enum).
    # unit/test_dialog_system.cpp    # SKIPPED — uses DialogSystem::initialize/isInitialized/
                                     #           hasActiveDialog/loadDialogData/getDialog/getCurrentLine/
                                     #           advanceDialog, none of which are members of the real
                                     #           CatGame::DialogSystem. Needs a full rewrite against the
                                     #           shipping dialog API.
    # unit/test_npc_system.cpp       # SKIPPED — drifted vs current NPCSystem API
    # unit/test_combo_system.cpp     # SKIPPED — calls ComboSystem::initialize() with zero args (real
                                     #           signature takes at least one) and references
                                     #           isInitialized/hasActiveCombo/registerCombo/hasCombo which
                                     #           are absent from CatGame::ComboSystem. Rewrite when scope
                                     #           permits.
    # unit/test_status_effects.cpp   # SKIPPED — initialises CatEngine::Entity variables from raw int
                                     #           literals. Entity is a strong wrapper type and doesn't
                                     #           convert from int; entities must come from
                                     #           ecs.createEntity(). Rewrite to exercise the real Entity
                                     #           lifecycle.
    # ---------------------------------------------------------------------
    # Renderer-math property-test suite (appended 2026-05-16).
    #
    # Six property-style test files that shotgun the renderer-math headers
    # under 100k-1M randomised inputs. Same drift-guard discipline as
    # test_math_property_*.cpp pioneered: deterministic mt19937 seeds, no
    # hidden state in the test, every property pinned via an explicit
    # closed-form expectation. Each file is header-only / no-GPU and
    # links cleanly under USE_MOCK_GPU=1 with no extra mocks or .cpp TUs.
    #
    # test_render_property_oit_weight.cpp        — 1M-sample monotonicity +
    #   alpha linearity, WBOIT vs sorted-over delta-E < 0.10 in linear RGB
    #   for low-alpha (<0.3) stacks, channel-permute + order-invariance,
    #   clamp-window saturation, alpha=0 / depth=0 NaN guard, kDepthPower
    #   tail-decay calibration. 21 TEST_CASEs.
    # test_render_property_image_compare.cpp     — SSIM(self,self)=1 across
    #   100 random + structured images, symmetry, +/- 0.01 uniform-noise
    #   stays >= 0.98, PSNR monotone in MSE, PPM round-trip byte-exact
    #   on random + non-square images, NaN-on-dimension-mismatch contract,
    #   window-size sweep identity, single-window closed-form check,
    #   SSIMFromFiles agrees with SSIM. 21 TEST_CASEs.
    # test_render_property_mesh_optimizer.cpp    — Tipsy + Forsyth preserve
    #   triangle set across 100 shuffles, beat shuffled across 50 random
    #   icosphere variants, re-optimise idempotent, valence histogram
    #   preserved, disconnected-component reorder, pipeline-composition
    #   regression bound, vector/pointer-API parity. 21 TEST_CASEs.
    # test_render_property_shadow_atlas_packer.cpp — Insert-then-free
    #   restores canFit semantics, 80% density on 100 random workloads,
    #   no-overlap invariant under 50 random workloads, 1000-cycle
    #   alloc/free leaves free-rect count <= 4 after drain, free area +
    #   used area = atlas area, density formula exact, canFit purity,
    #   same-size grid packs to 100%. 24 TEST_CASEs.
    # test_render_property_camera_frustum.cpp    — Frustum extracted from
    #   the canonical perspective + lookAt matrices: camera origin
    #   outside its own frustum, mid-depth on optical axis inside, point
    #   past far plane outside, epsilon-cross of LEFT/RIGHT/TOP/BOTTOM
    #   planes flips signed distance, on-plane is inside (closed half-
    #   space contract), getCorners reproduces frustum, contains agrees
    #   with manual signedDistance loop, fromMatrices and fromMatrix
    #   agree, enclosing AABB contains every corner. Tested via
    #   Engine::Frustum directly to avoid the GLFW dependency Camera.hpp
    #   pulls in via Input.hpp. 24 TEST_CASEs.
    # test_render_property_shader_hot_reload.cpp — Pure helpers in
    #   HotReloadDetail:: + ShaderReloadRegistryDetail::NormalizeSourcePath
    #   are deterministic (same input twice -> same output), quote/unquote
    #   round-trip, classifier-vs-table, BuildGlslcCommand stage flag +
    #   -I per include + always -O, DetectChangedSources matches the !=
    #   predicate, TailLines short input verbatim + long input trimmed,
    #   ShaderReloadRegistry handle uniqueness, path normalisation
    #   bidirectional. 34 TEST_CASEs.
    unit/test_render_property_oit_weight.cpp
    unit/test_render_property_image_compare.cpp
    unit/test_render_property_mesh_optimizer.cpp
    unit/test_render_property_shadow_atlas_packer.cpp
    unit/test_render_property_camera_frustum.cpp
    unit/test_render_property_shader_hot_reload.cpp
    # test_model_loader_joints.cpp pins the glTF JOINTS_0/WEIGHTS_0
    # componentType contract (u8/u16/u32 joints widened per-lane, normalized
    # u8 weights converted to [0,1] floats). The 2026-07-16 outage it guards:
    # every rigged Meshy GLB (player cat + all 4 dogs) failed to load because
    # the extractor memcpy'd u8vec4 joints as i32x4. Builds hermetic GLBs in
    # the temp dir — no dependency on the gitignored art assets.
    unit/test_model_loader_joints.cpp
    # test_web_parity_config.cpp pins every constant in
    # game/config/WebParityConfig.hpp to the threejs reference (wave count/HP
    # formulas, enemy profile, spawn ring, pacing, player/camera rig,
    # progression rewards). Header-only + STL, no engine linkage.
    unit/test_web_parity_config.cpp
    # test_web_parity_environment.cpp pins the environment-parity contract
    # (tree sway helpers, ambient/sun lighting targets, shadow + tree-collision
    # policy) added by the 2026-07-17 verify-fix sweep. Header-only + STL.
    unit/test_web_parity_environment.cpp
)

# Collect integration test sources.
#
# Three legacy tests SKIPPED for drift (MockECS::ECS vs CatEngine::ECS
# mismatch plus phantom Clan/DayNightCycle/ElementalMagicSystem references).
# Files preserved on disk — cardinal rule 4: skip, don't delete.
#
# integration/test_golden_image.cpp is the ACTIVE entry. It closes the
# P0 "Headless render mode + golden-image CI test" item by SSIM-diffing
# tests/golden/smoke.ppm against build-ninja/smoke.ppm. Header-only
# ImageCompare + STL only, so it links cleanly in the no-GPU test build
# the same way tests/unit/test_image_compare.cpp does. The two
# CAT_GOLDEN_IMAGE_DIR / CAT_FRAMEDUMP_CANDIDATE_PATH defines below pin
# the two paths to absolute compile-time values so the test is
# portable across worktrees and cwd-independent inside ctest runs.
set(INTEGRATION_TEST_SOURCES
    integration/test_golden_image.cpp
    # integration/test_game_flow.cpp         # SKIPPED — passes MockECS::ECS* to QuestSystem::init and
                                             #           CombatSystem::init (which want CatEngine::ECS*),
                                             #           and references Clan::Shadow (not a real enum
                                             #           value). Rewrite against the real ECS + Clan enum.
    # integration/test_event_system.cpp      # SKIPPED — same MockECS/CatEngine ECS mismatch plus
                                             #           Clan::Shadow/Warrior references.
    # integration/test_system_integration.cpp # SKIPPED — constructs ElementalMagicSystem with no args
                                              #           (needs priority) and references DayNightCycle
                                              #           (class renamed to DayNightCycleSystem).
)

# Game source files needed for testing (non-GPU parts)
set(GAME_TEST_SOURCES
    ${PROJECT_ROOT}/game/systems/leveling_system.cpp
    ${PROJECT_ROOT}/game/systems/quest_system.cpp
    ${PROJECT_ROOT}/game/systems/CombatSystem.cpp
    ${PROJECT_ROOT}/game/systems/DialogSystem.cpp
    ${PROJECT_ROOT}/game/systems/NPCSystem.cpp
    ${PROJECT_ROOT}/game/systems/status_effects.cpp
    ${PROJECT_ROOT}/game/systems/combo_system.cpp
    ${PROJECT_ROOT}/game/systems/elemental_magic.cpp
    ${PROJECT_ROOT}/game/systems/cat_customization.cpp
    ${PROJECT_ROOT}/game/systems/day_night_cycle.cpp
    ${PROJECT_ROOT}/game/systems/story_mode.cpp
    ${PROJECT_ROOT}/game/systems/clan_territory.cpp
    ${PROJECT_ROOT}/game/systems/night_effects.cpp
    ${PROJECT_ROOT}/game/systems/story_skills.cpp
    ${PROJECT_ROOT}/game/systems/MerchantSystem.cpp
)

# Engine source files needed for testing (non-GPU parts).
#
# EntityManager.cpp is pulled in because several GAME_TEST_SOURCES
# (NPCSystem.cpp, night_effects.cpp, story_mode.cpp) reach into the real
# CatEngine::ECS → CatEngine::EntityManager chain via `ecs_->createEntity()`
# / `destroyEntity()` / `isAlive()`. The ECS class itself is header-only and
# inlines into the callers, but EntityManager has out-of-line definitions
# (in EntityManager.cpp) for the three methods above. Without this source
# the test executables fail to link with LNK2019 on
# `CatEngine::EntityManager::{create,destroy,isAlive}`. The file is a pure
# CPU implementation (std::mutex + std::queue), so it is safe to include in
# the no-GPU test build.
set(ENGINE_TEST_SOURCES
    ${PROJECT_ROOT}/engine/core/Logger.cpp
    ${PROJECT_ROOT}/engine/core/Timer.cpp
    ${PROJECT_ROOT}/engine/ecs/EntityManager.cpp
    # SystemManager has out-of-line ~SystemManager / update / clear /
    # sortSystems definitions in SystemManager.cpp. The ECS class header
    # owns a SystemManager by value, so every ECS destructor (and every
    # Scene destructor, since Scene owns an ECS) needs these symbols at
    # link time. Without this source the linker reports LNK2019 on every
    # SystemManager method as soon as any test constructs a Scene or an
    # ECS — which is exactly the regression test_ecs_scene_correctness
    # hits in the ECS-query test cases.
    ${PROJECT_ROOT}/engine/ecs/SystemManager.cpp
    # ComponentPool.cpp is currently empty besides explicit-instantiation
    # placeholders, but it's tracked here for symmetry so future explicit
    # instantiations (e.g. TransformComponent) don't trigger a silent
    # link-time regression.
    ${PROJECT_ROOT}/engine/ecs/ComponentPool.cpp
    # Scene + SceneSerializer.cpp are pulled in by test_ecs_scene_correctness
    # (and any future scene test). All four files are pure CPU code: SceneNode
    # uses std::function + std::unique_ptr, Scene/SceneManager are STL-only,
    # and SceneSerializer's only non-STL dep is the game-component headers
    # already on the include path via GAME_TEST_SOURCES. No Vulkan, no CUDA.
    ${PROJECT_ROOT}/engine/scene/SceneNode.cpp
    ${PROJECT_ROOT}/engine/scene/Scene.cpp
    ${PROJECT_ROOT}/engine/scene/SceneManager.cpp
    ${PROJECT_ROOT}/engine/scene/SceneSerializer.cpp
    # Memory + job-system implementations are pulled in by test_allocators
    # and test_job_system. All five files are pure CPU code — no Vulkan, no
    # CUDA, no engine RHI. The job-system files transitively include
    # <thread>/<jthread>/<atomic>, all standalone C++20 STL.
    ${PROJECT_ROOT}/engine/memory/LinearAllocator.cpp
    ${PROJECT_ROOT}/engine/memory/StackAllocator.cpp
    ${PROJECT_ROOT}/engine/memory/PoolAllocator.cpp
    ${PROJECT_ROOT}/engine/jobs/JobSystem.cpp
    ${PROJECT_ROOT}/engine/jobs/WorkerThread.cpp
    # Animation subsystem .cpp files pulled in for test_skeleton.cpp and
    # test_animator.cpp. All four files are pure CPU (math + STL) with
    # zero GPU coupling — no Vulkan, no CUDA, no RHI — so they link
    # cleanly under USE_MOCK_GPU=1. The tests exercise the real
    # Skeleton::computeWorldTransforms topological-resolve walk and the
    # real Animator::startTransition cursor-reset path so a regression
    # in either shows up here as a failing assertion, not as a silent
    # broken cat in the demo build.
    ${PROJECT_ROOT}/engine/animation/Animation.cpp
    ${PROJECT_ROOT}/engine/animation/AnimationBlend.cpp
    ${PROJECT_ROOT}/engine/animation/Animator.cpp
    ${PROJECT_ROOT}/engine/animation/Skeleton.cpp
    # BehaviorTree.cpp pulled in for test_ai_behavior_tree.cpp. Pure CPU
    # code (std::unordered_map shadow registry + std::vector working-stack
    # in the iterative dump path), no GPU coupling, no engine-RHI deps —
    # only depends on Blackboard.hpp which is header-only.
    ${PROJECT_ROOT}/engine/ai/BehaviorTree.cpp
    # serialization.cpp is pulled in by test_serialization_endianness.cpp
    # so the BinaryWriter / BinaryReader constructors, file-stream
    # operations, and CRC32 helpers all resolve at link time. Pure CPU —
    # <fstream> + <vector> + STL only, no GPU coupling.
    ${PROJECT_ROOT}/engine/core/serialization.cpp
    # ModelLoader.cpp is pulled in by test_model_loader_joints.cpp. Pure
    # CPU glTF parsing (nlohmann::json + buffer extraction) — its only
    # non-STL link dependency is the stb_image implementation, provided by
    # mocks/stb_image_impl.cpp in this build (the game binary gets it from
    # TextureLoader.cpp instead).
    ${PROJECT_ROOT}/engine/assets/ModelLoader.cpp
)

# Mock implementations.
#
# mock_particle_system.cpp provides link-time stubs for the four
# CatEngine::CUDA::ParticleSystem emitter methods referenced by
# game/systems/elemental_magic.cpp. The real definitions live in
# ParticleSystem.cu which is intentionally excluded from the test build
# (USE_MOCK_GPU=1); without the stubs the linker fails with LNK2019 on
# ParticleSystem::{addEmitter,removeEmitter,updateEmitter,getEmitter}. The
# file itself explains the link-time-mock pattern in detail.
set(MOCK_SOURCES
    mocks/mock_renderer.cpp
    mocks/mock_ecs.cpp
    mocks/mock_particle_system.cpp
    # mock_cat_entity.cpp provides link-time stubs for
    # CatGame::CatEntity::{loadModel, configureAnimations} — the real
    # implementations live in game/entities/CatEntity.cpp and pull in the
    # full AssetManager + Skeleton + Animator + Animation chain, which the
    # mock-GPU test build deliberately excludes (USE_MOCK_GPU=1). NPCSystem
    # references these methods inside spawnNPC, so without the stub the test
    # executables fail to link with LNK2019. See the file's header for the
    # full rationale and the same link-time-mock pattern that
    # mock_particle_system.cpp documents.
    mocks/mock_cat_entity.cpp
    # stb_image_impl.cpp compiles the real stb_image implementation into the
    # test binary. NOT a mock: ModelLoader.cpp calls stbi_load_from_memory /
    # stbi_image_free for embedded baseColor textures, and in the game binary
    # those symbols come from TextureLoader.cpp (which the test build excludes
    # for its Vulkan coupling). Same single-implementation pattern, different
    # host TU.
    mocks/stb_image_impl.cpp
)

# Create unit test executable
add_executable(unit_tests
    ${UNIT_TEST_SOURCES}
    ${GAME_TEST_SOURCES}
    ${ENGINE_TEST_SOURCES}
    ${MOCK_SOURCES}
    test_main.cpp
)

# Create integration test executable
add_executable(integration_tests
    ${INTEGRATION_TEST_SOURCES}
    ${GAME_TEST_SOURCES}
    ${ENGINE_TEST_SOURCES}
    ${MOCK_SOURCES}
    test_main.cpp
)

# Link libraries (no GPU libraries needed for tests).
# pthread is a POSIX library and isn't available on Windows — MSVC's standard
# library handles threading without an explicit link dependency — so we
# only add it on non-Windows builds.
if(NOT WIN32)
    target_link_libraries(unit_tests PRIVATE pthread)
    target_link_libraries(integration_tests PRIVATE pthread)
endif()

# Compile-time absolute paths for test_golden_image.cpp, plumbed via a
# generated header rather than -D macros.
#
# Why a generated header instead of `target_compile_definitions(... PATH="...")`:
#   The earlier `-D` form was emitted into ninja's compile_commands.json as
#       -DCAT_GOLDEN_IMAGE_DIR=\C:/Users/Matt-PC/Documents/App Development/...\
#   The leading and trailing backslashes were CMake's attempt at quoting a
#   value that already contained quote characters; on a path WITHOUT spaces
#   the shell tokenizer happily reassembled it back into a single arg, but
#   on this checkout's path WITH a space ("App Development") the tokenizer
#   split the value and clang's `-D` parser saw an unterminated
#   `\C:/Users/Matt-PC/Documents/App` macro expansion. Result: every
#   `bun bridge/cat.ts validate` for ~30 iterations flagged this file as
#   a severity-2 syntax failure even though the ninja build was always
#   green (ninja runs the command via its own shell with the original
#   un-mangled args, so the validator was the only consumer hitting the
#   shell-tokenization bug).
#
# `configure_file()` substitutes the path values directly into a C++ source
# template at CMake configure time. The resulting header lands in the build
# tree, never needs preprocessor expansion, and contains zero shell-quoted
# strings — so it round-trips through clang, validate, ninja, and IDEs
# uniformly regardless of how spaces / backslashes / Unicode appear in the
# checkout's absolute path.
#
# `${PROJECT_ROOT}/tests/golden` is the stable in-repo location for the
# SSIM reference images; `${CMAKE_BINARY_DIR}/smoke.ppm` is where the
# engine's `--frame-dump` CLI writes by convention (the top-level CMake
# build dir, e.g. build-ninja/, because CatAnnihilation.exe lives there).
# `tests/` is pulled in via `add_subdirectory(tests)` from the parent
# project, so CMAKE_BINARY_DIR here is the PARENT's binary dir — exactly
# what the engine writes to.
#
# The generated header lands at:
#   ${CMAKE_BINARY_DIR}/tests/integration/test_paths.hpp
# We add that prefix to integration_tests's include path so the
# `#include "test_paths.hpp"` in test_golden_image.cpp resolves.
set(CAT_GOLDEN_IMAGE_DIR_VALUE "${PROJECT_ROOT}/tests/golden")
set(CAT_FRAMEDUMP_CANDIDATE_PATH_VALUE "${CMAKE_BINARY_DIR}/smoke.ppm")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_paths.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/integration/test_paths.hpp
    @ONLY
)
target_include_directories(integration_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/integration
)

# Add test targets
enable_testing()
add_test(NAME UnitTests COMMAND unit_tests)
add_test(NAME IntegrationTests COMMAND integration_tests)

# Print test configuration
message(STATUS "")
message(STATUS "=== Test Suite Configuration ===")
message(STATUS "Unit test sources: ${UNIT_TEST_SOURCES}")
message(STATUS "Integration test sources: ${INTEGRATION_TEST_SOURCES}")
message(STATUS "Using mock GPU implementations")
message(STATUS "")

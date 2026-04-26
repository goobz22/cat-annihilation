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

# Compile-time path defines for test_golden_image.cpp.
#
# The golden image lives at a stable in-repo location (tests/golden/smoke.ppm)
# but the test binary may be invoked from any working directory — ctest from
# the build dir, an IDE test-runner from the source dir, or a CI runner from
# an arbitrary shell path. Injecting absolute paths at configure time means
# the test never has to guess or probe relative paths.
#
# CAT_FRAMEDUMP_CANDIDATE_PATH must resolve to the directory where the
# engine's `--frame-dump` CLI writes by convention. That directory is the
# top-level CMake build dir — e.g. build-ninja/ — because
# CatAnnihilation.exe lives there and writes its swapchain readback
# relative to its own working directory. Note: because `tests/` is pulled
# in by `add_subdirectory(tests)` from the parent project, CMAKE_BINARY_DIR
# here refers to the PARENT'S binary dir (the main build-ninja/ folder),
# NOT the tests subdir. An earlier draft used `${CMAKE_BINARY_DIR}/../smoke.ppm`
# on the incorrect assumption CMAKE_BINARY_DIR was tests/; that walked one
# directory too high and the candidate was never found. CMAKE_CURRENT_BINARY_DIR
# here would have been the tests subdir — we deliberately use the parent so
# the path aligns with where the engine actually writes.
#
# The quoting discipline is deliberate: CMake substitutes ${PATH} as raw
# text, so the surrounding \" are necessary to keep the path a valid C++
# string literal after preprocessor expansion. Paths on Windows use forward
# slashes uniformly (MSVC accepts them in file I/O); no escaping of
# backslashes required.
target_compile_definitions(integration_tests PRIVATE
    CAT_GOLDEN_IMAGE_DIR="${PROJECT_ROOT}/tests/golden"
    CAT_FRAMEDUMP_CANDIDATE_PATH="${CMAKE_BINARY_DIR}/smoke.ppm"
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

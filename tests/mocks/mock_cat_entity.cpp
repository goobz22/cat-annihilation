/**
 * Mock CatGame::CatEntity — link-time stubs for the no-GPU test build.
 *
 * WHY this file exists
 * --------------------
 * The real `CatGame::CatEntity::{loadModel, configureAnimations}` live in
 * `game/entities/CatEntity.cpp` and pull in the full asset + animation chain:
 *
 *   * AssetManager::GetInstance().LoadModel(...)  → engine/assets/AssetManager
 *   * std::make_shared<Engine::Skeleton>(...)     → engine/animation/Skeleton
 *   * std::make_shared<Engine::Animator>(...)     → engine/animation/Animator
 *   * std::make_shared<Engine::Animation>(...)    → engine/animation/Animation
 *
 * Adding `game/entities/CatEntity.cpp` to GAME_TEST_SOURCES would cascade
 * those four translation units (plus their model-format dependencies — cgltf,
 * VulkanBuffer for GPU upload, ModelLoader, etc.) into the test build, which
 * intentionally runs CPU-only with USE_MOCK_GPU=1.
 *
 * `game/systems/NPCSystem.cpp` calls both methods inside `spawnNPC` so that
 * the live game lights up 16 distinct NPC cats — but the test suite never
 * invokes `spawnNPC` with a non-empty `modelPath` (the only callers under
 * test exercise dialog / shop / quest paths against transform-only NPCs).
 * The references survive to the link stage anyway because the linker resolves
 * every external symbol referenced by compiled code regardless of whether it
 * runs at test time. Without this stub the test executables fail with LNK2019
 * on `loadModel` / `configureAnimations`.
 *
 * Returning `false` from both stubs makes the production code's failure
 * branch the one exercised under test ("model not found, spawning invisible"
 * → NPC remains transform-only). That matches the test suite's pre-mesh
 * expectation exactly: every NPC is a transform-only entity, dialog/shop
 * works, no MeshComponent in play.
 *
 * Same link-time-mock pattern as `mock_particle_system.cpp` — the stub is
 * defined on the REAL class, in the original namespace, because the linker
 * resolves the mangled symbol of the real type. There is no ODR conflict
 * because `game/entities/CatEntity.cpp` is NOT in the test translation set.
 *
 * If a future unit test needs CatEntity to actually attach a mesh + skeleton
 * + animator, replace these with a proper fake that constructs minimal stand-
 * in components — or graduate the test build to include the real CatEntity.cpp
 * plus its asset/animation dependencies (and accept the resulting build cost).
 */

#include "../../game/entities/CatEntity.hpp"

namespace CatGame {

bool CatEntity::loadModel(CatEngine::ECS& /*ecs*/,
                          CatEngine::Entity /*entity*/,
                          const char* /*modelPath*/) {
    // No mesh attached. Production NPCSystem treats this as a non-fatal
    // failure and spawns the NPC as transform-only, which is exactly what
    // the test suite expects.
    return false;
}

bool CatEntity::configureAnimations(CatEngine::ECS& /*ecs*/,
                                    CatEngine::Entity /*entity*/) {
    // Never reached in the test build because NPCSystem only calls this
    // after loadModel returns true, and the mock above always returns false.
    // We still need the symbol to satisfy the linker (the call site is
    // compiled regardless of which branch the test path exercises).
    return false;
}

} // namespace CatGame

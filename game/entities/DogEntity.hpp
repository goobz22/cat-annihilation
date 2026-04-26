#pragma once

#include "../../engine/ecs/ECS.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include "../components/EnemyComponent.hpp"

// Forward-declare ScenePass so PrewarmGpuMeshes can take a non-owning pointer
// to it without dragging the (heavy) renderer header into every translation
// unit that talks to DogEntity. The ScenePass include lives in DogEntity.cpp.
namespace CatEngine::Renderer { class ScenePass; }

namespace CatGame {

/**
 * Dog Entity Factory
 * Creates enemy dog entities with appropriate components
 */
class DogEntity {
public:
    /**
     * Create a standard dog enemy
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createDog(CatEngine::ECS* ecs,
                                       const Engine::vec3& position,
                                       CatEngine::Entity target);

    /**
     * Create a big dog enemy (high HP, high damage, slow)
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createBigDog(CatEngine::ECS* ecs,
                                          const Engine::vec3& position,
                                          CatEngine::Entity target);

    /**
     * Create a fast dog enemy (low HP, low damage, fast)
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createFastDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target);

    /**
     * Create a boss dog enemy
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createBossDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target);

    /**
     * Create dog of specified type
     * @param ecs ECS instance
     * @param type Enemy type
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @param healthMultiplier Health scaling multiplier (for wave progression)
     * @return Created entity
     */
    static CatEngine::Entity create(CatEngine::ECS* ecs,
                                    EnemyType type,
                                    const Engine::vec3& position,
                                    CatEngine::Entity target,
                                    float healthMultiplier = 1.0f);

    /**
     * Warm the AssetManager model cache for all four dog-variant GLBs
     * (regular / fast / big / boss).
     *
     * WHY this exists (2026-04-26 perf halt iteration):
     * The cat NPCs are pre-loaded by `NPCSystem::loadNPCsFromFile` at engine
     * init (16 GLB loads in 2.4 s before the main loop starts). The dog GLBs
     * are NOT pre-loaded — `attachMeshAndAnimator` is the FIRST caller of
     * `AssetManager::LoadModel(dog_*.glb)`, which means the first wave's
     * first-of-each-variant spawn pays the full `ModelLoader::Load` cost
     * synchronously on the game thread:
     *   - Open the GLB file.
     *   - Parse the JSON header + base64 buffer chunks.
     *   - Decompress the embedded geometry blob (Meshy GLBs are dense:
     *     dog_big.glb is ~250k verts before LOD).
     *   - Hand the parsed Model back so the caller can build its Skeleton
     *     + Animator on top.
     *
     * Cat-verify evidence row #6 (2026-04-26 01:51 UTC) shows the smoking
     * gun: BigDog spawn at frame ~150 → fps=14 in the very next heartbeat,
     * then recovery to fps=43 the heartbeat after. The same variant
     * spawned in wave 2 at frame ~960 reads fps=56 (cache hit). That fps
     * delta is `attribute=disk-load+parse for the Meshy GLB`.
     *
     * By calling this from `CatAnnihilation::loadAssets()` (just like the
     * cats are pre-loaded by the NPC path), wave 1's first BigDog spawn
     * already sees a cache hit and skips the synchronous load. Boss waves
     * also benefit: the very first BossDog (which today triggers a fresh
     * dog_boss.glb load) becomes a cache hit too.
     *
     * Cost: four `AssetManager::LoadModel` calls during init. The cats
     * already pay 2.4 s of startup time loading 16 GLBs; adding 4 more
     * dog GLBs (smaller than the cat NPCs because there are only 4
     * variants vs 16 cat clans) is a fraction of a second more, paid
     * before the first frame paints. Strictly better than spending it
     * mid-wave at 14 fps.
     *
     * Idempotent: AssetManager is content-addressed by path; calling
     * `LoadModel` twice for the same path returns the cached `shared_ptr`
     * the second time without re-parsing. So even if a future code path
     * accidentally calls `PreloadAllVariants` more than once (e.g., from
     * a "Restart game" handler), there's no per-call cost beyond the
     * mutex acquisition.
     *
     * Robust to missing files: AssetManager + ModelLoader propagate
     * `std::runtime_error` on missing GLB. We catch and `Logger::warn`
     * here so a missing variant file (e.g., the artist hasn't dropped
     * `dog_boss.glb` yet) doesn't abort the boot — the per-spawn path
     * already has the same try/catch and falls back to a meshless dog.
     */
    static void PreloadAllVariants();

    /**
     * Push every dog-variant Model through the renderer's GPU mesh upload
     * path so the per-frame lazy-uploader inside ScenePass becomes a
     * cache-hit no-op the first time a wave-spawned dog appears in view.
     *
     * WHY this exists (2026-04-26 ~02:30 UTC, regression-halt iteration):
     * `PreloadAllVariants` (above) warmed the AssetManager's *CPU-side*
     * model cache so disk I/O + glTF parse happen during init instead of
     * mid-wave. But the renderer's `ScenePass::EnsureModelGpuMesh` is
     * separate — it lazily packs `(position, normal, texcoord0)` into a
     * stride-32 interleaved buffer and uploads it via `VulkanBuffer::
     * UpdateData` the first time a Model* is encountered in a frame's
     * draw list. For Meshy's ~250k-vertex BigDog GLB that's an 8 MB pack
     * + memcpy + flush; cat-verify evidence row #10 (sha=57c6b95) caught
     * it as a 3-second stall when wave-1's BigDog first spawned at frame
     * ~155 (heartbeat went `frames=179 fps=51 -> frames=202 fps=8` over
     * a 3.4 s window — 23 frames at ~7 fps). Wave-2's respawn at frame
     * ~960 read fps=18, proving the cost was the GPU upload (cache cold)
     * not the GLB parse (already warmed by PreloadAllVariants).
     *
     * Calling this from `CatAnnihilation::loadAssets` after
     * `PreloadAllVariants` folds the four uploads into init-time. Cost:
     * four `EnsureModelGpuMesh` calls during boot — totalling ~30 MB of
     * VkBuffer allocation + memcpy + flush, paid before the first frame
     * paints. Strictly better than spending it at <10 fps mid-combat.
     *
     * Idempotent + null-safe:
     *   - Skip cleanly if `scenePass` is null (e.g. headless tests that
     *     don't construct a renderer).
     *   - Skip cleanly if a variant Model isn't in AssetManager's cache
     *     (e.g. PreloadAllVariants logged-and-continued on a missing
     *     dog_boss.glb file). The per-spawn path's existing `try` /
     *     `Model* == nullptr` fallback handles that case downstream.
     *   - `ScenePass::PrewarmModel` is itself idempotent (cache-hit
     *     branch on the second call), so a future restart-flow that
     *     re-runs `loadAssets` doesn't re-allocate or re-upload.
     *
     * Non-fatal on Vulkan allocation failure: PrewarmModel returns false,
     * we log and continue. The runtime path will retry the upload on
     * first encounter and the per-spawn fallback to a meshless dog
     * still applies.
     */
    static void PrewarmGpuMeshes(CatEngine::Renderer::ScenePass* scenePass);

private:
    /**
     * Get base stats for enemy type
     */
    struct EnemyStats {
        float health;
        float moveSpeed;
        float attackDamage;
        float attackRange;
        float aggroRange;
        int scoreValue;
        Engine::vec3 scale;
    };

    static EnemyStats getStatsForType(EnemyType type);
};

} // namespace CatGame

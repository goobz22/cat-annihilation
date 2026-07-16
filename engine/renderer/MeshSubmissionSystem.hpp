#pragma once

#include "passes/ScenePass.hpp"
#include "../ecs/ECS.hpp"
#include "../ecs/Entity.hpp"
#include "../math/Frustum.hpp"
#include "../math/Transform.hpp"
#include "../math/Vector.hpp"
#include "../assets/ModelLoader.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace CatGame {
struct MeshComponent;
} // namespace CatGame

namespace CatEngine::Renderer {

/**
 * Collects every ECS entity that carries both an Engine::Transform and a
 * CatGame::MeshComponent and emits a ScenePass::EntityDraw for each one, so
 * the 3D scene pass actually renders mesh-equipped entities instead of
 * silently dropping them the way the audit observed.
 *
 * Lifetime guarantee
 * ------------------
 * The renderer records draw commands now but the GPU may keep reading from
 * the Model's vertex/index buffers for up to `maxFramesInFlight` frames.
 * If an entity is destroyed (or its MeshComponent is removed) in the same
 * frame, the std::shared_ptr<Model> would drop to zero immediately, the
 * AssetManager cache would free it on the next UnloadUnusedAssets pass, and
 * the GPU would sample freed memory. To prevent that, every Submit() call
 * copies the strong ref into an internal ring keyed by frame index. Refs
 * are released only when the same slot is reused `maxFramesInFlight` later
 * — by which point the GPU has signalled the fence for the original frame.
 */
class MeshSubmissionSystem {
public:
    /**
     * Construct a submission system sized for the renderer's frame-in-flight
     * count. The default of 2 matches Renderer::Config::maxFramesInFlight.
     */
    explicit MeshSubmissionSystem(std::size_t maxFramesInFlight = 2);

    /**
     * Walk the ECS and push one EntityDraw per entity with Transform +
     * MeshComponent into `out`. Strong refs to each Model are retained in
     * the ring slot for `frameIndex` so the GPU can read the buffers for
     * the life of the frame.
     *
     * If `frustum` is non-null, entities whose model AABB sphere lies fully
     * outside the camera frustum are skipped — no EntityDraw is emitted, no
     * skinning palette is computed, and no Model retention ref is added for
     * that frame. With ~16 NPCs scattered across the world plus an active
     * wave, the player's frustum typically contains 3-6 of them at a time;
     * dropping the off-camera ones eliminates ~70% of the per-frame CPU
     * skinning workload (each Meshy-sourced rigged cat is 100k-200k verts).
     *
     * Lifetime note: the retention ring is unaffected by culling — frames
     * that exit the frustum simply contribute no entries to the slot. The
     * Model's strong ref is still held by the entity's MeshComponent, so
     * the model isn't unloaded; a cat that walks back into view next frame
     * is uploaded from the existing per-Model GPU cache without re-loading.
     */
    void Submit(CatEngine::ECS& ecs,
                std::size_t frameIndex,
                std::vector<ScenePass::EntityDraw>& out,
                const Engine::Frustum* frustum = nullptr,
                const Engine::vec3* cameraPosition = nullptr,
                float maxDrawDistance = 0.0F);

    /**
     * Clear retained refs for all frames. Call on renderer shutdown before
     * the AssetManager cache is destroyed so shutdown order is deterministic.
     */
    void Reset();

    std::size_t GetFramesInFlight() const { return m_retained.size(); }

    /**
     * Record that the operator forced the LEGACY per-frame CPU-skinning
     * path (`--enable-cpu-skinning`). Kept as the historical CLI signal;
     * since the 2026-07-16 GPU-skinning iteration the ACTUAL routing
     * decision lives in ScenePass::SetSkinningMode — main.cpp wires the
     * same CLI resolution into both.
     *
     * History (why this flag exists at all — 2026-04-26 regression halt):
     * -------------------------------------------------------------------
     * Each rigged cat/dog Meshy GLB carries 100k-250k vertices. With ~17-20
     * skinned NPCs + dogs visible at once during a wave, host-side skinning
     * is ~3-5 M weighted-mat4 sums + transforms + normalises per frame on a
     * single CPU thread — measured ~3-5 fps in the running game (heartbeat
     * trace 2026-04-26 01:02-01:03 UTC: fps=2-5 with enemies present,
     * recovers to fps=37-46 the moment frustum culling drops the count to
     * <=2). Skinning therefore shipped default-OFF (bind-pose) for months.
     *
     * That tradeoff is retired: the default route is now the GPU-skinned
     * entity pipeline (ScenePass::SkinningMode::GpuPalette), where the CPU
     * contributes only a ~2.4 KB bone palette per skinned entity per frame
     * and the vertex shader does the per-vertex blend. Submit() populates
     * EntityDraw::bonePalette for EVERY animator-bearing entity regardless
     * of this flag — the palette math was never the expensive part.
     *
     * Static intentionally: there's only ever one MeshSubmissionSystem
     * instance (a function-static in CatAnnihilation::renderFrame) and the
     * setter is callable from main.cpp's CLI parser before the game-loop
     * starts; IsCpuSkinningEnabled stays available for game-layer
     * diagnostics that want to report which skinning executor was forced.
     */
    static void SetEnableCpuSkinning(bool enabled);
    static bool IsCpuSkinningEnabled();

private:
    // One retention slot per frame index. Each slot holds the strong refs
    // for all Models drawn during that frame; the slot is cleared and
    // repopulated at the start of the next call for the same frame index.
    std::vector<std::vector<std::shared_ptr<CatEngine::Model>>> m_retained;
};

} // namespace CatEngine::Renderer

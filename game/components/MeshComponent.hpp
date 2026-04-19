#pragma once

#include "../../engine/assets/ModelLoader.hpp"
#include "../../engine/animation/Animator.hpp"
#include "../../engine/animation/Skeleton.hpp"
#include <memory>
#include <string>

namespace CatGame {

/**
 * Renderable mesh + skeletal-animation state attached to an ECS entity.
 *
 * Holds strong references to the AssetManager-cached Model so the renderer
 * and animation subsystems can safely consume the same data. The Animator
 * is owned per-entity because playback state (current clip, blend timer,
 * parameter bag) is entity-local, while the Skeleton is shared from the
 * source model.
 */
struct MeshComponent {
    // Source asset path (kept so the renderer/hot-reloader can resolve the
    // asset without round-tripping through the model).
    std::string sourcePath;

    // Cached model data (vertices, materials, nodes, animation clips).
    // Shared via AssetManager so identical paths reuse GPU/CPU buffers.
    std::shared_ptr<CatEngine::Model> model;

    // Per-entity skeleton and animator. The skeleton's bone hierarchy is
    // derived from the model's node graph; the animator drives bone poses.
    std::shared_ptr<Engine::Skeleton> skeleton;
    std::shared_ptr<Engine::Animator> animator;

    // Visibility flag consumed by the renderer. Culled entities stay alive
    // in the ECS but skip vertex submission.
    bool visible = true;
};

} // namespace CatGame

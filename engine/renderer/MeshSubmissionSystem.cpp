#include "MeshSubmissionSystem.hpp"

#include "../../game/components/MeshComponent.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CatEngine::Renderer {

namespace {

// Compute an axis-aligned half-extent vector from every mesh's model-space
// bounds. ModelLoader fills boundsMin/boundsMax per Mesh; the union of those
// gives the whole-model AABB in model space. The ScenePass proxy cube
// doesn't take a transform matrix, only a world-space position + half-
// extents, so we multiply the extents by the entity's scale and leave the
// rotation unused. This is approximate for rotated meshes (AABBs can't
// represent rotation) but matches the proxy-cube contract ScenePass
// currently exposes; once real mesh draws land this helper will be replaced.
Engine::vec3 ComputeModelHalfExtents(const CatGame::MeshComponent& meshComponent,
                                     const Engine::vec3& scale) {
    if (!meshComponent.model || meshComponent.model->meshes.empty()) {
        // Fall back to a conservative unit-size cube so the entity is still
        // visible as a marker even for degenerate models.
        return Engine::vec3(0.5F, 0.5F, 0.5F);
    }

    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float minZ = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    float maxZ = -std::numeric_limits<float>::infinity();

    for (const auto& mesh : meshComponent.model->meshes) {
        minX = std::min(minX, mesh.boundsMin.x);
        minY = std::min(minY, mesh.boundsMin.y);
        minZ = std::min(minZ, mesh.boundsMin.z);
        maxX = std::max(maxX, mesh.boundsMax.x);
        maxY = std::max(maxY, mesh.boundsMax.y);
        maxZ = std::max(maxZ, mesh.boundsMax.z);
    }

    // Degenerate bounds (all-zero from an unloaded mesh, or inverted) fall
    // back to a unit cube rather than producing negative extents that the
    // vertex shader would flip inside-out.
    if (!(maxX > minX) || !(maxY > minY) || !(maxZ > minZ)) {
        return Engine::vec3(0.5F, 0.5F, 0.5F);
    }

    const float halfX = 0.5F * (maxX - minX) * std::abs(scale.x);
    const float halfY = 0.5F * (maxY - minY) * std::abs(scale.y);
    const float halfZ = 0.5F * (maxZ - minZ) * std::abs(scale.z);
    return Engine::vec3(halfX, halfY, halfZ);
}

// Translate an entity's Transform into the world-space center of the
// model's AABB. glTF models typically have their origin at the feet, so we
// shift up by half the vertical extent to center the proxy cube on the
// visible silhouette rather than sinking it into the ground.
Engine::vec3 ComputeWorldCenter(const Engine::Transform& transform,
                                const Engine::vec3& halfExtents) {
    return transform.position + Engine::vec3(0.0F, halfExtents.y, 0.0F);
}

} // namespace

MeshSubmissionSystem::MeshSubmissionSystem(std::size_t maxFramesInFlight)
    : m_retained(std::max<std::size_t>(1, maxFramesInFlight)) {}

void MeshSubmissionSystem::Submit(CatEngine::ECS& ecs,
                                  std::size_t frameIndex,
                                  std::vector<ScenePass::EntityDraw>& out) {
    // Wrap frameIndex into the retention ring. The caller passes the
    // renderer's current frame index which may exceed the frames-in-flight
    // count; modulo keeps us inside the ring.
    const std::size_t slot = frameIndex % m_retained.size();
    auto& retainedForFrame = m_retained[slot];

    // Release last use of this slot — any Model whose only remaining ref
    // was the one we held is safe to drop now because the GPU has finished
    // the frame that recorded it (that's the whole point of the ring size
    // matching maxFramesInFlight).
    retainedForFrame.clear();

    ecs.forEach<Engine::Transform, CatGame::MeshComponent>(
        [&](CatEngine::Entity /*entity*/,
            Engine::Transform* transform,
            CatGame::MeshComponent* meshComponent) {
            if (transform == nullptr || meshComponent == nullptr) {
                return;
            }
            if (!meshComponent->visible || !meshComponent->model) {
                return;
            }

            // Retain a strong ref for the duration of this frame's GPU work.
            retainedForFrame.push_back(meshComponent->model);

            ScenePass::EntityDraw draw;
            draw.halfExtents = ComputeModelHalfExtents(*meshComponent,
                                                       transform->scale);
            draw.position = ComputeWorldCenter(*transform, draw.halfExtents);
            // Sentinel tint so mesh-backed draws are visually distinct from
            // the inline player/enemy proxy cubes during the transition to
            // real mesh rendering. Once ScenePass consumes the Model's
            // vertex data directly, this color will be replaced by the
            // model's baseColorFactor.
            draw.color = Engine::vec3(0.75F, 0.75F, 0.85F);
            out.push_back(draw);
        });
}

void MeshSubmissionSystem::Reset() {
    for (auto& slot : m_retained) {
        slot.clear();
    }
}

} // namespace CatEngine::Renderer

#include "Collider.hpp"
#include <algorithm>

namespace CatEngine {
namespace Physics {

Engine::AABB Collider::computeAABB(const Engine::vec3& position, const Engine::Quaternion& rotation) const {
    // Apply local offset to world position
    Engine::vec3 worldCenter = position + rotation.rotate(offset);

    switch (type) {
        case ColliderType::Sphere: {
            // Sphere AABB is simple - just expand by radius
            Engine::vec3 radiusVec(radius, radius, radius);
            return Engine::AABB(worldCenter - radiusVec, worldCenter + radiusVec);
        }

        case ColliderType::Box: {
            // For rotated box, compute corners and find min/max
            Engine::vec3 halfExtents(halfExtentX, halfExtentY, halfExtentZ);

            // Local space corners
            Engine::vec3 corners[8] = {
                Engine::vec3(-halfExtentX, -halfExtentY, -halfExtentZ),
                Engine::vec3( halfExtentX, -halfExtentY, -halfExtentZ),
                Engine::vec3(-halfExtentX,  halfExtentY, -halfExtentZ),
                Engine::vec3( halfExtentX,  halfExtentY, -halfExtentZ),
                Engine::vec3(-halfExtentX, -halfExtentY,  halfExtentZ),
                Engine::vec3( halfExtentX, -halfExtentY,  halfExtentZ),
                Engine::vec3(-halfExtentX,  halfExtentY,  halfExtentZ),
                Engine::vec3( halfExtentX,  halfExtentY,  halfExtentZ)
            };

            // Transform corners to world space
            Engine::AABB aabb;
            for (int i = 0; i < 8; i++) {
                Engine::vec3 worldCorner = worldCenter + rotation.rotate(corners[i]);
                aabb.expand(worldCorner);
            }

            return aabb;
        }

        case ColliderType::Capsule: {
            // Capsule extends along local Y-axis
            Engine::vec3 localAxis(0.0f, 1.0f, 0.0f);
            Engine::vec3 worldAxis = rotation.rotate(localAxis);

            // Capsule endpoints
            Engine::vec3 p1 = worldCenter - worldAxis * halfHeight;
            Engine::vec3 p2 = worldCenter + worldAxis * halfHeight;

            // Find min/max including radius
            Engine::vec3 radiusVec(radius, radius, radius);

            Engine::vec3 minCorner(
                std::min(p1.x, p2.x) - radius,
                std::min(p1.y, p2.y) - radius,
                std::min(p1.z, p2.z) - radius
            );

            Engine::vec3 maxCorner(
                std::max(p1.x, p2.x) + radius,
                std::max(p1.y, p2.y) + radius,
                std::max(p1.z, p2.z) + radius
            );

            return Engine::AABB(minCorner, maxCorner);
        }

        default:
            return Engine::AABB(worldCenter, 0.5f);
    }
}

} // namespace Physics
} // namespace CatEngine

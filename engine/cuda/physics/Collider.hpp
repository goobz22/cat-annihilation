#pragma once

#include "../../math/Vector.hpp"
#include "../../math/AABB.hpp"
#include <cuda_runtime.h>

namespace CatEngine {
namespace Physics {

/**
 * @brief Collider types supported by the physics system
 */
enum class ColliderType : int {
    Sphere = 0,
    Box = 1,
    Capsule = 2
};

/**
 * @brief Collider shape definition
 *
 * Uses a union to store different shape parameters efficiently.
 * Size: 16 bytes (float4) for GPU alignment
 */
struct Collider {
    ColliderType type;

    // Local offset from rigid body center
    Engine::vec3 offset;

    // Shape parameters stored as float4 for GPU efficiency
    // Sphere: (radius, unused, unused, unused)
    // Box: (halfExtentX, halfExtentY, halfExtentZ, unused)
    // Capsule: (radius, halfHeight, unused, unused)
    union {
        struct {
            float radius;           // Sphere/Capsule radius
            float halfHeight;       // Capsule half-height
            float unused1;
            float unused2;
        };
        struct {
            float halfExtentX;      // Box half extents
            float halfExtentY;
            float halfExtentZ;
            float unused3;
        };
        float params[4];
    };

    // Default constructor
    Collider()
        : type(ColliderType::Sphere)
        , offset(0.0f, 0.0f, 0.0f)
        , radius(0.5f)
        , halfHeight(0.0f)
        , unused1(0.0f)
        , unused2(0.0f)
    {}

    // Sphere collider
    static Collider Sphere(float radius, const Engine::vec3& offset = Engine::vec3(0.0f)) {
        Collider c;
        c.type = ColliderType::Sphere;
        c.offset = offset;
        c.radius = radius;
        return c;
    }

    // Box collider
    static Collider Box(const Engine::vec3& halfExtents, const Engine::vec3& offset = Engine::vec3(0.0f)) {
        Collider c;
        c.type = ColliderType::Box;
        c.offset = offset;
        c.halfExtentX = halfExtents.x;
        c.halfExtentY = halfExtents.y;
        c.halfExtentZ = halfExtents.z;
        return c;
    }

    // Capsule collider (cylinder with hemispherical caps)
    static Collider Capsule(float radius, float height, const Engine::vec3& offset = Engine::vec3(0.0f)) {
        Collider c;
        c.type = ColliderType::Capsule;
        c.offset = offset;
        c.radius = radius;
        c.halfHeight = height * 0.5f;
        return c;
    }

    /**
     * @brief Compute AABB for broadphase
     * @param position World position of the rigid body
     * @param rotation Rotation of the rigid body
     */
    Engine::AABB computeAABB(const Engine::vec3& position, const Engine::Quaternion& rotation) const;

    /**
     * @brief Get the maximum radius for spatial hashing
     */
    float getMaxRadius() const {
        switch (type) {
            case ColliderType::Sphere:
                return radius;
            case ColliderType::Box: {
                // Circumradius of the box
                float maxHalfExtent = std::max({halfExtentX, halfExtentY, halfExtentZ});
                return std::sqrt(halfExtentX * halfExtentX + halfExtentY * halfExtentY + halfExtentZ * halfExtentZ);
            }
            case ColliderType::Capsule:
                return radius + halfHeight;
            default:
                return 0.5f;
        }
    }
};

/**
 * @brief GPU-friendly contact point structure
 */
struct ContactPoint {
    float3 point;           // Contact point in world space
    float3 normal;          // Contact normal (from A to B)
    float penetration;      // Penetration depth
    int bodyIndexA;         // First body index
    int bodyIndexB;         // Second body index

    __host__ __device__ ContactPoint()
        : point(make_float3(0, 0, 0))
        , normal(make_float3(0, 1, 0))
        , penetration(0.0f)
        , bodyIndexA(-1)
        , bodyIndexB(-1)
    {}
};

/**
 * @brief Collision pair for broadphase output
 */
struct CollisionPair {
    int bodyIndexA;
    int bodyIndexB;

    __host__ __device__ CollisionPair() : bodyIndexA(-1), bodyIndexB(-1) {}
    __host__ __device__ CollisionPair(int a, int b) : bodyIndexA(a), bodyIndexB(b) {}

    __host__ __device__ bool operator==(const CollisionPair& other) const {
        return (bodyIndexA == other.bodyIndexA && bodyIndexB == other.bodyIndexB) ||
               (bodyIndexA == other.bodyIndexB && bodyIndexB == other.bodyIndexA);
    }

    __host__ __device__ bool isValid() const {
        return bodyIndexA >= 0 && bodyIndexB >= 0 && bodyIndexA != bodyIndexB;
    }
};

} // namespace Physics
} // namespace CatEngine

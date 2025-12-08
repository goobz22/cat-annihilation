#pragma once

#include "../../math/Vector.hpp"
#include "../../math/Quaternion.hpp"
#include "Collider.hpp"
#include <cstdint>

namespace CatEngine {
namespace Physics {

/**
 * @brief Rigid body flags
 */
enum class RigidBodyFlags : uint32_t {
    None = 0,
    Static = 1 << 0,        // Body doesn't move (infinite mass)
    Kinematic = 1 << 1,     // Body moves but isn't affected by forces
    Trigger = 1 << 2,       // Body doesn't generate collision response, only events
    NoGravity = 1 << 3,     // Body ignores gravity
    Sleeping = 1 << 4       // Body is sleeping (not actively simulated)
};

inline RigidBodyFlags operator|(RigidBodyFlags a, RigidBodyFlags b) {
    return static_cast<RigidBodyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RigidBodyFlags operator&(RigidBodyFlags a, RigidBodyFlags b) {
    return static_cast<RigidBodyFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasFlag(RigidBodyFlags flags, RigidBodyFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * @brief CPU-side rigid body representation
 *
 * This structure is used on the CPU and uploaded to GPU as Structure of Arrays.
 * For optimal GPU performance, the PhysicsWorld converts this to SoA layout.
 */
struct RigidBody {
    // Transform
    Engine::vec3 position;
    Engine::Quaternion rotation;
    Engine::vec3 scale;

    // Dynamics
    Engine::vec3 linearVelocity;
    Engine::vec3 angularVelocity;

    // Forces (accumulated each frame, cleared after integration)
    Engine::vec3 force;
    Engine::vec3 torque;

    // Mass properties
    float mass;
    float invMass;              // 1/mass, 0 for static bodies
    Engine::vec3 inertiaTensor; // Diagonal inertia tensor (simplified)
    Engine::vec3 invInertia;    // Inverse inertia tensor

    // Material properties
    float restitution;          // Bounciness (0-1)
    float friction;             // Friction coefficient
    float linearDamping;        // Linear velocity damping
    float angularDamping;       // Angular velocity damping

    // Collider
    Collider collider;

    // Flags
    RigidBodyFlags flags;

    // User data
    void* userData;

    // Default constructor
    RigidBody()
        : position(0.0f, 0.0f, 0.0f)
        , rotation(Engine::Quaternion::identity())
        , scale(1.0f, 1.0f, 1.0f)
        , linearVelocity(0.0f, 0.0f, 0.0f)
        , angularVelocity(0.0f, 0.0f, 0.0f)
        , force(0.0f, 0.0f, 0.0f)
        , torque(0.0f, 0.0f, 0.0f)
        , mass(1.0f)
        , invMass(1.0f)
        , inertiaTensor(1.0f, 1.0f, 1.0f)
        , invInertia(1.0f, 1.0f, 1.0f)
        , restitution(0.5f)
        , friction(0.5f)
        , linearDamping(0.01f)
        , angularDamping(0.05f)
        , collider()
        , flags(RigidBodyFlags::None)
        , userData(nullptr)
    {}

    /**
     * @brief Set mass and automatically compute inverse mass and inertia
     */
    void setMass(float m) {
        mass = m;
        if (m > 0.0f && !isStatic()) {
            invMass = 1.0f / m;
            computeInertia();
        } else {
            invMass = 0.0f;
            invInertia = Engine::vec3(0.0f);
        }
    }

    /**
     * @brief Check if body is static
     */
    bool isStatic() const {
        return hasFlag(flags, RigidBodyFlags::Static);
    }

    /**
     * @brief Check if body is kinematic
     */
    bool isKinematic() const {
        return hasFlag(flags, RigidBodyFlags::Kinematic);
    }

    /**
     * @brief Check if body is a trigger
     */
    bool isTrigger() const {
        return hasFlag(flags, RigidBodyFlags::Trigger);
    }

    /**
     * @brief Check if body is dynamic (affected by forces)
     */
    bool isDynamic() const {
        return !isStatic() && !isKinematic();
    }

    /**
     * @brief Apply force at center of mass
     */
    void applyForce(const Engine::vec3& f) {
        if (isDynamic()) {
            force += f;
        }
    }

    /**
     * @brief Apply force at world position (generates torque)
     */
    void applyForceAtPosition(const Engine::vec3& f, const Engine::vec3& worldPos) {
        if (isDynamic()) {
            force += f;
            Engine::vec3 r = worldPos - position;
            torque += r.cross(f);
        }
    }

    /**
     * @brief Apply impulse (instantaneous velocity change)
     */
    void applyImpulse(const Engine::vec3& impulse) {
        if (isDynamic()) {
            linearVelocity += impulse * invMass;
        }
    }

    /**
     * @brief Apply impulse at world position
     */
    void applyImpulseAtPosition(const Engine::vec3& impulse, const Engine::vec3& worldPos) {
        if (isDynamic()) {
            linearVelocity += impulse * invMass;
            Engine::vec3 r = worldPos - position;
            Engine::vec3 angularImpulse = r.cross(impulse);
            angularVelocity += angularImpulse * invInertia;
        }
    }

    /**
     * @brief Apply torque
     */
    void applyTorque(const Engine::vec3& t) {
        if (isDynamic()) {
            torque += t;
        }
    }

    /**
     * @brief Compute inertia tensor based on collider shape
     */
    void computeInertia() {
        if (mass <= 0.0f || isStatic()) {
            inertiaTensor = Engine::vec3(0.0f);
            invInertia = Engine::vec3(0.0f);
            return;
        }

        switch (collider.type) {
            case ColliderType::Sphere: {
                // I = (2/5) * m * r^2
                float I = 0.4f * mass * collider.radius * collider.radius;
                inertiaTensor = Engine::vec3(I, I, I);
                break;
            }
            case ColliderType::Box: {
                // I_x = (1/12) * m * (h^2 + d^2), etc.
                float x2 = collider.halfExtentX * collider.halfExtentX;
                float y2 = collider.halfExtentY * collider.halfExtentY;
                float z2 = collider.halfExtentZ * collider.halfExtentZ;
                inertiaTensor = Engine::vec3(
                    (1.0f / 12.0f) * mass * (y2 + z2),
                    (1.0f / 12.0f) * mass * (x2 + z2),
                    (1.0f / 12.0f) * mass * (x2 + y2)
                );
                break;
            }
            case ColliderType::Capsule: {
                // Approximate as cylinder
                float r2 = collider.radius * collider.radius;
                float h2 = (collider.halfHeight * 2.0f) * (collider.halfHeight * 2.0f);
                float Ix = (1.0f / 12.0f) * mass * h2 + 0.25f * mass * r2;
                float Iy = 0.5f * mass * r2;
                inertiaTensor = Engine::vec3(Ix, Iy, Ix);
                break;
            }
        }

        // Compute inverse inertia
        invInertia = Engine::vec3(
            inertiaTensor.x > 0.0f ? 1.0f / inertiaTensor.x : 0.0f,
            inertiaTensor.y > 0.0f ? 1.0f / inertiaTensor.y : 0.0f,
            inertiaTensor.z > 0.0f ? 1.0f / inertiaTensor.z : 0.0f
        );
    }

    /**
     * @brief Get world-space collider center
     */
    Engine::vec3 getColliderCenter() const {
        return position + rotation.rotate(collider.offset);
    }
};

} // namespace Physics
} // namespace CatEngine

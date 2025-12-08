#pragma once

#include <cuda_runtime.h>
#include "Collider.hpp"

namespace CatEngine {
namespace Physics {

/**
 * @brief GPU contact manifold (simplified, one contact per pair)
 */
struct GpuContactManifold {
    float3 point;               // Contact point in world space
    float3 normal;              // Contact normal (from A to B)
    float penetration;          // Penetration depth
    int bodyIndexA;             // First body index
    int bodyIndexB;             // Second body index
};

/**
 * @brief GPU rigid body data (Structure of Arrays layout)
 */
struct GpuRigidBodies {
    float3* positions;
    float4* rotations;          // Quaternions (x, y, z, w)
    float3* linearVelocities;
    float3* angularVelocities;
    float3* forces;
    float3* torques;
    float* invMasses;
    float* restitutions;
    float* frictions;
    int* colliderTypes;         // ColliderType enum
    float4* colliderParams;     // Shape parameters
    float3* colliderOffsets;    // Local collider offsets
    uint32_t* flags;            // RigidBodyFlags
    int count;
};

/**
 * @brief Perform narrow-phase collision detection
 *
 * Takes collision pairs from broadphase and generates contact manifolds.
 *
 * @param bodies Rigid body data in SoA format
 * @param pairs Collision pairs from broadphase
 * @param pairCount Number of pairs to check
 * @param contacts Output contact manifolds
 * @param contactCount Output number of contacts found (device pointer)
 * @param maxContacts Maximum number of contacts
 * @param stream CUDA stream
 */
void narrowPhaseCollision(
    const GpuRigidBodies& bodies,
    const int2* pairs,
    int pairCount,
    GpuContactManifold* contacts,
    int* contactCount,
    int maxContacts,
    cudaStream_t stream = 0
);

/**
 * @brief Allocate GPU rigid body data
 */
GpuRigidBodies allocateGpuRigidBodies(int maxBodies);

/**
 * @brief Free GPU rigid body data
 */
void freeGpuRigidBodies(GpuRigidBodies& bodies);

} // namespace Physics
} // namespace CatEngine

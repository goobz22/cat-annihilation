#include "NarrowPhase.cuh"
#include "../CudaError.hpp"
#include <cfloat>

namespace CatEngine {
namespace Physics {

// ============================================================================
// Device Helper Functions
// ============================================================================

__device__ __forceinline__ float3 make_float3(float x, float y, float z) {
    float3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}

__device__ __forceinline__ float3 operator+(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ __forceinline__ float3 operator-(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ __forceinline__ float3 operator*(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

__device__ __forceinline__ float3 operator*(float s, float3 a) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

__device__ __forceinline__ float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ __forceinline__ float length(float3 v) {
    return sqrtf(dot(v, v));
}

__device__ __forceinline__ float lengthSq(float3 v) {
    return dot(v, v);
}

__device__ __forceinline__ float3 normalize(float3 v) {
    float len = length(v);
    return len > 1e-6f ? v * (1.0f / len) : make_float3(0, 0, 0);
}

__device__ __forceinline__ float3 cross(float3 a, float3 b) {
    return make_float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

__device__ __forceinline__ float3 rotateByQuaternion(float3 v, float4 q) {
    // v' = q * v * q^-1
    // Optimized version: v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    float3 qv = make_float3(q.x, q.y, q.z);
    float3 t = cross(qv, v);
    float3 t2 = cross(qv, t + v * q.w);
    return v + t2 * 2.0f;
}

__device__ __forceinline__ float clamp(float x, float minVal, float maxVal) {
    return fminf(fmaxf(x, minVal), maxVal);
}

// ============================================================================
// Collision Detection Functions
// ============================================================================

/**
 * @brief Sphere-Sphere collision
 */
__device__ bool collideSphereSphere(
    float3 posA, float radiusA,
    float3 posB, float radiusB,
    GpuContactManifold& contact
) {
    float3 delta = posB - posA;
    float distSq = lengthSq(delta);
    float sumRadii = radiusA + radiusB;

    if (distSq >= sumRadii * sumRadii) {
        return false; // No collision
    }

    float dist = sqrtf(distSq);
    float penetration = sumRadii - dist;

    // Contact normal (from A to B)
    float3 normal = dist > 1e-6f ? delta * (1.0f / dist) : make_float3(0, 1, 0);

    // Contact point (midpoint between surfaces)
    contact.point = posA + normal * (radiusA - penetration * 0.5f);
    contact.normal = normal;
    contact.penetration = penetration;

    return true;
}

/**
 * @brief Sphere-Box collision (simplified)
 */
__device__ bool collideSphereBox(
    float3 spherePos, float sphereRadius,
    float3 boxPos, float4 boxRot, float3 boxHalfExtents,
    GpuContactManifold& contact
) {
    // Transform sphere center to box local space
    float4 boxRotConj = make_float4(-boxRot.x, -boxRot.y, -boxRot.z, boxRot.w);
    float3 localSpherePos = rotateByQuaternion(spherePos - boxPos, boxRotConj);

    // Find closest point on box to sphere center
    float3 closestPoint = make_float3(
        clamp(localSpherePos.x, -boxHalfExtents.x, boxHalfExtents.x),
        clamp(localSpherePos.y, -boxHalfExtents.y, boxHalfExtents.y),
        clamp(localSpherePos.z, -boxHalfExtents.z, boxHalfExtents.z)
    );

    float3 delta = localSpherePos - closestPoint;
    float distSq = lengthSq(delta);

    if (distSq >= sphereRadius * sphereRadius) {
        return false; // No collision
    }

    float dist = sqrtf(distSq);
    float penetration = sphereRadius - dist;

    // Contact normal in local space
    float3 localNormal = dist > 1e-6f ? delta * (1.0f / dist) : make_float3(0, 1, 0);

    // Transform back to world space
    contact.normal = rotateByQuaternion(localNormal, boxRot);
    contact.point = boxPos + rotateByQuaternion(closestPoint, boxRot);
    contact.penetration = penetration;

    return true;
}

/**
 * @brief Box-Box collision (simplified SAT)
 */
__device__ bool collideBoxBox(
    float3 posA, float4 rotA, float3 halfExtentsA,
    float3 posB, float4 rotB, float3 halfExtentsB,
    GpuContactManifold& contact
) {
    // Simplified: Use sphere approximation for box-box
    // For full implementation, use Separating Axis Theorem (SAT)

    // Approximate boxes as spheres with radius = max half extent
    float radiusA = fmaxf(fmaxf(halfExtentsA.x, halfExtentsA.y), halfExtentsA.z);
    float radiusB = fmaxf(fmaxf(halfExtentsB.x, halfExtentsB.y), halfExtentsB.z);

    return collideSphereSphere(posA, radiusA, posB, radiusB, contact);
}

/**
 * @brief Capsule-Capsule collision
 */
__device__ bool collideCapsuleCapsule(
    float3 posA, float4 rotA, float radiusA, float halfHeightA,
    float3 posB, float4 rotB, float radiusB, float halfHeightB,
    GpuContactManifold& contact
) {
    // Capsule axis in world space (Y-axis)
    float3 axisA = rotateByQuaternion(make_float3(0, 1, 0), rotA);
    float3 axisB = rotateByQuaternion(make_float3(0, 1, 0), rotB);

    // Capsule endpoints
    float3 a1 = posA - axisA * halfHeightA;
    float3 a2 = posA + axisA * halfHeightA;
    float3 b1 = posB - axisB * halfHeightB;
    float3 b2 = posB + axisB * halfHeightB;

    // Find closest points on line segments
    float3 d1 = a2 - a1;
    float3 d2 = b2 - b1;
    float3 r = a1 - b1;

    float a = lengthSq(d1);
    float e = lengthSq(d2);
    float f = dot(d2, r);

    float s, t;
    float EPSILON = 1e-6f;

    if (a <= EPSILON && e <= EPSILON) {
        // Both segments are points
        s = t = 0.0f;
    } else if (a <= EPSILON) {
        // First segment is a point
        s = 0.0f;
        t = clamp(f / e, 0.0f, 1.0f);
    } else {
        float c = dot(d1, r);
        if (e <= EPSILON) {
            // Second segment is a point
            t = 0.0f;
            s = clamp(-c / a, 0.0f, 1.0f);
        } else {
            // General case
            float b = dot(d1, d2);
            float denom = a * e - b * b;

            if (denom != 0.0f) {
                s = clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            } else {
                s = 0.0f;
            }

            t = (b * s + f) / e;

            if (t < 0.0f) {
                t = 0.0f;
                s = clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }

    float3 c1 = a1 + d1 * s;
    float3 c2 = b1 + d2 * t;

    // Now treat as sphere-sphere collision
    return collideSphereSphere(c1, radiusA, c2, radiusB, contact);
}

// ============================================================================
// Kernels
// ============================================================================

/**
 * @brief Narrow-phase collision detection kernel
 */
__global__ void narrowPhaseKernel(
    const GpuRigidBodies bodies,
    const int2* pairs,
    int pairCount,
    GpuContactManifold* contacts,
    int* contactCount,
    int maxContacts
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pairCount) return;

    int2 pair = pairs[idx];
    int idxA = pair.x;
    int idxB = pair.y;

    // Skip if indices are invalid
    if (idxA < 0 || idxB < 0 || idxA >= bodies.count || idxB >= bodies.count) {
        return;
    }

    // Skip static-static pairs
    uint32_t flagsA = bodies.flags[idxA];
    uint32_t flagsB = bodies.flags[idxB];
    bool staticA = (flagsA & (uint32_t)RigidBodyFlags::Static) != 0;
    bool staticB = (flagsB & (uint32_t)RigidBodyFlags::Static) != 0;
    if (staticA && staticB) return;

    // Get body data
    float3 posA = bodies.positions[idxA];
    float4 rotA = bodies.rotations[idxA];
    int typeA = bodies.colliderTypes[idxA];
    float4 paramsA = bodies.colliderParams[idxA];
    float3 offsetA = bodies.colliderOffsets[idxA];

    float3 posB = bodies.positions[idxB];
    float4 rotB = bodies.rotations[idxB];
    int typeB = bodies.colliderTypes[idxB];
    float4 paramsB = bodies.colliderParams[idxB];
    float3 offsetB = bodies.colliderOffsets[idxB];

    // Apply collider offsets
    posA = posA + rotateByQuaternion(offsetA, rotA);
    posB = posB + rotateByQuaternion(offsetB, rotB);

    // Perform collision detection based on types
    GpuContactManifold contact;
    contact.bodyIndexA = idxA;
    contact.bodyIndexB = idxB;

    bool collision = false;

    // Sphere-Sphere
    if (typeA == (int)ColliderType::Sphere && typeB == (int)ColliderType::Sphere) {
        collision = collideSphereSphere(posA, paramsA.x, posB, paramsB.x, contact);
    }
    // Sphere-Box
    else if (typeA == (int)ColliderType::Sphere && typeB == (int)ColliderType::Box) {
        float3 boxHalfExtents = make_float3(paramsB.x, paramsB.y, paramsB.z);
        collision = collideSphereBox(posA, paramsA.x, posB, rotB, boxHalfExtents, contact);
    }
    // Box-Sphere
    else if (typeA == (int)ColliderType::Box && typeB == (int)ColliderType::Sphere) {
        float3 boxHalfExtents = make_float3(paramsA.x, paramsA.y, paramsA.z);
        collision = collideSphereBox(posB, paramsB.x, posA, rotA, boxHalfExtents, contact);
        if (collision) {
            // Flip normal since we swapped order
            contact.normal = contact.normal * -1.0f;
        }
    }
    // Box-Box
    else if (typeA == (int)ColliderType::Box && typeB == (int)ColliderType::Box) {
        float3 halfExtentsA = make_float3(paramsA.x, paramsA.y, paramsA.z);
        float3 halfExtentsB = make_float3(paramsB.x, paramsB.y, paramsB.z);
        collision = collideBoxBox(posA, rotA, halfExtentsA, posB, rotB, halfExtentsB, contact);
    }
    // Capsule-Capsule
    else if (typeA == (int)ColliderType::Capsule && typeB == (int)ColliderType::Capsule) {
        collision = collideCapsuleCapsule(
            posA, rotA, paramsA.x, paramsA.y,
            posB, rotB, paramsB.x, paramsB.y,
            contact
        );
    }

    // Add contact if collision detected
    if (collision) {
        int contactIdx = atomicAdd(contactCount, 1);
        if (contactIdx < maxContacts) {
            contacts[contactIdx] = contact;
        }
    }
}

// ============================================================================
// Host Functions
// ============================================================================

void narrowPhaseCollision(
    const GpuRigidBodies& bodies,
    const int2* pairs,
    int pairCount,
    GpuContactManifold* contacts,
    int* contactCount,
    int maxContacts,
    cudaStream_t stream
) {
    if (pairCount == 0) return;

    const int blockSize = 256;
    const int numBlocks = (pairCount + blockSize - 1) / blockSize;

    // Reset contact count
    CUDA_CHECK(cudaMemsetAsync(contactCount, 0, sizeof(int), stream));

    // Run narrow-phase kernel
    narrowPhaseKernel<<<numBlocks, blockSize, 0, stream>>>(
        bodies,
        pairs,
        pairCount,
        contacts,
        contactCount,
        maxContacts
    );
    CUDA_CHECK_LAST();
}

GpuRigidBodies allocateGpuRigidBodies(int maxBodies) {
    GpuRigidBodies bodies;
    bodies.count = maxBodies;

    CUDA_CHECK(cudaMalloc(&bodies.positions, maxBodies * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&bodies.rotations, maxBodies * sizeof(float4)));
    CUDA_CHECK(cudaMalloc(&bodies.linearVelocities, maxBodies * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&bodies.angularVelocities, maxBodies * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&bodies.forces, maxBodies * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&bodies.torques, maxBodies * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&bodies.invMasses, maxBodies * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&bodies.restitutions, maxBodies * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&bodies.frictions, maxBodies * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&bodies.colliderTypes, maxBodies * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&bodies.colliderParams, maxBodies * sizeof(float4)));
    CUDA_CHECK(cudaMalloc(&bodies.colliderOffsets, maxBodies * sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&bodies.flags, maxBodies * sizeof(uint32_t)));

    return bodies;
}

void freeGpuRigidBodies(GpuRigidBodies& bodies) {
    if (bodies.positions) cudaFree(bodies.positions);
    if (bodies.rotations) cudaFree(bodies.rotations);
    if (bodies.linearVelocities) cudaFree(bodies.linearVelocities);
    if (bodies.angularVelocities) cudaFree(bodies.angularVelocities);
    if (bodies.forces) cudaFree(bodies.forces);
    if (bodies.torques) cudaFree(bodies.torques);
    if (bodies.invMasses) cudaFree(bodies.invMasses);
    if (bodies.restitutions) cudaFree(bodies.restitutions);
    if (bodies.frictions) cudaFree(bodies.frictions);
    if (bodies.colliderTypes) cudaFree(bodies.colliderTypes);
    if (bodies.colliderParams) cudaFree(bodies.colliderParams);
    if (bodies.colliderOffsets) cudaFree(bodies.colliderOffsets);
    if (bodies.flags) cudaFree(bodies.flags);

    bodies = {};
}

} // namespace Physics
} // namespace CatEngine

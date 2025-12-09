#include "Integration.cuh"
#include "../CudaError.hpp"
#include "RigidBody.hpp"

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

__device__ __forceinline__ float4 make_float4(float x, float y, float z, float w) {
    float4 v;
    v.x = x; v.y = y; v.z = z; v.w = w;
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

__device__ __forceinline__ float3 operator*(float3 a, float3 b) {
    return make_float3(a.x * b.x, a.y * b.y, a.z * b.z);
}

__device__ __forceinline__ float4 operator*(float4 a, float s) {
    return make_float4(a.x * s, a.y * s, a.z * s, a.w * s);
}

__device__ __forceinline__ float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ __forceinline__ float3 cross(float3 a, float3 b) {
    return make_float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

__device__ __forceinline__ float length(float3 v) {
    return sqrtf(dot(v, v));
}

__device__ __forceinline__ float3 normalize(float3 v) {
    float len = length(v);
    return len > 1e-6f ? v * (1.0f / len) : make_float3(0, 0, 0);
}

__device__ __forceinline__ float4 normalizeQuat(float4 q) {
    float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 1e-6f) {
        float invLen = 1.0f / len;
        return make_float4(q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen);
    }
    return make_float4(0, 0, 0, 1);
}

__device__ __forceinline__ float4 quatMultiply(float4 a, float4 b) {
    return make_float4(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    );
}

// ============================================================================
// Kernels
// ============================================================================

/**
 * @brief Apply gravity to all dynamic bodies
 */
__global__ void applyForcesKernel(
    float3* forces,
    const float* invMasses,
    const uint32_t* flags,
    float3 gravity,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    uint32_t bodyFlags = flags[idx];
    bool isStatic = (bodyFlags & (uint32_t)RigidBodyFlags::Static) != 0;
    bool isKinematic = (bodyFlags & (uint32_t)RigidBodyFlags::Kinematic) != 0;
    bool noGravity = (bodyFlags & (uint32_t)RigidBodyFlags::NoGravity) != 0;

    if (isStatic || isKinematic || noGravity) {
        return;
    }

    float invMass = invMasses[idx];
    if (invMass > 0.0f) {
        float mass = 1.0f / invMass;
        forces[idx] = forces[idx] + gravity * mass;
    }
}

/**
 * @brief Integrate velocities from forces (semi-implicit Euler)
 */
__global__ void integrateVelocitiesKernel(
    float3* linearVelocities,
    float3* angularVelocities,
    const float3* forces,
    const float3* torques,
    const float* invMasses,
    const uint32_t* flags,
    float deltaTime,
    float linearDamping,
    float angularDamping,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    uint32_t bodyFlags = flags[idx];
    bool isStatic = (bodyFlags & (uint32_t)RigidBodyFlags::Static) != 0;
    bool isKinematic = (bodyFlags & (uint32_t)RigidBodyFlags::Kinematic) != 0;

    if (isStatic || isKinematic) {
        return;
    }

    float invMass = invMasses[idx];
    if (invMass <= 0.0f) return;

    // Integrate linear velocity
    float3 acceleration = forces[idx] * invMass;
    linearVelocities[idx] = linearVelocities[idx] + acceleration * deltaTime;

    // Apply damping
    float dampingFactor = 1.0f / (1.0f + linearDamping * deltaTime);
    linearVelocities[idx] = linearVelocities[idx] * dampingFactor;

    // Integrate angular velocity (simplified, assumes diagonal inertia)
    // Note: For full implementation, would use invInertia tensor
    angularVelocities[idx] = angularVelocities[idx] + torques[idx] * (invMass * deltaTime);

    // Apply angular damping
    float angDampingFactor = 1.0f / (1.0f + angularDamping * deltaTime);
    angularVelocities[idx] = angularVelocities[idx] * angDampingFactor;
}

/**
 * @brief Integrate positions from velocities
 */
__global__ void integratePositionsKernel(
    float3* positions,
    float4* rotations,
    const float3* linearVelocities,
    const float3* angularVelocities,
    const uint32_t* flags,
    float deltaTime,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    uint32_t bodyFlags = flags[idx];
    bool isStatic = (bodyFlags & (uint32_t)RigidBodyFlags::Static) != 0;

    if (isStatic) {
        return;
    }

    // Integrate position
    positions[idx] = positions[idx] + linearVelocities[idx] * deltaTime;

    // Integrate rotation using quaternion derivative
    // q' = q + 0.5 * ω * q * dt
    float3 omega = angularVelocities[idx];
    float4 q = rotations[idx];

    // Convert angular velocity to quaternion
    float4 omegaQuat = make_float4(omega.x, omega.y, omega.z, 0.0f);

    // Quaternion derivative: dq/dt = 0.5 * omega_quat * q
    float4 qDot = quatMultiply(omegaQuat, q);
    qDot = qDot * 0.5f;

    // Euler integration
    q = make_float4(
        q.x + qDot.x * deltaTime,
        q.y + qDot.y * deltaTime,
        q.z + qDot.z * deltaTime,
        q.w + qDot.w * deltaTime
    );

    // Normalize quaternion to prevent drift
    rotations[idx] = normalizeQuat(q);
}

/**
 * @brief Solve contact constraints using sequential impulse
 */
__global__ void solveContactsKernel(
    float3* linearVelocities,
    float3* angularVelocities,
    const float3* positions,
    const float* invMasses,
    const float* restitutions,
    const float* frictions,
    const uint32_t* flags,
    const GpuContactManifold* contacts,
    int contactCount,
    int iteration
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= contactCount) return;

    GpuContactManifold contact = contacts[idx];

    int idxA = contact.bodyIndexA;
    int idxB = contact.bodyIndexB;

    if (idxA < 0 || idxB < 0) return;

    // Get body properties
    float invMassA = invMasses[idxA];
    float invMassB = invMasses[idxB];

    if (invMassA + invMassB <= 0.0f) return; // Both static

    float3 velA = linearVelocities[idxA];
    float3 velB = linearVelocities[idxB];
    float3 angVelA = angularVelocities[idxA];
    float3 angVelB = angularVelocities[idxB];

    float3 posA = positions[idxA];
    float3 posB = positions[idxB];

    float restitution = (restitutions[idxA] + restitutions[idxB]) * 0.5f;
    float friction = (frictions[idxA] + frictions[idxB]) * 0.5f;

    // Contact normal and point
    float3 normal = contact.normal;
    float3 contactPoint = contact.point;

    // Relative velocity at contact point
    float3 rA = contactPoint - posA;
    float3 rB = contactPoint - posB;

    float3 velAtContactA = velA + cross(angVelA, rA);
    float3 velAtContactB = velB + cross(angVelB, rB);
    float3 relativeVel = velAtContactA - velAtContactB;

    float normalVel = dot(relativeVel, normal);

    // Calculate impulse magnitude
    float bias = 0.0f;
    if (iteration == 0) {
        // Baumgarte stabilization to resolve penetration
        const float baumgarte = 0.2f;
        const float slop = 0.01f;
        bias = -(baumgarte / 0.016f) * fmaxf(contact.penetration - slop, 0.0f);
    }

    // Effective mass
    float3 rACrossN = cross(rA, normal);
    float3 rBCrossN = cross(rB, normal);

    // Simplified: assume identity inertia tensor (for full implementation, use invInertia)
    float effectiveMass = invMassA + invMassB +
                          dot(rACrossN, rACrossN) * invMassA +
                          dot(rBCrossN, rBCrossN) * invMassB;

    if (effectiveMass <= 0.0f) return;

    // Compute impulse
    float j = -(normalVel + bias * restitution) / effectiveMass;
    j = fmaxf(j, 0.0f); // Only apply separating impulse

    float3 impulse = normal * j;

    // Apply impulse
    if (invMassA > 0.0f) {
        atomicAdd(&linearVelocities[idxA].x, impulse.x * invMassA);
        atomicAdd(&linearVelocities[idxA].y, impulse.y * invMassA);
        atomicAdd(&linearVelocities[idxA].z, impulse.z * invMassA);

        float3 angImpulseA = cross(rA, impulse) * invMassA;
        atomicAdd(&angularVelocities[idxA].x, angImpulseA.x);
        atomicAdd(&angularVelocities[idxA].y, angImpulseA.y);
        atomicAdd(&angularVelocities[idxA].z, angImpulseA.z);
    }

    if (invMassB > 0.0f) {
        atomicAdd(&linearVelocities[idxB].x, -impulse.x * invMassB);
        atomicAdd(&linearVelocities[idxB].y, -impulse.y * invMassB);
        atomicAdd(&linearVelocities[idxB].z, -impulse.z * invMassB);

        float3 angImpulseB = cross(rB, impulse) * invMassB;
        atomicAdd(&angularVelocities[idxB].x, -angImpulseB.x);
        atomicAdd(&angularVelocities[idxB].y, -angImpulseB.y);
        atomicAdd(&angularVelocities[idxB].z, -angImpulseB.z);
    }

    // Friction impulse (simplified)
    if (j > 0.0f && friction > 0.0f) {
        float3 tangent = relativeVel - normal * normalVel;
        float tangentLen = length(tangent);

        if (tangentLen > 1e-6f) {
            tangent = tangent * (1.0f / tangentLen);

            float tangentVel = dot(relativeVel, tangent);
            float maxFriction = j * friction;

            float jt = -tangentVel / effectiveMass;
            jt = fmaxf(-maxFriction, fminf(jt, maxFriction));

            float3 frictionImpulse = tangent * jt;

            if (invMassA > 0.0f) {
                atomicAdd(&linearVelocities[idxA].x, frictionImpulse.x * invMassA);
                atomicAdd(&linearVelocities[idxA].y, frictionImpulse.y * invMassA);
                atomicAdd(&linearVelocities[idxA].z, frictionImpulse.z * invMassA);
            }

            if (invMassB > 0.0f) {
                atomicAdd(&linearVelocities[idxB].x, -frictionImpulse.x * invMassB);
                atomicAdd(&linearVelocities[idxB].y, -frictionImpulse.y * invMassB);
                atomicAdd(&linearVelocities[idxB].z, -frictionImpulse.z * invMassB);
            }
        }
    }
}

/**
 * @brief Clear forces and torques
 */
__global__ void clearForcesKernel(
    float3* forces,
    float3* torques,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    forces[idx] = make_float3(0, 0, 0);
    torques[idx] = make_float3(0, 0, 0);
}

// ============================================================================
// Host Functions
// ============================================================================

void applyForces(
    GpuRigidBodies& bodies,
    const PhysicsParams& params,
    cudaStream_t stream
) {
    if (bodies.count == 0) return;

    const int blockSize = 256;
    const int numBlocks = (bodies.count + blockSize - 1) / blockSize;

    applyForcesKernel<<<numBlocks, blockSize, 0, stream>>>(
        bodies.forces,
        bodies.invMasses,
        bodies.flags,
        params.gravity,
        bodies.count
    );
    CUDA_CHECK_LAST();
}

void integrateVelocities(
    GpuRigidBodies& bodies,
    const PhysicsParams& params,
    cudaStream_t stream
) {
    if (bodies.count == 0) return;

    const int blockSize = 256;
    const int numBlocks = (bodies.count + blockSize - 1) / blockSize;

    integrateVelocitiesKernel<<<numBlocks, blockSize, 0, stream>>>(
        bodies.linearVelocities,
        bodies.angularVelocities,
        bodies.forces,
        bodies.torques,
        bodies.invMasses,
        bodies.flags,
        params.deltaTime,
        params.linearDamping,
        params.angularDamping,
        bodies.count
    );
    CUDA_CHECK_LAST();
}

void integratePositions(
    GpuRigidBodies& bodies,
    const PhysicsParams& params,
    cudaStream_t stream
) {
    if (bodies.count == 0) return;

    const int blockSize = 256;
    const int numBlocks = (bodies.count + blockSize - 1) / blockSize;

    integratePositionsKernel<<<numBlocks, blockSize, 0, stream>>>(
        bodies.positions,
        bodies.rotations,
        bodies.linearVelocities,
        bodies.angularVelocities,
        bodies.flags,
        params.deltaTime,
        bodies.count
    );
    CUDA_CHECK_LAST();
}

void solveContacts(
    GpuRigidBodies& bodies,
    const GpuContactManifold* contacts,
    int contactCount,
    const PhysicsParams& params,
    cudaStream_t stream
) {
    if (contactCount == 0) return;

    const int blockSize = 256;
    const int numBlocks = (contactCount + blockSize - 1) / blockSize;

    // Iterative solver
    for (int i = 0; i < params.solverIterations; i++) {
        solveContactsKernel<<<numBlocks, blockSize, 0, stream>>>(
            bodies.linearVelocities,
            bodies.angularVelocities,
            bodies.positions,
            bodies.invMasses,
            bodies.restitutions,
            bodies.frictions,
            bodies.flags,
            contacts,
            contactCount,
            i
        );
        CUDA_CHECK_LAST();
    }
}

void clearForces(
    GpuRigidBodies& bodies,
    cudaStream_t stream
) {
    if (bodies.count == 0) return;

    const int blockSize = 256;
    const int numBlocks = (bodies.count + blockSize - 1) / blockSize;

    clearForcesKernel<<<numBlocks, blockSize, 0, stream>>>(
        bodies.forces,
        bodies.torques,
        bodies.count
    );
    CUDA_CHECK_LAST();
}

} // namespace Physics
} // namespace CatEngine

#pragma once

#include <cuda_runtime.h>
#include "NarrowPhase.cuh"

namespace CatEngine {
namespace Physics {

/**
 * @brief Physics simulation parameters
 */
struct PhysicsParams {
    float3 gravity;             // Gravity vector (e.g., (0, -9.81, 0))
    float deltaTime;            // Time step
    float linearDamping;        // Global linear velocity damping
    float angularDamping;       // Global angular velocity damping
    int solverIterations;       // Number of constraint solver iterations
};

/**
 * @brief Apply gravity and external forces
 *
 * @param bodies Rigid body data
 * @param params Physics parameters
 * @param stream CUDA stream
 */
void applyForces(
    GpuRigidBodies& bodies,
    const PhysicsParams& params,
    cudaStream_t stream = 0
);

/**
 * @brief Integrate velocities (semi-implicit Euler)
 *
 * Applies acceleration from forces to velocities:
 * v' = v + (F/m + gravity) * dt
 * ω' = ω + (T/I) * dt
 *
 * @param bodies Rigid body data
 * @param params Physics parameters
 * @param stream CUDA stream
 */
void integrateVelocities(
    GpuRigidBodies& bodies,
    const PhysicsParams& params,
    cudaStream_t stream = 0
);

/**
 * @brief Integrate positions from velocities
 *
 * Updates positions and rotations:
 * p' = p + v * dt
 * q' = q + 0.5 * ω * q * dt
 *
 * @param bodies Rigid body data
 * @param params Physics parameters
 * @param stream CUDA stream
 */
void integratePositions(
    GpuRigidBodies& bodies,
    const PhysicsParams& params,
    cudaStream_t stream = 0
);

/**
 * @brief Solve collision constraints using impulse-based method
 *
 * Applies collision impulses to resolve penetrations and provide restitution.
 *
 * @param bodies Rigid body data
 * @param contacts Contact manifolds
 * @param contactCount Number of contacts
 * @param params Physics parameters
 * @param stream CUDA stream
 */
void solveContacts(
    GpuRigidBodies& bodies,
    const GpuContactManifold* contacts,
    int contactCount,
    const PhysicsParams& params,
    cudaStream_t stream = 0
);

/**
 * @brief Clear accumulated forces and torques
 *
 * @param bodies Rigid body data
 * @param stream CUDA stream
 */
void clearForces(
    GpuRigidBodies& bodies,
    cudaStream_t stream = 0
);

} // namespace Physics
} // namespace CatEngine

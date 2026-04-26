#pragma once

#include "RigidBody.hpp"
#include "Collider.hpp"
#include "SpatialHash.cuh"
#include "NarrowPhase.cuh"
#include "Integration.cuh"
#include "../CudaContext.hpp"
#include "../CudaStream.hpp"
#include "../../math/Vector.hpp"
#include "../../math/Ray.hpp"

#include <vector>
#include <memory>
#include <functional>

namespace CatEngine {
namespace Physics {

/**
 * @brief Raycast hit result
 */
struct RaycastHit {
    int bodyIndex;              // Index of hit body (-1 if no hit)
    Engine::vec3 point;         // Hit point in world space
    Engine::vec3 normal;        // Surface normal at hit point
    float distance;             // Distance from ray origin to hit point

    RaycastHit() : bodyIndex(-1), point(0.0f), normal(0.0f, 1.0f, 0.0f), distance(FLT_MAX) {}

    bool hasHit() const { return bodyIndex >= 0; }
};

/**
 * @brief Collision callback function
 *
 * Called when two bodies collide.
 * Parameters: bodyIndexA, bodyIndexB, contactPoint, contactNormal, penetration
 */
using CollisionCallback = std::function<void(int, int, const Engine::vec3&, const Engine::vec3&, float)>;

/**
 * @brief Main physics simulation manager
 *
 * Manages rigid bodies and simulates physics on the GPU using CUDA.
 * Supports up to 10,000+ rigid bodies at 60 FPS.
 */
class PhysicsWorld {
public:
    /**
     * @brief Construct physics world with CUDA context
     *
     * @param cudaContext CUDA context for GPU operations
     * @param maxBodies Maximum number of rigid bodies (default: 10000)
     * @param maxContacts Maximum number of contacts per frame (default: 50000)
     */
    PhysicsWorld(CUDA::CudaContext& cudaContext, int maxBodies = 10000, int maxContacts = 50000);

    /**
     * @brief Destructor
     */
    ~PhysicsWorld();

    // Non-copyable, movable
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    /**
     * @brief Add a rigid body to the simulation
     *
     * @param body Rigid body to add
     * @return Index of the added body
     */
    int addRigidBody(const RigidBody& body);

    /**
     * @brief Remove a rigid body from the simulation
     *
     * @param index Index of the body to remove
     */
    void removeRigidBody(int index);

    /**
     * @brief Get a rigid body by index
     *
     * @param index Body index
     * @return Reference to the rigid body
     */
    RigidBody& getRigidBody(int index);
    const RigidBody& getRigidBody(int index) const;

    /**
     * @brief Get number of rigid bodies
     */
    int getRigidBodyCount() const { return static_cast<int>(m_bodies.size()); }

    /**
     * @brief Set gravity vector
     *
     * @param gravity Gravity acceleration (e.g., (0, -9.81, 0))
     */
    void setGravity(const Engine::vec3& gravity);

    /**
     * @brief Get gravity vector
     */
    Engine::vec3 getGravity() const { return Engine::vec3(m_params.gravity.x, m_params.gravity.y, m_params.gravity.z); }

    /**
     * @brief Set fixed timestep for simulation
     *
     * @param dt Fixed timestep in seconds (e.g., 1/60.0f)
     */
    void setFixedTimestep(float dt) { m_fixedTimestep = dt; }

    /**
     * @brief Get fixed timestep
     */
    float getFixedTimestep() const { return m_fixedTimestep; }

    /**
     * @brief Set number of solver iterations
     *
     * Higher values = more accurate but slower.
     * Typical range: 4-10
     *
     * @param iterations Number of iterations
     */
    void setSolverIterations(int iterations) { m_params.solverIterations = iterations; }

    /**
     * @brief Get solver iterations
     */
    int getSolverIterations() const { return m_params.solverIterations; }

    /**
     * @brief Step the simulation by a fixed timestep
     *
     * This is the main simulation function. Call this once per frame
     * or in a fixed timestep loop.
     *
     * Steps performed:
     * 1. Upload body data to GPU
     * 2. Clear forces
     * 3. Apply gravity
     * 4. Build spatial hash (broadphase)
     * 5. Find collision pairs
     * 6. Narrow-phase collision detection
     * 7. Integrate velocities
     * 8. Solve contacts (iterative)
     * 9. Integrate positions
     * 10. Download body data from GPU
     * 11. Trigger collision callbacks
     */
    void step();

    /**
     * @brief Step with variable timestep
     *
     * Uses fixed timestep internally with accumulator for stability.
     *
     * @param deltaTime Time elapsed since last frame
     */
    void step(float deltaTime);

    /**
     * @brief Perform a raycast query
     *
     * @param ray Ray to cast
     * @param maxDistance Maximum ray distance
     * @return Raycast hit result
     */
    RaycastHit raycast(const Engine::Ray& ray, float maxDistance = 1000.0f) const;

    /**
     * @brief Set collision callback
     *
     * @param callback Function to call when collision occurs
     */
    void setCollisionCallback(CollisionCallback callback) { m_collisionCallback = callback; }

    /**
     * @brief Synchronize with GPU (wait for all operations to complete)
     */
    void synchronize();

    /**
     * @brief Get GPU memory usage in bytes
     */
    size_t getGpuMemoryUsage() const;

    /**
     * @brief Get statistics
     */
    struct Stats {
        int bodyCount;
        int collisionPairCount;
        int contactCount;
        float lastStepTime;     // in milliseconds

        // CCD runtime pre-pass counters — how many fast-moving bodies were
        // considered this step, how many had their velocities clamped by the
        // swept-TOI math, and the earliest TOI observed. The profiler plots
        // these to surface tunneling hotspots. See CCDPrepass.hpp.
        int ccdFastBodies{0};
        int ccdClamps{0};
        float ccdSmallestTOI{1.0f};
    };

    Stats getStats() const { return m_stats; }

private:
    void uploadToGpu();
    void downloadFromGpu();
    void stepSimulation(float dt);
    void processCollisionCallbacks();

    // CUDA resources
    CUDA::CudaContext& m_cudaContext;
    CUDA::CudaStream m_stream;

    // CPU-side body storage
    std::vector<RigidBody> m_bodies;
    std::vector<int> m_freeIndices;  // Free slots for body removal

    // GPU-side data
    GpuRigidBodies m_gpuBodies;
    GpuSpatialHashData m_spatialHash;
    GpuCollisionPairs m_collisionPairs;
    GpuContactManifold* m_gpuContacts;
    int* m_gpuContactCount;

    // Host-side temporary buffers
    std::vector<float> m_radiiBuffer;

    // Device mirror of m_radiiBuffer, uploaded each step before broadphase.
    // (The previous code passed the CPU pointer straight to CUDA kernels,
    // which triggers cudaErrorIllegalAddress as soon as broadphase runs.)
    float* m_radiiDevice = nullptr;
    int m_radiiDeviceCapacity = 0;

    // Physics parameters
    PhysicsParams m_params;
    float m_fixedTimestep;
    float m_accumulator;

    // Capacity
    int m_maxBodies;
    int m_maxContacts;

    // Collision callback
    CollisionCallback m_collisionCallback;

    // Statistics
    Stats m_stats;

    // Flags
    bool m_needsUpload;
};

} // namespace Physics
} // namespace CatEngine

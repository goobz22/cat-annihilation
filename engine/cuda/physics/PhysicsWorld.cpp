#include "PhysicsWorld.hpp"
#include "../CudaError.hpp"
#include <algorithm>
#include <chrono>

namespace CatEngine {
namespace Physics {

PhysicsWorld::PhysicsWorld(CUDA::CudaContext& cudaContext, int maxBodies, int maxContacts)
    : m_cudaContext(cudaContext)
    , m_stream(CUDA::CudaStream::Flags::NonBlocking)
    , m_maxBodies(maxBodies)
    , m_maxContacts(maxContacts)
    , m_fixedTimestep(1.0f / 60.0f)
    , m_accumulator(0.0f)
    , m_needsUpload(false)
{
    // Initialize physics parameters
    m_params.gravity = make_float3(0.0f, -9.81f, 0.0f);
    m_params.deltaTime = m_fixedTimestep;
    m_params.linearDamping = 0.01f;
    m_params.angularDamping = 0.05f;
    m_params.solverIterations = 6;

    // Allocate GPU memory
    m_gpuBodies = allocateGpuRigidBodies(maxBodies);
    m_spatialHash = allocateSpatialHashData(maxBodies, 2.0f); // Cell size = 2.0
    m_collisionPairs = allocateCollisionPairs(maxBodies * 10); // Estimate 10 pairs per body

    CUDA_CHECK(cudaMalloc(&m_gpuContacts, maxContacts * sizeof(GpuContactManifold)));
    CUDA_CHECK(cudaMalloc(&m_gpuContactCount, sizeof(int)));

    // Device mirror for body radii (used by broadphase kernels — must live on GPU).
    CUDA_CHECK(cudaMalloc(&m_radiiDevice, static_cast<size_t>(maxBodies) * sizeof(float)));
    m_radiiDeviceCapacity = maxBodies;

    // Reserve CPU memory
    m_bodies.reserve(maxBodies);
    m_radiiBuffer.reserve(maxBodies);

    // Initialize stats
    m_stats = {};
}

PhysicsWorld::~PhysicsWorld() {
    // Wait for GPU to finish
    synchronize();

    // Free GPU memory
    freeGpuRigidBodies(m_gpuBodies);
    freeSpatialHashData(m_spatialHash);
    freeCollisionPairs(m_collisionPairs);

    if (m_gpuContacts) cudaFree(m_gpuContacts);
    if (m_gpuContactCount) cudaFree(m_gpuContactCount);
    if (m_radiiDevice) cudaFree(m_radiiDevice);
}

PhysicsWorld::PhysicsWorld(PhysicsWorld&& other) noexcept
    : m_cudaContext(other.m_cudaContext)
    , m_stream(std::move(other.m_stream))
    , m_bodies(std::move(other.m_bodies))
    , m_freeIndices(std::move(other.m_freeIndices))
    , m_gpuBodies(other.m_gpuBodies)
    , m_spatialHash(other.m_spatialHash)
    , m_collisionPairs(other.m_collisionPairs)
    , m_gpuContacts(other.m_gpuContacts)
    , m_gpuContactCount(other.m_gpuContactCount)
    , m_radiiBuffer(std::move(other.m_radiiBuffer))
    , m_radiiDevice(other.m_radiiDevice)
    , m_radiiDeviceCapacity(other.m_radiiDeviceCapacity)
    , m_params(other.m_params)
    , m_fixedTimestep(other.m_fixedTimestep)
    , m_accumulator(other.m_accumulator)
    , m_maxBodies(other.m_maxBodies)
    , m_maxContacts(other.m_maxContacts)
    , m_collisionCallback(std::move(other.m_collisionCallback))
    , m_stats(other.m_stats)
    , m_needsUpload(other.m_needsUpload)
{
    other.m_gpuBodies = {};
    other.m_spatialHash = {};
    other.m_collisionPairs = {};
    other.m_gpuContacts = nullptr;
    other.m_gpuContactCount = nullptr;
    other.m_radiiDevice = nullptr;
    other.m_radiiDeviceCapacity = 0;
}

PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&& other) noexcept {
    if (this != &other) {
        // Free existing resources
        synchronize();
        freeGpuRigidBodies(m_gpuBodies);
        freeSpatialHashData(m_spatialHash);
        freeCollisionPairs(m_collisionPairs);
        if (m_gpuContacts) cudaFree(m_gpuContacts);
        if (m_gpuContactCount) cudaFree(m_gpuContactCount);
        if (m_radiiDevice) cudaFree(m_radiiDevice);

        // Move data (note: m_cudaContext is a reference and cannot be reassigned)
        // Both PhysicsWorld instances should share the same CudaContext
        m_stream = std::move(other.m_stream);
        m_bodies = std::move(other.m_bodies);
        m_freeIndices = std::move(other.m_freeIndices);
        m_gpuBodies = other.m_gpuBodies;
        m_spatialHash = other.m_spatialHash;
        m_collisionPairs = other.m_collisionPairs;
        m_gpuContacts = other.m_gpuContacts;
        m_gpuContactCount = other.m_gpuContactCount;
        m_radiiBuffer = std::move(other.m_radiiBuffer);
        m_radiiDevice = other.m_radiiDevice;
        m_radiiDeviceCapacity = other.m_radiiDeviceCapacity;
        m_params = other.m_params;
        m_fixedTimestep = other.m_fixedTimestep;
        m_accumulator = other.m_accumulator;
        m_maxBodies = other.m_maxBodies;
        m_maxContacts = other.m_maxContacts;
        m_collisionCallback = std::move(other.m_collisionCallback);
        m_stats = other.m_stats;
        m_needsUpload = other.m_needsUpload;

        // Invalidate other
        other.m_gpuBodies = {};
        other.m_spatialHash = {};
        other.m_collisionPairs = {};
        other.m_gpuContacts = nullptr;
        other.m_gpuContactCount = nullptr;
        other.m_radiiDevice = nullptr;
        other.m_radiiDeviceCapacity = 0;
    }
    return *this;
}

int PhysicsWorld::addRigidBody(const RigidBody& body) {
    int index;

    if (!m_freeIndices.empty()) {
        // Reuse a free slot
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
        m_bodies[index] = body;
    } else {
        // Add new body
        if (static_cast<int>(m_bodies.size()) >= m_maxBodies) {
            throw std::runtime_error("PhysicsWorld: Maximum body count reached");
        }
        index = static_cast<int>(m_bodies.size());
        m_bodies.push_back(body);
    }

    m_needsUpload = true;
    return index;
}

void PhysicsWorld::removeRigidBody(int index) {
    if (index < 0 || index >= static_cast<int>(m_bodies.size())) {
        return;
    }

    // Mark as free (don't actually remove to preserve indices)
    m_bodies[index] = RigidBody(); // Reset to default
    m_bodies[index].flags = RigidBodyFlags::Static; // Make static so it's ignored
    m_freeIndices.push_back(index);

    m_needsUpload = true;
}

RigidBody& PhysicsWorld::getRigidBody(int index) {
    if (index < 0 || index >= static_cast<int>(m_bodies.size())) {
        throw std::out_of_range("PhysicsWorld: Invalid body index");
    }
    return m_bodies[index];
}

const RigidBody& PhysicsWorld::getRigidBody(int index) const {
    if (index < 0 || index >= static_cast<int>(m_bodies.size())) {
        throw std::out_of_range("PhysicsWorld: Invalid body index");
    }
    return m_bodies[index];
}

void PhysicsWorld::setGravity(const Engine::vec3& gravity) {
    m_params.gravity = make_float3(gravity.x, gravity.y, gravity.z);
}

void PhysicsWorld::step() {
    stepSimulation(m_fixedTimestep);
}

void PhysicsWorld::step(float deltaTime) {
    m_accumulator += deltaTime;

    // Fixed timestep loop
    while (m_accumulator >= m_fixedTimestep) {
        stepSimulation(m_fixedTimestep);
        m_accumulator -= m_fixedTimestep;
    }
}

void PhysicsWorld::stepSimulation(float dt) {
    auto startTime = std::chrono::high_resolution_clock::now();

    if (m_bodies.empty()) {
        m_stats = {};
        return;
    }

    m_params.deltaTime = dt;

    // 1. Upload body data to GPU
    uploadToGpu();

    int bodyCount = static_cast<int>(m_bodies.size());
    m_gpuBodies.count = bodyCount;

    // 2. Clear forces
    clearForces(m_gpuBodies, m_stream);

    // 3. Apply gravity and forces
    applyForces(m_gpuBodies, m_params, m_stream);

    // Skip broad/narrow-phase work when there is at most one body — a single body
    // cannot collide with itself and the kernels aren't hardened for n < 2
    // (they crashed with cudaErrorIllegalAddress on the 1-player startup case).
    int pairCount = 0;
    int contactCount = 0;

    if (bodyCount >= 2) {
        // 4. Build spatial hash (broadphase)
        buildSpatialHash(
            m_gpuBodies.positions,
            m_radiiDevice,
            bodyCount,
            m_spatialHash,
            m_stream
        );

        // 5. Find collision pairs
        findCollisionPairs(
            m_gpuBodies.positions,
            m_radiiDevice,
            bodyCount,
            m_spatialHash,
            m_collisionPairs,
            m_stream
        );

        // Get pair count
        CUDA_CHECK(cudaMemcpyAsync(&pairCount, m_collisionPairs.count, sizeof(int),
                                   cudaMemcpyDeviceToHost, m_stream));
        m_stream.synchronize();

        // 6. Narrow-phase collision detection
        narrowPhaseCollision(
            m_gpuBodies,
            m_collisionPairs.pairs,
            std::min(pairCount, m_collisionPairs.maxPairs),
            m_gpuContacts,
            m_gpuContactCount,
            m_maxContacts,
            m_stream
        );

        // Get contact count
        CUDA_CHECK(cudaMemcpyAsync(&contactCount, m_gpuContactCount, sizeof(int),
                                   cudaMemcpyDeviceToHost, m_stream));
        m_stream.synchronize();
    }

    // 7. Integrate velocities
    integrateVelocities(m_gpuBodies, m_params, m_stream);

    // 8. Solve contacts
    if (contactCount > 0) {
        solveContacts(
            m_gpuBodies,
            m_gpuContacts,
            std::min(contactCount, m_maxContacts),
            m_params,
            m_stream
        );
    }

    // 9. Integrate positions
    integratePositions(m_gpuBodies, m_params, m_stream);

    // 10. Download body data from GPU
    downloadFromGpu();

    // 11. Process collision callbacks
    if (m_collisionCallback && contactCount > 0) {
        processCollisionCallbacks();
    }

    // Update statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    m_stats.bodyCount = bodyCount;
    m_stats.collisionPairCount = pairCount;
    m_stats.contactCount = contactCount;
    m_stats.lastStepTime = duration.count() / 1000.0f; // Convert to milliseconds
}

void PhysicsWorld::uploadToGpu() {
    int bodyCount = static_cast<int>(m_bodies.size());
    if (bodyCount == 0) return;

    // Prepare data for upload
    std::vector<float3> positions(bodyCount);
    std::vector<float4> rotations(bodyCount);
    std::vector<float3> linearVelocities(bodyCount);
    std::vector<float3> angularVelocities(bodyCount);
    std::vector<float3> forces(bodyCount);
    std::vector<float3> torques(bodyCount);
    std::vector<float> invMasses(bodyCount);
    std::vector<float> restitutions(bodyCount);
    std::vector<float> frictions(bodyCount);
    std::vector<int> colliderTypes(bodyCount);
    std::vector<float4> colliderParams(bodyCount);
    std::vector<float3> colliderOffsets(bodyCount);
    std::vector<uint32_t> flags(bodyCount);

    m_radiiBuffer.resize(bodyCount);

    for (int i = 0; i < bodyCount; i++) {
        const RigidBody& body = m_bodies[i];

        positions[i] = make_float3(body.position.x, body.position.y, body.position.z);
        rotations[i] = make_float4(body.rotation.x, body.rotation.y, body.rotation.z, body.rotation.w);
        linearVelocities[i] = make_float3(body.linearVelocity.x, body.linearVelocity.y, body.linearVelocity.z);
        angularVelocities[i] = make_float3(body.angularVelocity.x, body.angularVelocity.y, body.angularVelocity.z);
        forces[i] = make_float3(body.force.x, body.force.y, body.force.z);
        torques[i] = make_float3(body.torque.x, body.torque.y, body.torque.z);
        invMasses[i] = body.invMass;
        restitutions[i] = body.restitution;
        frictions[i] = body.friction;
        colliderTypes[i] = static_cast<int>(body.collider.type);
        colliderParams[i] = make_float4(
            body.collider.params[0],
            body.collider.params[1],
            body.collider.params[2],
            body.collider.params[3]
        );
        colliderOffsets[i] = make_float3(
            body.collider.offset.x,
            body.collider.offset.y,
            body.collider.offset.z
        );
        flags[i] = static_cast<uint32_t>(body.flags);

        m_radiiBuffer[i] = body.collider.getMaxRadius();
    }

    // Upload to GPU
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.positions, positions.data(),
                               bodyCount * sizeof(float3), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.rotations, rotations.data(),
                               bodyCount * sizeof(float4), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.linearVelocities, linearVelocities.data(),
                               bodyCount * sizeof(float3), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.angularVelocities, angularVelocities.data(),
                               bodyCount * sizeof(float3), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.forces, forces.data(),
                               bodyCount * sizeof(float3), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.torques, torques.data(),
                               bodyCount * sizeof(float3), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.invMasses, invMasses.data(),
                               bodyCount * sizeof(float), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.restitutions, restitutions.data(),
                               bodyCount * sizeof(float), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.frictions, frictions.data(),
                               bodyCount * sizeof(float), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.colliderTypes, colliderTypes.data(),
                               bodyCount * sizeof(int), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.colliderParams, colliderParams.data(),
                               bodyCount * sizeof(float4), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.colliderOffsets, colliderOffsets.data(),
                               bodyCount * sizeof(float3), cudaMemcpyHostToDevice, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(m_gpuBodies.flags, flags.data(),
                               bodyCount * sizeof(uint32_t), cudaMemcpyHostToDevice, m_stream));

    // Mirror the per-body radii onto the device so broadphase/narrow-phase kernels
    // receive a real device pointer (std::vector::data() is host memory and crashes
    // on first access inside a CUDA kernel with cudaErrorIllegalAddress).
    if (bodyCount > m_radiiDeviceCapacity) {
        if (m_radiiDevice != nullptr) {
            cudaFree(m_radiiDevice);
        }
        CUDA_CHECK(cudaMalloc(&m_radiiDevice, static_cast<size_t>(bodyCount) * sizeof(float)));
        m_radiiDeviceCapacity = bodyCount;
    }
    CUDA_CHECK(cudaMemcpyAsync(m_radiiDevice, m_radiiBuffer.data(),
                               bodyCount * sizeof(float), cudaMemcpyHostToDevice, m_stream));

    m_needsUpload = false;
}

void PhysicsWorld::downloadFromGpu() {
    int bodyCount = static_cast<int>(m_bodies.size());
    if (bodyCount == 0) return;

    // Download only dynamic data
    std::vector<float3> positions(bodyCount);
    std::vector<float4> rotations(bodyCount);
    std::vector<float3> linearVelocities(bodyCount);
    std::vector<float3> angularVelocities(bodyCount);

    CUDA_CHECK(cudaMemcpyAsync(positions.data(), m_gpuBodies.positions,
                               bodyCount * sizeof(float3), cudaMemcpyDeviceToHost, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(rotations.data(), m_gpuBodies.rotations,
                               bodyCount * sizeof(float4), cudaMemcpyDeviceToHost, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(linearVelocities.data(), m_gpuBodies.linearVelocities,
                               bodyCount * sizeof(float3), cudaMemcpyDeviceToHost, m_stream));
    CUDA_CHECK(cudaMemcpyAsync(angularVelocities.data(), m_gpuBodies.angularVelocities,
                               bodyCount * sizeof(float3), cudaMemcpyDeviceToHost, m_stream));

    m_stream.synchronize();

    // Update CPU bodies
    for (int i = 0; i < bodyCount; i++) {
        m_bodies[i].position = Engine::vec3(positions[i].x, positions[i].y, positions[i].z);
        m_bodies[i].rotation = Engine::Quaternion(rotations[i].x, rotations[i].y, rotations[i].z, rotations[i].w);
        m_bodies[i].linearVelocity = Engine::vec3(linearVelocities[i].x, linearVelocities[i].y, linearVelocities[i].z);
        m_bodies[i].angularVelocity = Engine::vec3(angularVelocities[i].x, angularVelocities[i].y, angularVelocities[i].z);
    }
}

void PhysicsWorld::processCollisionCallbacks() {
    if (!m_collisionCallback) return;

    // Download contacts from GPU
    int contactCount = 0;
    CUDA_CHECK(cudaMemcpy(&contactCount, m_gpuContactCount, sizeof(int), cudaMemcpyDeviceToHost));

    if (contactCount == 0) return;

    contactCount = std::min(contactCount, m_maxContacts);

    std::vector<GpuContactManifold> contacts(contactCount);
    CUDA_CHECK(cudaMemcpy(contacts.data(), m_gpuContacts,
                          contactCount * sizeof(GpuContactManifold), cudaMemcpyDeviceToHost));

    // Trigger callbacks
    for (const auto& contact : contacts) {
        if (contact.bodyIndexA >= 0 && contact.bodyIndexB >= 0) {
            Engine::vec3 point(contact.point.x, contact.point.y, contact.point.z);
            Engine::vec3 normal(contact.normal.x, contact.normal.y, contact.normal.z);

            m_collisionCallback(
                contact.bodyIndexA,
                contact.bodyIndexB,
                point,
                normal,
                contact.penetration
            );
        }
    }
}

RaycastHit PhysicsWorld::raycast(const Engine::Ray& ray, float maxDistance) const {
    RaycastHit bestHit;

    // Simple CPU raycast (for full implementation, use GPU-accelerated BVH)
    for (size_t i = 0; i < m_bodies.size(); i++) {
        const RigidBody& body = m_bodies[i];

        // Simple sphere raycast
        if (body.collider.type == ColliderType::Sphere) {
            Engine::vec3 center = body.getColliderCenter();
            float radius = body.collider.radius;

            Engine::vec3 oc = ray.origin - center;
            float a = ray.direction.dot(ray.direction);
            float b = 2.0f * oc.dot(ray.direction);
            float c = oc.dot(oc) - radius * radius;
            float discriminant = b * b - 4 * a * c;

            if (discriminant >= 0) {
                float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
                if (t >= 0 && t <= maxDistance && t < bestHit.distance) {
                    bestHit.bodyIndex = static_cast<int>(i);
                    bestHit.distance = t;
                    bestHit.point = ray.origin + ray.direction * t;
                    bestHit.normal = (bestHit.point - center).normalized();
                }
            }
        }
    }

    return bestHit;
}

void PhysicsWorld::synchronize() {
    m_stream.synchronize();
}

size_t PhysicsWorld::getGpuMemoryUsage() const {
    size_t total = 0;

    // Rigid bodies
    total += m_maxBodies * sizeof(float3) * 6; // positions, velocities, forces, etc.
    total += m_maxBodies * sizeof(float4) * 2; // rotations, collider params
    total += m_maxBodies * sizeof(float) * 3;  // invMass, restitution, friction
    total += m_maxBodies * sizeof(int);        // collider types
    total += m_maxBodies * sizeof(uint32_t);   // flags

    // Spatial hash
    total += m_maxBodies * sizeof(uint32_t) * 4; // hashes, indices, temp
    total += m_spatialHash.hashTableSize * sizeof(uint32_t) * 2; // starts, ends

    // Collision pairs
    total += m_collisionPairs.maxPairs * sizeof(int2);

    // Contacts
    total += m_maxContacts * sizeof(GpuContactManifold);

    return total;
}

} // namespace Physics
} // namespace CatEngine

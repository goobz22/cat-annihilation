#include "ParticleSystem.hpp"
#include "../CudaError.hpp"
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <chrono>

namespace CatEngine {
namespace CUDA {

// ============================================================================
// Constructor / Destructor
// ============================================================================

ParticleSystem::ParticleSystem(const CudaContext& context, const Config& config)
    : m_config(config)
    , m_maxParticles(config.maxParticles)
    , m_context(&context)
    , m_stream(CudaStream::Flags::NonBlocking)
{
    initializeBuffers();
    initializeRandomStates();

    // Initialize forces to defaults
    m_forces.gravity = make_float3(0.0f, -9.81f, 0.0f);
    m_forces.windDirection = make_float3(0.0f, 0.0f, 0.0f);
    m_forces.windStrength = 0.0f;
    m_forces.turbulenceEnabled = false;
    m_forces.turbulenceStrength = 1.0f;
    m_forces.turbulenceFrequency = 1.0f;
    m_forces.turbulenceOctaves = 3.0f;
    m_forces.turbulenceTime = 0.0f;
    m_forces.attractorCount = 0;
    m_forces.attractorPositions = nullptr;
    m_forces.attractorStrengths = nullptr;
    m_forces.attractorRadii = nullptr;

    // Initialize stats
    m_stats.maxParticles = m_maxParticles;
    m_stats.activeParticles = 0;
    m_stats.deadParticles = 0;
    m_stats.emitterCount = 0;
    m_stats.attractorCount = 0;
    m_stats.utilizationPercent = 0.0f;
    m_stats.lastCompactionFrame = 0;
    m_stats.particlesEmittedThisFrame = 0;
}

ParticleSystem::~ParticleSystem() {
    // Buffers clean themselves up via RAII
}

ParticleSystem::ParticleSystem(ParticleSystem&& other) noexcept
    : m_config(other.m_config)
    , m_maxParticles(other.m_maxParticles)
    , m_positions(std::move(other.m_positions))
    , m_velocities(std::move(other.m_velocities))
    , m_colors(std::move(other.m_colors))
    , m_lifetimes(std::move(other.m_lifetimes))
    , m_maxLifetimes(std::move(other.m_maxLifetimes))
    , m_sizes(std::move(other.m_sizes))
    , m_rotations(std::move(other.m_rotations))
    , m_alive(std::move(other.m_alive))
    , m_depths(std::move(other.m_depths))
    , m_randStates(std::move(other.m_randStates))
    , m_attractorPositions(std::move(other.m_attractorPositions))
    , m_attractorStrengths(std::move(other.m_attractorStrengths))
    , m_attractorRadii(std::move(other.m_attractorRadii))
    , m_emitters(std::move(other.m_emitters))
    , m_nextEmitterId(other.m_nextEmitterId)
    , m_attractors(std::move(other.m_attractors))
    , m_nextAttractorId(other.m_nextAttractorId)
    , m_forces(other.m_forces)
    , m_simulationTime(other.m_simulationTime)
    , m_particleCount(other.m_particleCount)
    , m_frameCounter(other.m_frameCounter)
    , m_stats(other.m_stats)
    , m_context(other.m_context)
    , m_stream(std::move(other.m_stream))
{
    other.m_particleCount = 0;
}

ParticleSystem& ParticleSystem::operator=(ParticleSystem&& other) noexcept {
    if (this != &other) {
        m_config = other.m_config;
        m_maxParticles = other.m_maxParticles;
        m_positions = std::move(other.m_positions);
        m_velocities = std::move(other.m_velocities);
        m_colors = std::move(other.m_colors);
        m_lifetimes = std::move(other.m_lifetimes);
        m_maxLifetimes = std::move(other.m_maxLifetimes);
        m_sizes = std::move(other.m_sizes);
        m_rotations = std::move(other.m_rotations);
        m_alive = std::move(other.m_alive);
        m_depths = std::move(other.m_depths);
        m_randStates = std::move(other.m_randStates);
        m_attractorPositions = std::move(other.m_attractorPositions);
        m_attractorStrengths = std::move(other.m_attractorStrengths);
        m_attractorRadii = std::move(other.m_attractorRadii);
        m_emitters = std::move(other.m_emitters);
        m_nextEmitterId = other.m_nextEmitterId;
        m_attractors = std::move(other.m_attractors);
        m_nextAttractorId = other.m_nextAttractorId;
        m_forces = other.m_forces;
        m_simulationTime = other.m_simulationTime;
        m_particleCount = other.m_particleCount;
        m_frameCounter = other.m_frameCounter;
        m_stats = other.m_stats;
        m_context = other.m_context;
        m_stream = std::move(other.m_stream);

        other.m_particleCount = 0;
    }
    return *this;
}

// ============================================================================
// Initialization
// ============================================================================

void ParticleSystem::initializeBuffers() {
    // Allocate GPU buffers (SoA layout)
    m_positions.resize(m_maxParticles);
    m_velocities.resize(m_maxParticles);
    m_colors.resize(m_maxParticles);
    m_lifetimes.resize(m_maxParticles);
    m_maxLifetimes.resize(m_maxParticles);
    m_sizes.resize(m_maxParticles);
    m_rotations.resize(m_maxParticles);
    m_alive.resize(m_maxParticles);

    // Work buffers
    m_depths.resize(m_maxParticles);

    // Initialize all particles as dead
    m_alive.zero();
}

void ParticleSystem::initializeRandomStates() {
    // Allocate random states (reuse for efficiency)
    constexpr int NUM_RAND_STATES = 1024;
    m_randStates.resize(NUM_RAND_STATES);

    // Initialize random states on GPU
    int blockSize = 256;
    int numBlocks = (NUM_RAND_STATES + blockSize - 1) / blockSize;

    // Use time-based seed for random number generation
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    auto seed = static_cast<unsigned long long>(micros);

    initRandomStates<<<numBlocks, blockSize, 0, m_stream.get()>>>(
        m_randStates.get(),
        seed,
        NUM_RAND_STATES
    );

    CUDA_CHECK(cudaGetLastError());
}

// ============================================================================
// Emitter Management
// ============================================================================

uint32_t ParticleSystem::addEmitter(const ParticleEmitter& emitter) {
    uint32_t id = m_nextEmitterId++;
    m_emitters[id] = emitter;
    m_emitters[id].id = id;
    m_stats.emitterCount = static_cast<int>(m_emitters.size());
    return id;
}

void ParticleSystem::removeEmitter(uint32_t emitterId) {
    m_emitters.erase(emitterId);
    m_stats.emitterCount = static_cast<int>(m_emitters.size());
}

void ParticleSystem::updateEmitter(uint32_t emitterId, const ParticleEmitter& emitter) {
    auto it = m_emitters.find(emitterId);
    if (it != m_emitters.end()) {
        it->second = emitter;
        it->second.id = emitterId;
    }
}

ParticleEmitter* ParticleSystem::getEmitter(uint32_t emitterId) {
    auto it = m_emitters.find(emitterId);
    return it != m_emitters.end() ? &it->second : nullptr;
}

void ParticleSystem::setEmitterEnabled(uint32_t emitterId, bool enabled) {
    auto it = m_emitters.find(emitterId);
    if (it != m_emitters.end()) {
        it->second.enabled = enabled;
    }
}

void ParticleSystem::triggerBurst(uint32_t emitterId) {
    auto it = m_emitters.find(emitterId);
    if (it != m_emitters.end()) {
        it->second.triggerBurst();
    }
}

// ============================================================================
// Force Management
// ============================================================================

void ParticleSystem::setGravity(const Engine::vec3& gravity) {
    m_forces.gravity = make_float3(gravity.x, gravity.y, gravity.z);
}

void ParticleSystem::setWind(const Engine::vec3& direction, float strength) {
    Engine::vec3 normalized = direction.normalized();
    m_forces.windDirection = make_float3(normalized.x, normalized.y, normalized.z);
    m_forces.windStrength = strength;
}

void ParticleSystem::setTurbulence(bool enabled, float strength, float frequency) {
    m_forces.turbulenceEnabled = enabled;
    m_forces.turbulenceStrength = strength;
    m_forces.turbulenceFrequency = frequency;
}

uint32_t ParticleSystem::addAttractor(const Engine::vec3& position, float strength, float radius) {
    uint32_t id = m_nextAttractorId++;
    m_attractors[id] = {position, strength, radius};
    m_stats.attractorCount = static_cast<int>(m_attractors.size());

    // Update GPU buffers
    m_attractorPositions.resize(m_attractors.size());
    m_attractorStrengths.resize(m_attractors.size());
    m_attractorRadii.resize(m_attractors.size());

    std::vector<float3> positions;
    std::vector<float> strengths;
    std::vector<float> radii;

    for (const auto& [aid, attr] : m_attractors) {
        positions.push_back(make_float3(attr.position.x, attr.position.y, attr.position.z));
        strengths.push_back(attr.strength);
        radii.push_back(attr.radius);
    }

    m_attractorPositions.copyFromHost(positions.data(), positions.size());
    m_attractorStrengths.copyFromHost(strengths.data(), strengths.size());
    m_attractorRadii.copyFromHost(radii.data(), radii.size());

    m_forces.attractorCount = static_cast<int>(m_attractors.size());
    m_forces.attractorPositions = m_attractorPositions.get();
    m_forces.attractorStrengths = m_attractorStrengths.get();
    m_forces.attractorRadii = m_attractorRadii.get();

    return id;
}

void ParticleSystem::removeAttractor(uint32_t attractorId) {
    m_attractors.erase(attractorId);
    m_stats.attractorCount = static_cast<int>(m_attractors.size());

    // Update GPU buffers (same as addAttractor)
    if (m_attractors.empty()) {
        m_forces.attractorCount = 0;
        m_forces.attractorPositions = nullptr;
        m_forces.attractorStrengths = nullptr;
        m_forces.attractorRadii = nullptr;
        return;
    }

    m_attractorPositions.resize(m_attractors.size());
    m_attractorStrengths.resize(m_attractors.size());
    m_attractorRadii.resize(m_attractors.size());

    std::vector<float3> positions;
    std::vector<float> strengths;
    std::vector<float> radii;

    for (const auto& [aid, attr] : m_attractors) {
        positions.push_back(make_float3(attr.position.x, attr.position.y, attr.position.z));
        strengths.push_back(attr.strength);
        radii.push_back(attr.radius);
    }

    m_attractorPositions.copyFromHost(positions.data(), positions.size());
    m_attractorStrengths.copyFromHost(strengths.data(), strengths.size());
    m_attractorRadii.copyFromHost(radii.data(), radii.size());

    m_forces.attractorCount = static_cast<int>(m_attractors.size());
    m_forces.attractorPositions = m_attractorPositions.get();
    m_forces.attractorStrengths = m_attractorStrengths.get();
    m_forces.attractorRadii = m_attractorRadii.get();
}

void ParticleSystem::updateAttractor(uint32_t attractorId, const Engine::vec3& position,
                                     float strength, float radius) {
    auto it = m_attractors.find(attractorId);
    if (it != m_attractors.end()) {
        it->second = {position, strength, radius};

        // Update GPU buffers
        std::vector<float3> positions;
        std::vector<float> strengths;
        std::vector<float> radii;

        for (const auto& [aid, attr] : m_attractors) {
            positions.push_back(make_float3(attr.position.x, attr.position.y, attr.position.z));
            strengths.push_back(attr.strength);
            radii.push_back(attr.radius);
        }

        m_attractorPositions.copyFromHost(positions.data(), positions.size());
        m_attractorStrengths.copyFromHost(strengths.data(), strengths.size());
        m_attractorRadii.copyFromHost(radii.data(), radii.size());
    }
}

void ParticleSystem::clearAttractors() {
    m_attractors.clear();
    m_stats.attractorCount = 0;
    m_forces.attractorCount = 0;
    m_forces.attractorPositions = nullptr;
    m_forces.attractorStrengths = nullptr;
    m_forces.attractorRadii = nullptr;
}

// ============================================================================
// Emission
// ============================================================================

GpuEmitterParams ParticleSystem::convertEmitterToGpu(const ParticleEmitter& emitter) {
    GpuEmitterParams gpu;

    // Transform
    gpu.position = make_float3(emitter.position.x, emitter.position.y, emitter.position.z);
    gpu.rotation = make_float3(emitter.rotation.x, emitter.rotation.y, emitter.rotation.z);
    gpu.scale = make_float3(emitter.scale.x, emitter.scale.y, emitter.scale.z);

    // Shape
    gpu.shapeType = static_cast<int>(emitter.shape);

    // Shape params
    gpu.sphereRadius = emitter.shapeParams.sphereRadius;
    gpu.sphereEmitFromShell = emitter.shapeParams.sphereEmitFromShell;
    gpu.coneAngle = emitter.shapeParams.coneAngle;
    gpu.coneRadius = emitter.shapeParams.coneRadius;
    gpu.coneLength = emitter.shapeParams.coneLength;
    gpu.coneDirection = make_float3(
        emitter.shapeParams.coneDirection.x,
        emitter.shapeParams.coneDirection.y,
        emitter.shapeParams.coneDirection.z
    );
    gpu.boxExtents = make_float3(
        emitter.shapeParams.boxExtents.x,
        emitter.shapeParams.boxExtents.y,
        emitter.shapeParams.boxExtents.z
    );
    gpu.diskRadius = emitter.shapeParams.diskRadius;
    gpu.diskInnerRadius = emitter.shapeParams.diskInnerRadius;
    gpu.diskNormal = make_float3(
        emitter.shapeParams.diskNormal.x,
        emitter.shapeParams.diskNormal.y,
        emitter.shapeParams.diskNormal.z
    );

    // Initial properties
    gpu.velocityMin = make_float3(
        emitter.initialProperties.velocityMin.x,
        emitter.initialProperties.velocityMin.y,
        emitter.initialProperties.velocityMin.z
    );
    gpu.velocityMax = make_float3(
        emitter.initialProperties.velocityMax.x,
        emitter.initialProperties.velocityMax.y,
        emitter.initialProperties.velocityMax.z
    );
    gpu.lifetimeMin = emitter.initialProperties.lifetimeMin;
    gpu.lifetimeMax = emitter.initialProperties.lifetimeMax;
    gpu.sizeMin = emitter.initialProperties.sizeMin;
    gpu.sizeMax = emitter.initialProperties.sizeMax;
    gpu.rotationMin = emitter.initialProperties.rotationMin;
    gpu.rotationMax = emitter.initialProperties.rotationMax;
    gpu.colorBase = make_float4(
        emitter.initialProperties.colorBase.x,
        emitter.initialProperties.colorBase.y,
        emitter.initialProperties.colorBase.z,
        emitter.initialProperties.colorBase.w
    );
    gpu.colorVariation = make_float4(
        emitter.initialProperties.colorVariation.x,
        emitter.initialProperties.colorVariation.y,
        emitter.initialProperties.colorVariation.z,
        emitter.initialProperties.colorVariation.w
    );

    // Behavior
    gpu.fadeOutAlpha = emitter.fadeOutAlpha;
    gpu.scaleOverLifetime = emitter.scaleOverLifetime;
    gpu.endScale = emitter.endScale;
    gpu.velocityDamping = emitter.velocityDamping;

    return gpu;
}

int ParticleSystem::emitFromEmitter(const ParticleEmitter& emitter, int count) {
    if (count <= 0) return 0;

    // Find free particles (simple strategy: append at end)
    int startIndex = m_particleCount;
    int available = m_maxParticles - m_particleCount;
    int toEmit = std::min(count, available);

    if (toEmit <= 0) return 0;

    // Convert emitter to GPU format
    GpuEmitterParams gpuEmitter = convertEmitterToGpu(emitter);

    // Launch emission kernel
    int blockSize = 256;
    int numBlocks = (toEmit + blockSize - 1) / blockSize;

    GpuParticles particles;
    particles.positions = m_positions.get();
    particles.velocities = m_velocities.get();
    particles.colors = m_colors.get();
    particles.lifetimes = m_lifetimes.get();
    particles.maxLifetimes = m_maxLifetimes.get();
    particles.sizes = m_sizes.get();
    particles.rotations = m_rotations.get();
    particles.alive = m_alive.get();
    particles.count = m_particleCount;
    particles.maxCount = m_maxParticles;

    emitParticles<<<numBlocks, blockSize, 0, m_stream.get()>>>(
        particles,
        gpuEmitter,
        startIndex,
        toEmit,
        m_randStates.get()
    );

    CUDA_CHECK(cudaGetLastError());

    m_particleCount += toEmit;
    return toEmit;
}

void ParticleSystem::processEmitters(float deltaTime) {
    m_stats.particlesEmittedThisFrame = 0;

    for (auto& [id, emitter] : m_emitters) {
        if (!emitter.enabled) continue;

        int toEmit = 0;

        // Handle burst emission
        if (emitter.burstTriggered && emitter.burstEnabled) {
            toEmit = emitter.burstCount;
            emitter.burstTriggered = false;

            if (emitter.mode == EmissionMode::OneShot) {
                emitter.enabled = false;
            }
        }

        // Handle continuous emission
        if (emitter.mode == EmissionMode::Looping && emitter.emissionRate > 0.0f) {
            emitter.emissionAccumulator += deltaTime * emitter.emissionRate;
            int particlesToEmit = static_cast<int>(emitter.emissionAccumulator);
            emitter.emissionAccumulator -= particlesToEmit;
            toEmit += particlesToEmit;
        }

        if (toEmit > 0) {
            int emitted = emitFromEmitter(emitter, toEmit);
            m_stats.particlesEmittedThisFrame += emitted;
        }
    }
}

// ============================================================================
// Simulation
// ============================================================================

void ParticleSystem::update(float deltaTime) {
    if (m_particleCount == 0 && m_emitters.empty()) {
        return;
    }

    m_frameCounter++;
    m_simulationTime += deltaTime;
    m_forces.turbulenceTime = m_simulationTime;

    // Process emitters
    processEmitters(deltaTime);

    if (m_particleCount == 0) {
        return;
    }

    // Update particle physics
    int blockSize = 256;
    int numBlocks = (m_particleCount + blockSize - 1) / blockSize;

    GpuParticles particles;
    particles.positions = m_positions.get();
    particles.velocities = m_velocities.get();
    particles.colors = m_colors.get();
    particles.lifetimes = m_lifetimes.get();
    particles.maxLifetimes = m_maxLifetimes.get();
    particles.sizes = m_sizes.get();
    particles.rotations = m_rotations.get();
    particles.alive = m_alive.get();
    particles.count = m_particleCount;
    particles.maxCount = m_maxParticles;

    updateParticles<<<numBlocks, blockSize, 0, m_stream.get()>>>(
        particles,
        m_forces,
        deltaTime
    );

    CUDA_CHECK(cudaGetLastError());

    // Kill dead particles
    killParticles<<<numBlocks, blockSize, 0, m_stream.get()>>>(particles);
    CUDA_CHECK(cudaGetLastError());

    // Compact particles (remove dead)
    if (m_config.enableCompaction) {
        if (m_config.compactionFrequency == 0 ||
            m_frameCounter % m_config.compactionFrequency == 0) {
            compactParticlesInternal();
        }
    }

    // Update stats
    m_stats.activeParticles = m_particleCount;
    m_stats.maxParticles = m_maxParticles;
    m_stats.utilizationPercent = (m_particleCount * 100.0f) / m_maxParticles;
}

void ParticleSystem::reset() {
    m_particleCount = 0;
    m_frameCounter = 0;
    m_simulationTime = 0.0f;
    m_alive.zero();

    for (auto& [id, emitter] : m_emitters) {
        emitter.reset();
    }

    m_stats.activeParticles = 0;
    m_stats.deadParticles = 0;
    m_stats.particlesEmittedThisFrame = 0;
}

void ParticleSystem::compact() {
    compactParticlesInternal();
}

void ParticleSystem::compactParticlesInternal() {
    if (m_particleCount == 0) return;

    GpuParticles particles;
    particles.positions = m_positions.get();
    particles.velocities = m_velocities.get();
    particles.colors = m_colors.get();
    particles.lifetimes = m_lifetimes.get();
    particles.maxLifetimes = m_maxLifetimes.get();
    particles.sizes = m_sizes.get();
    particles.rotations = m_rotations.get();
    particles.alive = m_alive.get();
    particles.count = m_particleCount;
    particles.maxCount = m_maxParticles;

    int compactedCount = 0;
    compactParticles(particles, compactedCount, m_stream.get());

    m_stats.deadParticles = m_particleCount - compactedCount;
    m_particleCount = compactedCount;
    m_stats.lastCompactionFrame = m_frameCounter;
}

void ParticleSystem::sort(const Engine::vec3& cameraPosition) {
    if (!m_config.enableSorting || m_particleCount == 0) {
        return;
    }

    float3 camPos = make_float3(cameraPosition.x, cameraPosition.y, cameraPosition.z);

    int blockSize = 256;
    int numBlocks = (m_particleCount + blockSize - 1) / blockSize;

    GpuParticles particles;
    particles.positions = m_positions.get();
    particles.velocities = m_velocities.get();
    particles.colors = m_colors.get();
    particles.lifetimes = m_lifetimes.get();
    particles.maxLifetimes = m_maxLifetimes.get();
    particles.sizes = m_sizes.get();
    particles.rotations = m_rotations.get();
    particles.alive = m_alive.get();
    particles.count = m_particleCount;
    particles.maxCount = m_maxParticles;

    // Compute depths
    computeParticleDepths<<<numBlocks, blockSize, 0, m_stream.get()>>>(
        particles,
        camPos,
        m_depths.get()
    );

    CUDA_CHECK(cudaGetLastError());

    // Sort
    sortParticles(particles, m_depths.get(), m_stream.get());
}

// ============================================================================
// Rendering Data
// ============================================================================

ParticleSystem::RenderData ParticleSystem::getRenderData() const {
    RenderData data;
    data.positions = m_positions.get();
    data.colors = m_colors.get();
    data.sizes = m_sizes.get();
    data.rotations = m_rotations.get();
    data.count = m_particleCount;
    return data;
}

void ParticleSystem::copyToHost(Engine::vec3* positions, Engine::vec4* colors, float* sizes) {
    if (m_particleCount == 0) return;

    if (positions) {
        m_positions.copyToHost(reinterpret_cast<float3*>(positions), m_particleCount);
    }

    if (colors) {
        m_colors.copyToHost(reinterpret_cast<float4*>(colors), m_particleCount);
    }

    if (sizes) {
        m_sizes.copyToHost(sizes, m_particleCount);
    }
}

void ParticleSystem::synchronize() {
    m_stream.synchronize();
}

} // namespace CUDA
} // namespace CatEngine

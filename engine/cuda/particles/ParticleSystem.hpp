#pragma once

#include "../CudaContext.hpp"
#include "../CudaBuffer.hpp"
#include "../CudaStream.hpp"
#include "ParticleEmitter.hpp"
#include "ParticleKernels.cuh"
#include "../../math/Vector.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <curand_kernel.h>

namespace CatEngine {
namespace CUDA {

/**
 * @brief CUDA-accelerated particle system
 *
 * Manages up to 1 million particles on the GPU with multiple emitters,
 * physics simulation, and efficient rendering data output.
 *
 * Features:
 * - Multiple emitters with various shapes
 * - GPU-accelerated physics (gravity, wind, turbulence, attractors)
 * - Dead particle recycling via stream compaction
 * - Depth sorting for alpha blending
 * - Vulkan-CUDA interop ready
 *
 * Performance target: 100,000+ particles @ 60 FPS, burst capable to 1M
 */
class ParticleSystem {
public:
    /**
     * @brief Configuration for the particle system
     */
    struct Config {
        int maxParticles = 1000000;         // Maximum particle capacity
        bool enableSorting = true;          // Enable depth sorting for alpha blending
        bool enableCompaction = true;       // Enable dead particle compaction
        int compactionFrequency = 60;       // Compact every N frames (0 = every frame)
        bool useAsyncOperations = true;     // Use CUDA streams for async operations
    };

    /**
     * @brief Construct particle system
     *
     * @param context CUDA context
     * @param config System configuration
     */
    ParticleSystem(const CudaContext& context, const Config& config = Config{});

    /**
     * @brief Destructor
     */
    ~ParticleSystem();

    // Non-copyable, movable
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;
    ParticleSystem(ParticleSystem&&) noexcept;
    ParticleSystem& operator=(ParticleSystem&&) noexcept;

    // ========================================================================
    // Emitter Management
    // ========================================================================

    /**
     * @brief Add a new emitter
     *
     * @param emitter Emitter configuration
     * @return Emitter ID
     */
    uint32_t addEmitter(const ParticleEmitter& emitter);

    /**
     * @brief Remove an emitter
     *
     * @param emitterId ID of emitter to remove
     */
    void removeEmitter(uint32_t emitterId);

    /**
     * @brief Update emitter parameters
     *
     * @param emitterId ID of emitter to update
     * @param emitter New emitter configuration
     */
    void updateEmitter(uint32_t emitterId, const ParticleEmitter& emitter);

    /**
     * @brief Get emitter by ID
     *
     * @param emitterId Emitter ID
     * @return Pointer to emitter, or nullptr if not found
     */
    ParticleEmitter* getEmitter(uint32_t emitterId);

    /**
     * @brief Enable/disable an emitter
     *
     * @param emitterId Emitter ID
     * @param enabled Enable state
     */
    void setEmitterEnabled(uint32_t emitterId, bool enabled);

    /**
     * @brief Trigger burst emission on an emitter
     *
     * @param emitterId Emitter ID
     */
    void triggerBurst(uint32_t emitterId);

    /**
     * @brief Get all emitters
     */
    const std::unordered_map<uint32_t, ParticleEmitter>& getEmitters() const {
        return m_emitters;
    }

    // ========================================================================
    // Force Management
    // ========================================================================

    /**
     * @brief Set gravity force
     */
    void setGravity(const Engine::vec3& gravity);

    /**
     * @brief Set wind parameters
     */
    void setWind(const Engine::vec3& direction, float strength);

    /**
     * @brief Enable/disable turbulence
     */
    void setTurbulence(bool enabled, float strength = 1.0f, float frequency = 1.0f);

    /**
     * @brief Add point attractor/repulsor
     *
     * @param position Position in world space
     * @param strength Positive = attract, negative = repel
     * @param radius Effective radius
     * @return Attractor ID
     */
    uint32_t addAttractor(const Engine::vec3& position, float strength, float radius);

    /**
     * @brief Remove attractor
     */
    void removeAttractor(uint32_t attractorId);

    /**
     * @brief Update attractor
     */
    void updateAttractor(uint32_t attractorId, const Engine::vec3& position,
                        float strength, float radius);

    /**
     * @brief Clear all attractors
     */
    void clearAttractors();

    // ========================================================================
    // Simulation
    // ========================================================================

    /**
     * @brief Update particle simulation
     *
     * @param deltaTime Time step in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Reset all particles (clear system)
     */
    void reset();

    /**
     * @brief Clear all dead particles immediately
     */
    void compact();

    /**
     * @brief Sort particles by depth for rendering
     *
     * @param cameraPosition Camera position for depth calculation
     */
    void sort(const Engine::vec3& cameraPosition);

    // ========================================================================
    // Rendering Data
    // ========================================================================

    /**
     * @brief Get particle count
     */
    int getParticleCount() const { return m_particleCount; }

    /**
     * @brief Get maximum particle capacity
     */
    int getMaxParticles() const { return m_maxParticles; }

    /**
     * @brief Get particle data for rendering (device pointers)
     *
     * Returns GPU pointers suitable for Vulkan-CUDA interop or direct compute.
     * Data is in SoA layout for optimal GPU performance.
     */
    struct RenderData {
        const float3* positions;    // Device pointer
        const float4* colors;       // Device pointer
        const float* sizes;         // Device pointer
        const float* rotations;     // Device pointer
        int count;                  // Number of live particles
    };

    /**
     * @brief Get rendering data (GPU pointers)
     */
    RenderData getRenderData() const;

    /**
     * @brief Copy particle data to host (for debugging/CPU rendering)
     *
     * @param positions Output buffer (must be at least getParticleCount() size)
     * @param colors Output buffer (optional)
     * @param sizes Output buffer (optional)
     */
    void copyToHost(Engine::vec3* positions, Engine::vec4* colors = nullptr,
                   float* sizes = nullptr);

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Stats {
        int activeParticles;
        int deadParticles;
        int maxParticles;
        int emitterCount;
        int attractorCount;
        float utilizationPercent;
        int lastCompactionFrame;
        int particlesEmittedThisFrame;
    };

    /**
     * @brief Get system statistics
     */
    Stats getStats() const { return m_stats; }

    /**
     * @brief Synchronize GPU operations (wait for completion)
     */
    void synchronize();

private:
    // Initialization
    void initializeBuffers();
    void initializeRandomStates();

    // Emission
    void processEmitters(float deltaTime);
    int emitFromEmitter(const ParticleEmitter& emitter, int count);
    GpuEmitterParams convertEmitterToGpu(const ParticleEmitter& emitter);

    // Compaction
    void compactParticlesInternal();

    // Configuration
    Config m_config;
    int m_maxParticles;

    // GPU buffers (SoA layout)
    CudaBuffer<float3> m_positions;
    CudaBuffer<float3> m_velocities;
    CudaBuffer<float4> m_colors;
    CudaBuffer<float> m_lifetimes;
    CudaBuffer<float> m_maxLifetimes;
    CudaBuffer<float> m_sizes;
    CudaBuffer<float> m_rotations;
    CudaBuffer<uint32_t> m_alive;

    // Work buffers
    CudaBuffer<float> m_depths;              // For sorting
    CudaBuffer<curandState> m_randStates;    // Random number generator states

    // Attractor buffers
    CudaBuffer<float3> m_attractorPositions;
    CudaBuffer<float> m_attractorStrengths;
    CudaBuffer<float> m_attractorRadii;

    // Emitters
    std::unordered_map<uint32_t, ParticleEmitter> m_emitters;
    uint32_t m_nextEmitterId = 1;

    // Attractors
    struct Attractor {
        Engine::vec3 position;
        float strength;
        float radius;
    };
    std::unordered_map<uint32_t, Attractor> m_attractors;
    uint32_t m_nextAttractorId = 1;

    // Forces
    ForceParams m_forces;
    float m_simulationTime = 0.0f;

    // State
    int m_particleCount = 0;
    int m_frameCounter = 0;
    Stats m_stats{};

    // CUDA resources
    const CudaContext* m_context;
    CudaStream m_stream;
};

} // namespace CUDA
} // namespace CatEngine

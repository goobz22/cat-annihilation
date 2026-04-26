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
     *
     * WHY two overloads instead of `= Config{}` as a default argument:
     * clang 21 refuses to synthesise the nested `Config{}` default
     * constructor inside `ParticleSystem`'s class body, because it
     * would need to evaluate each default member initializer
     * (`maxParticles = 1000000`, `enableSorting = true`, ...) in a
     * non-complete-class context. Splitting into an explicit no-arg
     * overload (defined in ParticleSystem.cpp) and a full-arg
     * overload moves the `Config{}` construction to a site where the
     * enclosing class is already complete. See Terrain.hpp for the
     * full analysis of this clang 21 behaviour.
     */
    explicit ParticleSystem(const CudaContext& context);
    ParticleSystem(const CudaContext& context, const Config& config);

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
     * @brief Select which scalar noise backs the curl field.
     *
     * @param mode Perlin (legacy, grid-banding visible) or Simplex (isotropic,
     *             Gustavson 2012 tetrahedral lattice — see
     *             engine/cuda/particles/SimplexNoise.hpp).
     *
     * WHY split from setTurbulence(): callers upgrading an existing emitter to
     * simplex should not need to know or re-supply its strength / frequency.
     * Splitting the mode knob from the amplitude knob keeps migrations to a
     * single line of code.
     */
    void setTurbulenceNoiseMode(TurbulenceNoiseMode mode);

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
        // Device pointer to the position one simulation step ago. Consumed by
        // the ribbon-trail pipeline as the tail endpoint of each per-particle
        // billboard quad — (positions[i], prevPositions[i]) forms the line
        // segment the strip wraps around. Always non-null (the buffer is
        // allocated alongside positions in initializeBuffers).
        const float3* prevPositions;
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
     * @brief Launch the ribbon-trail strip-builder kernel.
     *
     * Writes four `ribbon_device::RibbonVertex` corners per particle slot
     * into `outDeviceVertices`, filling exactly
     * `getMaxParticles() * ribbon_device::kVerticesPerParticle` entries. The
     * caller owns the output buffer (typically a Vulkan-CUDA-interop VkBuffer
     * mapped to device memory) and is responsible for its lifetime and for
     * constructing the static index buffer via
     * `ribbon_device::FillRibbonIndexBufferCPU` once at pipeline-init.
     *
     * Degenerate slots (indices beyond the live count, or those whose
     * `alive[i]==0`, or whose motion/view frame collapses) emit four
     * coincident zero-alpha vertices that the rasterizer early-culls — the
     * renderer can safely draw every slot in one `vkCmdDrawIndexed` call
     * without a compaction pass of its own.
     *
     * @param outDeviceVertices  Device pointer to a writable
     *        RibbonVertex buffer of size
     *        `ribbon_device::RibbonVertexBufferSize(getMaxParticles())`.
     * @param viewDirection      Unit camera-forward vector. Caller normalises
     *        once per frame and passes the result; the kernel skips the
     *        re-normalise for perf and to surface upstream zero-viewDir
     *        bugs loudly rather than masking them.
     * @param tailWidthFactor    Ribbon taper ratio at the tail (0 = tapers
     *        to a point, 1 = constant width, >1 = wider at tail). Default
     *        matches the host `ribbon::DefaultStripParams`.
     *
     * WHY this lives on ParticleSystem (not on the renderer): the kernel
     * reads every SoA column (positions, prevPositions, colors, sizes,
     * lifetimes, maxLifetimes, alive), so the owner of those buffers is
     * the correct issuer. The renderer only supplies the output VkBuffer
     * and the viewDir — all other inputs come from the particle system's
     * private state.
     */
    void buildRibbonStrip(void* outDeviceVertices,
                          const Engine::vec3& viewDirection,
                          float tailWidthFactor = 0.0f);

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
    // Position one simulation step ago — the ribbon-trail renderer derives the
    // per-particle tangent direction from (current - prev). Added as a full
    // SoA column (not a scratch buffer) so that the compaction gather and the
    // depth sort permutation move it in lockstep with every other column —
    // forgetting to permute this array would silently break the prev/current
    // correspondence for every surviving particle. See GpuParticles's
    // `prevPositions` field and ENGINE_PROGRESS.md 2026-04-24 iteration 2.
    CudaBuffer<float3> m_prevPositions;
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

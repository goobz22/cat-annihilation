#pragma once

#include <cuda_runtime.h>
#include <curand_kernel.h>
#include "ParticleKernels.cuh"

namespace CatEngine {
namespace CUDA {

/**
 * @brief Elemental particle effect types
 */
enum class ElementalEffectType {
    Water,      // Flowing blue particles
    Air,        // Swirling white particles
    Earth,      // Brown/green debris particles
    Fire        // Orange/red flame particles
};

/**
 * @brief Parameters for elemental particle effects
 */
struct ElementalParticleParams {
    // Base particle parameters
    float3 position;
    float3 velocity;
    float emissionRate;

    // Elemental type
    ElementalEffectType effectType;

    // Effect intensity (0.0 to 1.0)
    float intensity;

    // Effect size multiplier
    float sizeMult;

    // Effect lifetime
    float lifetime;

    // Animation time
    float time;

    // Custom parameters per element
    union {
        // Water
        struct {
            float flowSpeed;
            float waveFrequency;
            float waveAmplitude;
        } water;

        // Air
        struct {
            float swirlSpeed;
            float swirlRadius;
            float turbulence;
        } air;

        // Earth
        struct {
            float3 gravityDir;
            float rotationSpeed;
            float debrisSize;
        } earth;

        // Fire
        struct {
            float heatDistortion;
            float flameCurl;
            float emberCount;
        } fire;
    };
};

// ============================================================================
// Kernel Declarations
// ============================================================================

/**
 * @brief Emit water particles with flowing behavior
 *
 * Creates smooth, flowing blue particles that follow wave patterns
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param startIndex Starting particle index
 * @param count Number of particles to emit
 * @param randStates Random number generator states
 */
__global__ void emitWaterParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
);

/**
 * @brief Emit air particles with swirling behavior
 *
 * Creates white, swirling particles that form vortex patterns
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param startIndex Starting particle index
 * @param count Number of particles to emit
 * @param randStates Random number generator states
 */
__global__ void emitAirParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
);

/**
 * @brief Emit earth particles with debris behavior
 *
 * Creates brown/green rocky particles with gravity and rotation
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param startIndex Starting particle index
 * @param count Number of particles to emit
 * @param randStates Random number generator states
 */
__global__ void emitEarthParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
);

/**
 * @brief Emit fire particles with flame behavior
 *
 * Creates orange/red particles that rise and flicker like flames
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param startIndex Starting particle index
 * @param count Number of particles to emit
 * @param randStates Random number generator states
 */
__global__ void emitFireParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
);

/**
 * @brief Update water particles with wave motion
 *
 * Applies flowing wave patterns to water particles
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param deltaTime Time step
 */
__global__ void updateWaterParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
);

/**
 * @brief Update air particles with vortex motion
 *
 * Applies swirling vortex motion to air particles
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param deltaTime Time step
 */
__global__ void updateAirParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
);

/**
 * @brief Update earth particles with gravity and rotation
 *
 * Applies gravity and rotation to earth debris particles
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param deltaTime Time step
 */
__global__ void updateEarthParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
);

/**
 * @brief Update fire particles with heat rise and flicker
 *
 * Applies upward heat motion and flickering to fire particles
 *
 * @param particles GPU particle buffers
 * @param params Elemental effect parameters
 * @param deltaTime Time step
 */
__global__ void updateFireParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
);

// ============================================================================
// Device Helper Functions
// ============================================================================

/**
 * @brief Generate water color based on lifetime
 */
__device__ float4 getWaterColor(float lifetimeNorm, float intensity);

/**
 * @brief Generate air color based on lifetime
 */
__device__ float4 getAirColor(float lifetimeNorm, float intensity);

/**
 * @brief Generate earth color based on lifetime
 */
__device__ float4 getEarthColor(float lifetimeNorm, float intensity);

/**
 * @brief Generate fire color based on lifetime
 */
__device__ float4 getFireColor(float lifetimeNorm, float intensity);

/**
 * @brief Apply wave motion to particle position
 */
__device__ float3 applyWaveMotion(float3 pos, float time, float frequency, float amplitude);

/**
 * @brief Apply vortex motion to particle position
 */
__device__ float3 applyVortexMotion(float3 pos, float3 center, float radius, float speed, float time);

/**
 * @brief Apply heat rise to fire particles
 */
__device__ float3 applyHeatRise(float3 pos, float3 vel, float time, float intensity);

/**
 * @brief Rotate vector around axis
 */
__device__ float3 rotateAroundAxis(float3 vec, float3 axis, float angle);

/**
 * @brief Smooth interpolation function
 */
__device__ inline float smoothstep(float edge0, float edge1, float x) {
    float t = fminf(fmaxf((x - edge0) / (edge1 - edge0), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief Generate procedural noise for particle variation
 */
__device__ float elementalNoise(float3 p, float time);

/**
 * @brief Generate turbulent noise for air effects
 */
__device__ float3 turbulentNoise(float3 p, float time, float strength);

} // namespace CUDA
} // namespace CatEngine

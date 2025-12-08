#pragma once

#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <cstdint>

namespace CatEngine {
namespace CUDA {

/**
 * @brief GPU particle data in Structure-of-Arrays layout for optimal memory access
 */
struct GpuParticles {
    float3* positions;      // Current positions
    float3* velocities;     // Current velocities
    float4* colors;         // RGBA colors
    float* lifetimes;       // Current lifetime remaining
    float* maxLifetimes;    // Initial lifetime (for normalization)
    float* sizes;           // Current size
    float* rotations;       // Billboard rotation in radians
    uint32_t* alive;        // Alive flag (0 or 1)

    int count;              // Current number of active particles
    int maxCount;           // Maximum capacity
};

/**
 * @brief GPU emitter parameters (simplified for kernel consumption)
 */
struct GpuEmitterParams {
    // Transform
    float3 position;
    float3 rotation;
    float3 scale;

    // Shape
    int shapeType;          // 0=Point, 1=Sphere, 2=Cone, 3=Box, 4=Disk

    // Shape params
    float sphereRadius;
    bool sphereEmitFromShell;
    float coneAngle;
    float coneRadius;
    float coneLength;
    float3 coneDirection;
    float3 boxExtents;
    float diskRadius;
    float diskInnerRadius;
    float3 diskNormal;

    // Initial properties
    float3 velocityMin;
    float3 velocityMax;
    float lifetimeMin;
    float lifetimeMax;
    float sizeMin;
    float sizeMax;
    float rotationMin;
    float rotationMax;
    float4 colorBase;
    float4 colorVariation;

    // Behavior
    bool fadeOutAlpha;
    bool scaleOverLifetime;
    float endScale;
    float velocityDamping;
};

/**
 * @brief Force parameters for particle simulation
 */
struct ForceParams {
    // Gravity
    float3 gravity;

    // Wind
    float3 windDirection;
    float windStrength;

    // Turbulence (curl noise)
    bool turbulenceEnabled;
    float turbulenceStrength;
    float turbulenceFrequency;
    float turbulenceOctaves;
    float turbulenceTime;       // For animation

    // Point attractors/repulsors
    int attractorCount;
    float3* attractorPositions;
    float* attractorStrengths;  // Positive = attract, negative = repel
    float* attractorRadii;
};

// ============================================================================
// Kernel Declarations
// ============================================================================

/**
 * @brief Initialize random states for particles
 */
__global__ void initRandomStates(
    curandState* states,
    unsigned long long seed,
    int count
);

/**
 * @brief Emit new particles from an emitter
 *
 * @param particles GPU particle buffers
 * @param emitter Emitter parameters
 * @param startIndex Index where to start writing particles
 * @param count Number of particles to emit
 * @param randStates Random number generator states
 */
__global__ void emitParticles(
    GpuParticles particles,
    GpuEmitterParams emitter,
    int startIndex,
    int count,
    curandState* randStates
);

/**
 * @brief Update particle physics (position, velocity, lifetime)
 *
 * @param particles GPU particle buffers
 * @param forces Force parameters
 * @param deltaTime Time step in seconds
 */
__global__ void updateParticles(
    GpuParticles particles,
    ForceParams forces,
    float deltaTime
);

/**
 * @brief Kill particles that have expired (lifetime <= 0)
 *
 * @param particles GPU particle buffers
 */
__global__ void killParticles(
    GpuParticles particles
);

/**
 * @brief Compute particle depth for sorting (distance from camera)
 *
 * @param particles GPU particle buffers
 * @param cameraPosition Camera position for depth calculation
 * @param depths Output buffer for depths
 */
__global__ void computeParticleDepths(
    GpuParticles particles,
    float3 cameraPosition,
    float* depths
);

/**
 * @brief Compact particles using a prefix sum (removes dead particles)
 *
 * @param particles GPU particle buffers
 * @param compactedCount Output: number of live particles after compaction
 * @param indices Work buffer for indices
 */
void compactParticles(
    GpuParticles& particles,
    int& compactedCount,
    cudaStream_t stream = 0
);

/**
 * @brief Sort particles by depth for alpha blending
 *
 * @param particles GPU particle buffers
 * @param depths Depth values
 * @param stream CUDA stream
 */
void sortParticles(
    GpuParticles& particles,
    float* depths,
    cudaStream_t stream = 0
);

// ============================================================================
// Device helper functions
// ============================================================================

/**
 * @brief Device function: Generate random float in range [min, max]
 */
__device__ inline float randomRange(curandState* state, float min, float max) {
    return min + curand_uniform(state) * (max - min);
}

/**
 * @brief Device function: Generate random float3 in range
 */
__device__ inline float3 randomRange3(curandState* state, float3 min, float3 max) {
    return make_float3(
        randomRange(state, min.x, max.x),
        randomRange(state, min.y, max.y),
        randomRange(state, min.z, max.z)
    );
}

/**
 * @brief Device function: Generate random float4 in range
 */
__device__ inline float4 randomRange4(curandState* state, float4 min, float4 max) {
    return make_float4(
        randomRange(state, min.x, max.x),
        randomRange(state, min.y, max.y),
        randomRange(state, min.z, max.z),
        randomRange(state, min.w, max.w)
    );
}

/**
 * @brief Device function: Curl noise for turbulence
 */
__device__ float3 curlNoise(float3 p, float frequency, float time);

/**
 * @brief Device function: 3D Perlin noise
 */
__device__ float perlinNoise3D(float3 p);

} // namespace CUDA
} // namespace CatEngine

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
    // Position at the START of the current simulation step (i.e. "where this
    // particle was last frame"). Written by updateParticles BEFORE the
    // position integration step, and by emitParticles = current position (so
    // a fresh particle contributes a zero-length, no-op segment on its first
    // frame). Consumed by RibbonTrail.hpp's camera-facing billboard kernel
    // to derive the tangent direction for the trail strip. Must be included
    // in every stream-compaction and depth-sort gather — forgetting it would
    // silently break the (prev, current) correspondence for surviving
    // particles, producing visible trail chords that chord across the
    // particle's original emit position instead of its previous frame
    // position. See ENGINE_PROGRESS.md 2026-04-24 ribbon-trail iteration 2.
    float3* prevPositions;
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
 * @brief Which underlying scalar noise function drives the curl field.
 *
 * Both modes evaluate a 3-channel vector field by sampling a scalar noise at
 * (p, p+yShift, p+zShift) — the difference is the scalar noise used.
 *
 *   Perlin  — grid-aligned hash-lerp value noise (legacy, retained for A/B
 *             comparison screenshots because its "streaks" make the grid
 *             tessellation visible at any axis-aligned camera angle).
 *   Simplex — Gustavson's 2012 simplex noise on a tetrahedral lattice
 *             (no axis-aligned cell faces, so the turbulence looks
 *             isotropic at any orientation).
 */
enum class TurbulenceNoiseMode : int {
    Perlin  = 0,  // legacy, default for backward-compat
    Simplex = 1
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

    // WHY a separate enum field not a bool: future noise functions (worley,
    // curl-of-curl, analytic divergence-free fields) can be added without
    // breaking the two-mode ABI — callers that set `Perlin` or `Simplex`
    // explicitly will still work unchanged.
    TurbulenceNoiseMode turbulenceNoiseMode;

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
 * @brief Device function: Curl noise for turbulence.
 *
 * Samples a divergence-free vector field via numerical curl of a scalar noise
 * function. The underlying scalar is selected by `mode` — see
 * `TurbulenceNoiseMode` above for the rationale.
 */
__device__ float3 curlNoise(float3 p, float frequency, float time,
                            TurbulenceNoiseMode mode);

/**
 * @brief Device function: grid-aligned 3D value noise (historically called
 *        "perlin" in this codebase, but the implementation is trilinearly-
 *        lerped hash values — strictly speaking a variant of value noise).
 */
__device__ float perlinNoise3D(float3 p);

/**
 * @brief Device function: 3D simplex noise (Gustavson 2012).
 *        See engine/cuda/particles/SimplexNoise.hpp for the reference
 *        implementation shared with the Catch2 tests.
 */
__device__ float simplexNoise3D(float3 p);

} // namespace CUDA
} // namespace CatEngine

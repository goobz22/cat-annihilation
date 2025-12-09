#include "ParticleKernels.cuh"
#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <thrust/device_ptr.h>
#include <thrust/sort.h>
#include <thrust/remove.h>
#include <thrust/count.h>
#include <thrust/execution_policy.h>
#include <cmath>

namespace CatEngine {
namespace CUDA {

// ============================================================================
// Constants
// ============================================================================

constexpr int BLOCK_SIZE = 256;
constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;

// ============================================================================
// Device Helper Functions
// ============================================================================

__device__ inline float3 operator+(const float3& a, const float3& b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ inline float3 operator-(const float3& a, const float3& b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ inline float3 operator*(const float3& a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

__device__ inline float3 operator/(const float3& a, float s) {
    return make_float3(a.x / s, a.y / s, a.z / s);
}

__device__ inline float4 operator+(const float4& a, const float4& b) {
    return make_float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

__device__ inline float4 operator*(const float4& a, float s) {
    return make_float4(a.x * s, a.y * s, a.z * s, a.w * s);
}

__device__ inline float length(const float3& v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

__device__ inline float3 normalize(const float3& v) {
    float len = length(v);
    return len > 1e-6f ? v / len : make_float3(0.0f, 0.0f, 0.0f);
}

__device__ inline float dot(const float3& a, const float3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ inline float3 cross(const float3& a, const float3& b) {
    return make_float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

__device__ inline float clamp(float x, float min, float max) {
    return fmaxf(min, fminf(max, x));
}

__device__ inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// ============================================================================
// Noise Functions
// ============================================================================

__device__ inline float fract(float x) {
    return x - floorf(x);
}

__device__ inline float hash(float n) {
    return fract(sinf(n) * 43758.5453f);
}

__device__ float noise3D(float3 p) {
    float3 pi = make_float3(floorf(p.x), floorf(p.y), floorf(p.z));
    float3 pf = make_float3(fract(p.x), fract(p.y), fract(p.z));

    // Fade curves
    pf.x = pf.x * pf.x * (3.0f - 2.0f * pf.x);
    pf.y = pf.y * pf.y * (3.0f - 2.0f * pf.y);
    pf.z = pf.z * pf.z * (3.0f - 2.0f * pf.z);

    float n = pi.x + pi.y * 157.0f + 113.0f * pi.z;

    return lerp(
        lerp(
            lerp(hash(n + 0.0f), hash(n + 1.0f), pf.x),
            lerp(hash(n + 157.0f), hash(n + 158.0f), pf.x),
            pf.y),
        lerp(
            lerp(hash(n + 113.0f), hash(n + 114.0f), pf.x),
            lerp(hash(n + 270.0f), hash(n + 271.0f), pf.x),
            pf.y),
        pf.z);
}

__device__ float perlinNoise3D(float3 p) {
    return noise3D(p) * 2.0f - 1.0f;
}

__device__ float3 curlNoise(float3 p, float frequency, float time) {
    // Curl noise using numerical derivatives of 3D noise
    float eps = 0.01f;
    float3 fp = p * frequency + make_float3(time, time, time);

    // Sample noise at neighboring points
    float3 dx = make_float3(eps, 0.0f, 0.0f);
    float3 dy = make_float3(0.0f, eps, 0.0f);
    float3 dz = make_float3(0.0f, 0.0f, eps);

    // Compute partial derivatives
    float dFy_dx = (perlinNoise3D(fp + dx) - perlinNoise3D(fp - dx)) / (2.0f * eps);
    float dFz_dx = (perlinNoise3D(fp + dx + dz) - perlinNoise3D(fp - dx + dz)) / (2.0f * eps);

    float dFx_dy = (perlinNoise3D(fp + dy) - perlinNoise3D(fp - dy)) / (2.0f * eps);
    float dFz_dy = (perlinNoise3D(fp + dy + dz) - perlinNoise3D(fp - dy + dz)) / (2.0f * eps);

    float dFx_dz = (perlinNoise3D(fp + dz) - perlinNoise3D(fp - dz)) / (2.0f * eps);
    float dFy_dz = (perlinNoise3D(fp + dy + dz) - perlinNoise3D(fp - dy + dz)) / (2.0f * eps);

    // Curl = (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy)
    return make_float3(
        dFz_dy - dFy_dz,
        dFx_dz - dFz_dx,
        dFy_dx - dFx_dy
    );
}

// ============================================================================
// Random State Initialization
// ============================================================================

__global__ void initRandomStates(
    curandState* states,
    unsigned long long seed,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    curand_init(seed, idx, 0, &states[idx]);
}

// ============================================================================
// Particle Emission
// ============================================================================

__device__ float3 sampleEmissionShape(
    const GpuEmitterParams& emitter,
    curandState* randState
) {
    float3 pos = make_float3(0.0f, 0.0f, 0.0f);

    switch (emitter.shapeType) {
        case 0: // Point
            pos = make_float3(0.0f, 0.0f, 0.0f);
            break;

        case 1: { // Sphere
            float theta = curand_uniform(randState) * 2.0f * PI;
            float phi = acosf(2.0f * curand_uniform(randState) - 1.0f);
            float r = emitter.sphereRadius;

            if (!emitter.sphereEmitFromShell) {
                r *= cbrtf(curand_uniform(randState)); // Uniform volume distribution
            }

            pos = make_float3(
                r * sinf(phi) * cosf(theta),
                r * sinf(phi) * sinf(theta),
                r * cosf(phi)
            );
            break;
        }

        case 2: { // Cone
            float angle = curand_uniform(randState) * emitter.coneAngle * DEG_TO_RAD;
            float theta = curand_uniform(randState) * 2.0f * PI;
            float dist = curand_uniform(randState) * emitter.coneLength;

            float radius = tanf(angle) * dist;
            pos = make_float3(
                radius * cosf(theta),
                dist,
                radius * sinf(theta)
            );
            break;
        }

        case 3: { // Box
            pos = make_float3(
                (curand_uniform(randState) - 0.5f) * emitter.boxExtents.x,
                (curand_uniform(randState) - 0.5f) * emitter.boxExtents.y,
                (curand_uniform(randState) - 0.5f) * emitter.boxExtents.z
            );
            break;
        }

        case 4: { // Disk
            float r = curand_uniform(randState);
            r = sqrtf(lerp(
                emitter.diskInnerRadius * emitter.diskInnerRadius,
                emitter.diskRadius * emitter.diskRadius,
                r
            ));
            float theta = curand_uniform(randState) * 2.0f * PI;

            pos = make_float3(
                r * cosf(theta),
                0.0f,
                r * sinf(theta)
            );
            break;
        }
    }

    return pos;
}

__global__ void emitParticles(
    GpuParticles particles,
    GpuEmitterParams emitter,
    int startIndex,
    int count,
    curandState* randStates
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    int particleIdx = startIndex + idx;
    if (particleIdx >= particles.maxCount) return;

    curandState localState = randStates[idx % 1024]; // Reuse states in batches

    // Sample emission shape
    float3 localPos = sampleEmissionShape(emitter, &localState);

    // Transform to world space
    particles.positions[particleIdx] = localPos + emitter.position;

    // Initial velocity
    particles.velocities[particleIdx] = randomRange3(
        &localState,
        emitter.velocityMin,
        emitter.velocityMax
    );

    // Initial lifetime
    float lifetime = randomRange(&localState, emitter.lifetimeMin, emitter.lifetimeMax);
    particles.lifetimes[particleIdx] = lifetime;
    particles.maxLifetimes[particleIdx] = lifetime;

    // Initial size
    particles.sizes[particleIdx] = randomRange(
        &localState,
        emitter.sizeMin,
        emitter.sizeMax
    );

    // Initial rotation
    particles.rotations[particleIdx] = randomRange(
        &localState,
        emitter.rotationMin,
        emitter.rotationMax
    ) * DEG_TO_RAD;

    // Initial color
    float4 colorVariation = randomRange4(
        &localState,
        make_float4(0.0f, 0.0f, 0.0f, 0.0f),
        emitter.colorVariation
    );
    particles.colors[particleIdx] = emitter.colorBase + colorVariation;

    // Mark as alive
    particles.alive[particleIdx] = 1;

    // Update random state
    randStates[idx % 1024] = localState;
}

// ============================================================================
// Particle Update
// ============================================================================

__global__ void updateParticles(
    GpuParticles particles,
    ForceParams forces,
    float deltaTime
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= particles.count) return;
    if (particles.alive[idx] == 0) return;

    float3 pos = particles.positions[idx];
    float3 vel = particles.velocities[idx];
    float lifetime = particles.lifetimes[idx];
    float maxLifetime = particles.maxLifetimes[idx];

    // Apply forces
    float3 acceleration = make_float3(0.0f, 0.0f, 0.0f);

    // Gravity
    acceleration = acceleration + forces.gravity;

    // Wind
    acceleration = acceleration + forces.windDirection * forces.windStrength;

    // Turbulence (curl noise)
    if (forces.turbulenceEnabled) {
        float3 turbulence = curlNoise(
            pos,
            forces.turbulenceFrequency,
            forces.turbulenceTime
        );
        acceleration = acceleration + turbulence * forces.turbulenceStrength;
    }

    // Point attractors/repulsors
    for (int i = 0; i < forces.attractorCount; ++i) {
        float3 toAttractor = forces.attractorPositions[i] - pos;
        float dist = length(toAttractor);

        if (dist > 0.001f && dist < forces.attractorRadii[i]) {
            float strength = forces.attractorStrengths[i];
            float falloff = 1.0f - (dist / forces.attractorRadii[i]);
            falloff = falloff * falloff; // Quadratic falloff

            float3 force = normalize(toAttractor) * strength * falloff;
            acceleration = acceleration + force;
        }
    }

    // Integrate velocity
    vel = vel + acceleration * deltaTime;

    // Apply damping (simple exponential decay)
    // vel = vel * (1.0f - forces.damping * deltaTime);

    // Integrate position
    pos = pos + vel * deltaTime;

    // Update lifetime
    lifetime -= deltaTime;

    // Lifetime-based effects
    float lifetimeRatio = clamp(lifetime / maxLifetime, 0.0f, 1.0f);

    // Fade out alpha
    float4 color = particles.colors[idx];
    color.w *= lifetimeRatio; // Fade alpha

    // Scale over lifetime (optional, controlled by emitter)
    // float size = particles.sizes[idx];
    // size = lerp(initialSize, endSize, 1.0f - lifetimeRatio);

    // Write back
    particles.positions[idx] = pos;
    particles.velocities[idx] = vel;
    particles.lifetimes[idx] = lifetime;
    particles.colors[idx] = color;
}

// ============================================================================
// Particle Killing
// ============================================================================

__global__ void killParticles(GpuParticles particles) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= particles.count) return;

    if (particles.lifetimes[idx] <= 0.0f) {
        particles.alive[idx] = 0;
    }
}

// ============================================================================
// Depth Computation
// ============================================================================

__global__ void computeParticleDepths(
    GpuParticles particles,
    float3 cameraPosition,
    float* depths
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= particles.count) return;

    if (particles.alive[idx] == 0) {
        depths[idx] = 1e10f; // Push dead particles to back
        return;
    }

    float3 toCamera = particles.positions[idx] - cameraPosition;
    depths[idx] = dot(toCamera, toCamera); // Squared distance (faster than sqrt)
}

// ============================================================================
// Compaction (using Thrust)
// ============================================================================

struct IsAlive {
    __device__ bool operator()(uint32_t alive) const {
        return alive != 0;
    }
};

void compactParticles(
    GpuParticles& particles,
    int& compactedCount,
    cudaStream_t stream
) {
    // Use Thrust to compact particles
    // This is a simplified version - a production system would use
    // stream compaction on all arrays simultaneously

    thrust::device_ptr<uint32_t> alivePtr(particles.alive);
    thrust::device_ptr<float3> posPtr(particles.positions);
    thrust::device_ptr<float3> velPtr(particles.velocities);
    thrust::device_ptr<float4> colorPtr(particles.colors);
    thrust::device_ptr<float> lifetimePtr(particles.lifetimes);
    thrust::device_ptr<float> maxLifetimePtr(particles.maxLifetimes);
    thrust::device_ptr<float> sizePtr(particles.sizes);
    thrust::device_ptr<float> rotationPtr(particles.rotations);

    // Count alive particles
    compactedCount = thrust::count_if(
        thrust::cuda::par.on(stream),
        alivePtr,
        alivePtr + particles.count,
        IsAlive()
    );

    // In a real implementation, we'd use stream compaction to remove dead particles
    // and pack the arrays. For now, this is left as an exercise.
    // A proper implementation would use:
    // - Thrust copy_if with a zip_iterator
    // - Or a custom CUDA kernel with prefix sum
}

// ============================================================================
// Sorting (using Thrust)
// ============================================================================

void sortParticles(
    GpuParticles& particles,
    float* depths,
    cudaStream_t stream
) {
    // Sort particles by depth (back-to-front for alpha blending)
    // This is a simplified version - a production system would use
    // a permutation array and apply it to all particle data

    thrust::device_ptr<float> depthPtr(depths);

    // Sort indices by depth
    // In a real implementation, we'd create an index array and sort that,
    // then permute all particle arrays using the sorted indices.
    // For now, we just sort depths directly.

    thrust::sort(
        thrust::cuda::par.on(stream),
        depthPtr,
        depthPtr + particles.count,
        thrust::greater<float>() // Back-to-front
    );

    // Note: A complete implementation would use thrust::sort_by_key
    // with a zip_iterator to sort all particle data arrays together
}

} // namespace CUDA
} // namespace CatEngine

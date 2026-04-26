#include "ParticleKernels.cuh"
#include "SimplexNoise.hpp"  // Header-only, __host__ __device__ simplex noise
#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <thrust/device_ptr.h>
#include <thrust/device_vector.h>
#include <thrust/sort.h>
#include <thrust/remove.h>
#include <thrust/count.h>
#include <thrust/copy.h>
#include <thrust/gather.h>
#include <thrust/sequence.h>
#include <thrust/execution_policy.h>
#include <thrust/functional.h>
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

__device__ float simplexNoise3D(float3 p) {
    // Thin CUDA-side wrapper around the host/device simplex reference. Kept
    // as a __device__ function (not inline-forwarded) so the kernel callsite
    // stays identical in structure to the perlinNoise3D path — easier to
    // diff and profile the two paths side by side.
    return ::CatEngine::CUDA::noise::Simplex3D(p.x, p.y, p.z);
}

// Evaluate the curl-noise scalar field at a single sample site for the
// chosen noise mode. Pulled out so the six-point numerical curl stencil
// below stays readable — otherwise each row would be a ternary expression
// with three-argument make_float3 nesting, which hides the stencil geometry.
__device__ inline float sampleCurlNoiseScalar(float3 p, TurbulenceNoiseMode mode) {
    // WHY a switch here rather than a function pointer: CUDA device code can
    // do cheap branch prediction on a scalar enum, but function pointers to
    // __device__ functions require the address of a separately-linked
    // __device__ symbol — slower and a compile-time complication on older
    // CUDA toolchains. The branch lives inside the inlined helper and the
    // compiler lifts the switch out of the inner stencil loop via CSE.
    switch (mode) {
        case TurbulenceNoiseMode::Simplex: return simplexNoise3D(p);
        case TurbulenceNoiseMode::Perlin:
        default:                           return perlinNoise3D(p);
    }
}

__device__ float3 curlNoise(float3 p, float frequency, float time,
                            TurbulenceNoiseMode mode) {
    // Numerical curl of a scalar potential field sampled at neighbouring
    // points. The field here is a single scalar, so we evaluate it at three
    // offset "phases" of the input — the three-way hash-shift is Bridson's
    // 2007 trick to get three statistically-independent scalar fields out
    // of one noise function without allocating three separate noises.
    constexpr float eps = 0.01f;
    const float3 fp = p * frequency + make_float3(time, time, time);

    // Bridson 2007 phase offsets. These must be large enough (>> `eps`) that
    // the three noise samples are de-correlated, but small enough that the
    // three derived channels stay within the noise function's input range
    // where its amplitude is well-defined. 31/67/103 are primes chosen so
    // their differences don't land near the simplex skew factor (1/3, 1/6).
    const float3 offY = make_float3(31.416f, 0.0f, 0.0f);
    const float3 offZ = make_float3(0.0f, 47.853f, 0.0f);

    const float3 dx = make_float3(eps, 0.0f, 0.0f);
    const float3 dy = make_float3(0.0f, eps, 0.0f);
    const float3 dz = make_float3(0.0f, 0.0f, eps);

    // Partial derivatives by central differences. The same six sample sites
    // are reused across the three curl components — the compiler's CSE will
    // hoist identical sampleCurlNoiseScalar calls out.
    // Channel 0 (Fx): lives at fp.
    // Channel 1 (Fy): lives at fp + offY.
    // Channel 2 (Fz): lives at fp + offZ.
    const float dFy_dx = (sampleCurlNoiseScalar(fp + offY + dx, mode) -
                          sampleCurlNoiseScalar(fp + offY - dx, mode)) / (2.0f * eps);
    const float dFz_dx = (sampleCurlNoiseScalar(fp + offZ + dx, mode) -
                          sampleCurlNoiseScalar(fp + offZ - dx, mode)) / (2.0f * eps);

    const float dFx_dy = (sampleCurlNoiseScalar(fp + dy, mode) -
                          sampleCurlNoiseScalar(fp - dy, mode)) / (2.0f * eps);
    const float dFz_dy = (sampleCurlNoiseScalar(fp + offZ + dy, mode) -
                          sampleCurlNoiseScalar(fp + offZ - dy, mode)) / (2.0f * eps);

    const float dFx_dz = (sampleCurlNoiseScalar(fp + dz, mode) -
                          sampleCurlNoiseScalar(fp - dz, mode)) / (2.0f * eps);
    const float dFy_dz = (sampleCurlNoiseScalar(fp + offY + dz, mode) -
                          sampleCurlNoiseScalar(fp + offY - dz, mode)) / (2.0f * eps);

    // Curl = (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy). This
    // produces a numerically divergence-free vector field: particles
    // advected by it neither compress nor expand, which prevents the
    // "swirl, then all clump in one spot" failure mode of raw noise
    // advection.
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
    const float3 worldPos = localPos + emitter.position;
    particles.positions[particleIdx] = worldPos;

    // Seed the ribbon-trail tangent with a zero-length segment. If we left
    // prevPositions uninitialised, the first frame's ribbon would stretch
    // from (wherever the slot's old corpse was) to this brand-new particle's
    // world position — a visible rubber-banding artifact on every emit. By
    // writing prev = current at birth the first-frame segment is length 0
    // and the ribbon renderer short-circuits it as a degenerate quad (see
    // BuildRibbonStrip in RibbonTrail.hpp, which skips sub-epsilon motion
    // without bridging). Cost: one coalesced float3 write per emitted
    // particle, negligible against curand_uniform calls above.
    particles.prevPositions[particleIdx] = worldPos;

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

    // Snapshot the position at the START of this simulation step — BEFORE any
    // integration. This is what the ribbon-trail renderer needs as the tail
    // endpoint: (prev, current) must bracket exactly one frame of motion.
    // Writing later (after `pos = pos + vel * dt`) would collapse prev and
    // current to the same post-integration point and the ribbon tangent would
    // be zero every frame.
    //
    // Alternative considered: a dedicated pre-integration kernel that only
    // copies positions -> prevPositions. Rejected because it would be a
    // memory-bound pass duplicating the same-particle indexing this kernel
    // already pays for — doing the write here reuses the warp's cached pos
    // load and adds exactly one coalesced store per live particle.
    particles.prevPositions[idx] = pos;

    // Apply forces
    float3 acceleration = make_float3(0.0f, 0.0f, 0.0f);

    // Gravity
    acceleration = acceleration + forces.gravity;

    // Wind
    acceleration = acceleration + forces.windDirection * forces.windStrength;

    // Turbulence (curl noise). Mode branch happens inside curlNoise so the
    // six-point stencil geometry stays in one place. The compiler will
    // constant-fold when the enum value is uniform across a warp, which is
    // the common case — ForceParams::turbulenceNoiseMode is set by the
    // emitter config, not per-particle.
    if (forces.turbulenceEnabled) {
        float3 turbulence = curlNoise(
            pos,
            forces.turbulenceFrequency,
            forces.turbulenceTime,
            forces.turbulenceNoiseMode
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
// Compaction — full stream compaction across all particle SoA arrays
// ============================================================================

struct IsAlive {
    __device__ bool operator()(uint32_t alive) const {
        return alive != 0;
    }
};

namespace {

// Permute every particle SoA array using the supplied sorted/compacted index
// list. Uses stream-ordered memory allocation (cudaMallocAsync / cudaFreeAsync,
// CUDA 11.2+) so the temporary buffers are allocated, filled, copied back, and
// freed entirely on the caller's stream — no host-side synchronization, no
// cross-stream serialization. The particle system schedules compaction every
// `compactionFrequency` frames (default 60) so the allocator churn is
// amortized across many simulation steps.
//
// IMPORTANT: the alive flag array IS included in the gather. A prior version
// skipped alive and then tried to fix it with cudaMemsetAsync(alive, 0x01, ...)
// — that set every byte to 0x01, producing uint32 values of 0x01010101
// instead of a canonical 1. Truthy checks still worked, but any code that
// compared `alive[i] == 1` silently broke. Gathering alive directly keeps the
// invariant "after compaction, alive[0..count] are the exact alive values from
// the source indices (all 1 by definition of copy_if's predicate)."
void permuteParticleArrays(
    GpuParticles& particles,
    const thrust::device_ptr<const uint32_t>& indices,
    int count,
    cudaStream_t stream
) {
    if (count <= 0) return;

    const size_t f3Bytes = static_cast<size_t>(count) * sizeof(float3);
    const size_t f4Bytes = static_cast<size_t>(count) * sizeof(float4);
    const size_t f1Bytes = static_cast<size_t>(count) * sizeof(float);
    const size_t u32Bytes = static_cast<size_t>(count) * sizeof(uint32_t);

    float3*   tmpPos          = nullptr;
    // prevPositions is a first-class SoA column and MUST ride the same
    // permutation as positions. If this gather were skipped, a compaction
    // pass that moved particle i from source slot s to destination slot d
    // would pair its positions[d] (from s) with prevPositions[d] (from
    // whatever dead particle was at d before) — producing a ribbon segment
    // that chords from the corpse's last frame to the survivor's current
    // frame. Visually: long flickering trails to the origin of the arena
    // every compaction frame. This is exactly the class of bug the comment
    // above the old permuteParticleArrays warned about (for alive), now
    // extended to prevPositions.
    float3*   tmpPrevPos      = nullptr;
    float3*   tmpVel          = nullptr;
    float4*   tmpColor        = nullptr;
    float*    tmpLifetime     = nullptr;
    float*    tmpMaxLifetime  = nullptr;
    float*    tmpSize         = nullptr;
    float*    tmpRotation     = nullptr;
    uint32_t* tmpAlive        = nullptr;

    cudaMallocAsync(&tmpPos,          f3Bytes,  stream);
    cudaMallocAsync(&tmpPrevPos,      f3Bytes,  stream);
    cudaMallocAsync(&tmpVel,          f3Bytes,  stream);
    cudaMallocAsync(&tmpColor,        f4Bytes,  stream);
    cudaMallocAsync(&tmpLifetime,     f1Bytes,  stream);
    cudaMallocAsync(&tmpMaxLifetime,  f1Bytes,  stream);
    cudaMallocAsync(&tmpSize,         f1Bytes,  stream);
    cudaMallocAsync(&tmpRotation,     f1Bytes,  stream);
    cudaMallocAsync(&tmpAlive,        u32Bytes, stream);

    auto policy = thrust::cuda::par.on(stream);
    auto idxBegin = indices;
    auto idxEnd   = indices + count;

    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float3>(particles.positions),
        thrust::device_ptr<float3>(tmpPos));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float3>(particles.prevPositions),
        thrust::device_ptr<float3>(tmpPrevPos));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float3>(particles.velocities),
        thrust::device_ptr<float3>(tmpVel));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float4>(particles.colors),
        thrust::device_ptr<float4>(tmpColor));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float>(particles.lifetimes),
        thrust::device_ptr<float>(tmpLifetime));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float>(particles.maxLifetimes),
        thrust::device_ptr<float>(tmpMaxLifetime));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float>(particles.sizes),
        thrust::device_ptr<float>(tmpSize));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<float>(particles.rotations),
        thrust::device_ptr<float>(tmpRotation));
    thrust::gather(policy, idxBegin, idxEnd,
        thrust::device_ptr<uint32_t>(particles.alive),
        thrust::device_ptr<uint32_t>(tmpAlive));

    // Copy each gathered temp array back into the source front. All copies
    // enqueue on the same stream so they serialize correctly relative to the
    // preceding gather ops without a host synchronize.
    cudaMemcpyAsync(particles.positions,     tmpPos,     f3Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.prevPositions, tmpPrevPos, f3Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.velocities,    tmpVel,     f3Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.colors,       tmpColor,       f4Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.lifetimes,    tmpLifetime,    f1Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.maxLifetimes, tmpMaxLifetime, f1Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.sizes,        tmpSize,        f1Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.rotations,    tmpRotation,    f1Bytes,  cudaMemcpyDeviceToDevice, stream);
    cudaMemcpyAsync(particles.alive,        tmpAlive,       u32Bytes, cudaMemcpyDeviceToDevice, stream);

    // Stream-ordered frees — the driver waits for the preceding memcpy ops to
    // drain on this stream before actually reclaiming the memory, so no host
    // sync is required. Matches the allocator's lifetime contract.
    cudaFreeAsync(tmpPos,         stream);
    cudaFreeAsync(tmpPrevPos,     stream);
    cudaFreeAsync(tmpVel,         stream);
    cudaFreeAsync(tmpColor,       stream);
    cudaFreeAsync(tmpLifetime,    stream);
    cudaFreeAsync(tmpMaxLifetime, stream);
    cudaFreeAsync(tmpSize,        stream);
    cudaFreeAsync(tmpRotation,    stream);
    cudaFreeAsync(tmpAlive,       stream);
}

} // anonymous namespace

void compactParticles(
    GpuParticles& particles,
    int& compactedCount,
    cudaStream_t stream
) {
    if (particles.count <= 0) {
        compactedCount = 0;
        return;
    }

    auto policy = thrust::cuda::par.on(stream);

    // Build an index sequence [0, 1, ..., count-1] and copy only those indices
    // whose matching alive[i] is non-zero into a compact list. The resulting
    // list is the permutation that gathers alive particles to the front.
    thrust::device_vector<uint32_t> indices(particles.count);
    thrust::sequence(policy, indices.begin(), indices.end());

    thrust::device_vector<uint32_t> compactIndices(particles.count);
    thrust::device_ptr<uint32_t> aliveBegin(particles.alive);

    auto compactEnd = thrust::copy_if(
        policy,
        indices.begin(), indices.end(),
        aliveBegin,
        compactIndices.begin(),
        IsAlive()
    );

    compactedCount = static_cast<int>(compactEnd - compactIndices.begin());

    if (compactedCount == 0) {
        // All dead — zero the alive flags across the full range.
        cudaMemsetAsync(
            particles.alive,
            0,
            static_cast<size_t>(particles.count) * sizeof(uint32_t),
            stream
        );
        particles.count = 0;
        return;
    }

    if (compactedCount == particles.count) {
        // Nothing to do — every slot is alive and already dense.
        return;
    }

    // Gather every SoA array (including alive) through the compact index list
    // into temp buffers, then copy the compact result back in place.
    // permuteParticleArrays handles the temp-buffer lifecycle via stream-
    // ordered cudaMallocAsync / cudaFreeAsync — no host sync required.
    thrust::device_ptr<const uint32_t> compactPtr(
        thrust::raw_pointer_cast(compactIndices.data())
    );
    permuteParticleArrays(particles, compactPtr, compactedCount, stream);

    // Mark the tail as dead so future emission slots start in a known state.
    // The leading region [0, compactedCount) already carries the correct
    // alive=1 values from the gather (since copy_if's predicate only selected
    // indices where alive was non-zero, and gather lifted the source flags
    // verbatim).
    cudaMemsetAsync(
        particles.alive + compactedCount,
        0,
        static_cast<size_t>(particles.count - compactedCount) * sizeof(uint32_t),
        stream
    );

    particles.count = compactedCount;
}

// ============================================================================
// Sorting — sort all particle arrays by depth via an index permutation
// ============================================================================

void sortParticles(
    GpuParticles& particles,
    float* depths,
    cudaStream_t stream
) {
    if (particles.count <= 0) return;

    auto policy = thrust::cuda::par.on(stream);

    // Build an index array [0..count-1] and sort it by depth (descending for
    // back-to-front alpha blending). thrust::sort_by_key rewrites the indices
    // array so that indices[i] is the original particle index that should end
    // up at slot i after sorting.
    thrust::device_vector<uint32_t> indices(particles.count);
    thrust::sequence(policy, indices.begin(), indices.end());

    thrust::device_ptr<float> depthPtr(depths);
    thrust::sort_by_key(
        policy,
        depthPtr,
        depthPtr + particles.count,
        indices.begin(),
        thrust::greater<float>()
    );

    // Permute every particle SoA array using the sorted index list so all
    // per-particle attributes remain consistent with the depth ordering.
    thrust::device_ptr<const uint32_t> sortedPtr(
        thrust::raw_pointer_cast(indices.data())
    );
    permuteParticleArrays(particles, sortedPtr, particles.count, stream);
}

} // namespace CUDA
} // namespace CatEngine

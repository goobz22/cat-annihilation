#include "elemental_particles.cuh"
#include <cmath>

namespace CatEngine {
namespace CUDA {

// ============================================================================
// Constants
// ============================================================================

#define PI 3.14159265359f
#define TWO_PI 6.28318530718f

// ============================================================================
// Device Helper Functions - Noise
// ============================================================================

__device__ float hash(float n) {
    return fract(sinf(n) * 43758.5453f);
}

__device__ float fract(float x) {
    return x - floorf(x);
}

__device__ float elementalNoise(float3 p, float time) {
    // Simple 3D procedural noise
    float3 pt = make_float3(p.x + time * 0.1f, p.y, p.z);

    float3 i = make_float3(floorf(pt.x), floorf(pt.y), floorf(pt.z));
    float3 f = make_float3(fract(pt.x), fract(pt.y), fract(pt.z));

    float n = i.x + i.y * 57.0f + i.z * 113.0f;

    float a = hash(n + 0.0f);
    float b = hash(n + 1.0f);
    float c = hash(n + 57.0f);
    float d = hash(n + 58.0f);

    float3 u = make_float3(
        f.x * f.x * (3.0f - 2.0f * f.x),
        f.y * f.y * (3.0f - 2.0f * f.y),
        f.z * f.z * (3.0f - 2.0f * f.z)
    );

    return mix(mix(mix(a, b, u.x), mix(c, d, u.x), u.y),
               mix(mix(a, b, u.x), mix(c, d, u.x), u.y), u.z);
}

__device__ float3 turbulentNoise(float3 p, float time, float strength) {
    float nx = elementalNoise(make_float3(p.x + time, p.y, p.z), time) - 0.5f;
    float ny = elementalNoise(make_float3(p.x, p.y + time, p.z), time) - 0.5f;
    float nz = elementalNoise(make_float3(p.x, p.y, p.z + time), time) - 0.5f;

    return make_float3(nx * strength, ny * strength, nz * strength);
}

__device__ inline float mix(float a, float b, float t) {
    return a * (1.0f - t) + b * t;
}

// ============================================================================
// Device Helper Functions - Transformations
// ============================================================================

__device__ float3 applyWaveMotion(float3 pos, float time, float frequency, float amplitude) {
    float wave = sinf(pos.x * frequency + time * 2.0f) * amplitude;
    wave += sinf(pos.z * frequency * 0.7f + time * 1.5f) * amplitude * 0.5f;

    return make_float3(pos.x, pos.y + wave, pos.z);
}

__device__ float3 applyVortexMotion(float3 pos, float3 center, float radius, float speed, float time) {
    float3 toCenter = make_float3(
        pos.x - center.x,
        pos.y - center.y,
        pos.z - center.z
    );

    float dist = sqrtf(toCenter.x * toCenter.x + toCenter.y * toCenter.y + toCenter.z * toCenter.z);

    if (dist < 0.001f) return pos;

    // Calculate rotation angle based on distance and time
    float angle = (1.0f - fminf(dist / radius, 1.0f)) * speed * time;

    // Rotate around Y axis
    float cosA = cosf(angle);
    float sinA = sinf(angle);

    float3 rotated = make_float3(
        toCenter.x * cosA - toCenter.z * sinA,
        toCenter.y,
        toCenter.x * sinA + toCenter.z * cosA
    );

    return make_float3(
        center.x + rotated.x,
        center.y + rotated.y,
        center.z + rotated.z
    );
}

__device__ float3 applyHeatRise(float3 pos, float3 vel, float time, float intensity) {
    // Add upward velocity with turbulence
    float3 turbulence = turbulentNoise(pos, time, intensity * 2.0f);

    return make_float3(
        vel.x + turbulence.x,
        vel.y + intensity * 3.0f + turbulence.y,
        vel.z + turbulence.z
    );
}

__device__ float3 rotateAroundAxis(float3 vec, float3 axis, float angle) {
    float cosA = cosf(angle);
    float sinA = sinf(angle);
    float invCosA = 1.0f - cosA;

    // Normalize axis
    float len = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len > 0.001f) {
        axis.x /= len;
        axis.y /= len;
        axis.z /= len;
    }

    // Rodrigues' rotation formula
    float3 result;
    result.x = (cosA + axis.x * axis.x * invCosA) * vec.x +
               (axis.x * axis.y * invCosA - axis.z * sinA) * vec.y +
               (axis.x * axis.z * invCosA + axis.y * sinA) * vec.z;

    result.y = (axis.y * axis.x * invCosA + axis.z * sinA) * vec.x +
               (cosA + axis.y * axis.y * invCosA) * vec.y +
               (axis.y * axis.z * invCosA - axis.x * sinA) * vec.z;

    result.z = (axis.z * axis.x * invCosA - axis.y * sinA) * vec.x +
               (axis.z * axis.y * invCosA + axis.x * sinA) * vec.y +
               (cosA + axis.z * axis.z * invCosA) * vec.z;

    return result;
}

// ============================================================================
// Device Helper Functions - Colors
// ============================================================================

__device__ float4 getWaterColor(float lifetimeNorm, float intensity) {
    // Blue gradient: dark blue -> cyan -> transparent
    float alpha = 1.0f - lifetimeNorm;

    float r = mix(0.1f, 0.3f, lifetimeNorm) * intensity;
    float g = mix(0.3f, 0.7f, lifetimeNorm) * intensity;
    float b = mix(0.8f, 1.0f, 1.0f - lifetimeNorm) * intensity;

    return make_float4(r, g, b, alpha);
}

__device__ float4 getAirColor(float lifetimeNorm, float intensity) {
    // White to light blue gradient
    float alpha = (1.0f - lifetimeNorm) * 0.8f;

    float r = mix(0.9f, 1.0f, lifetimeNorm) * intensity;
    float g = mix(0.9f, 1.0f, lifetimeNorm) * intensity;
    float b = 1.0f * intensity;

    return make_float4(r, g, b, alpha);
}

__device__ float4 getEarthColor(float lifetimeNorm, float intensity) {
    // Brown to green gradient for earth/rocks
    float alpha = 1.0f - lifetimeNorm * 0.5f;

    float r = mix(0.6f, 0.4f, lifetimeNorm) * intensity;
    float g = mix(0.4f, 0.5f, lifetimeNorm) * intensity;
    float b = mix(0.2f, 0.3f, lifetimeNorm) * intensity;

    return make_float4(r, g, b, alpha);
}

__device__ float4 getFireColor(float lifetimeNorm, float intensity) {
    // Fire gradient: yellow -> orange -> red -> dark red
    float alpha = 1.0f - lifetimeNorm;

    float r = 1.0f * intensity;
    float g = mix(0.8f, 0.2f, lifetimeNorm) * intensity;
    float b = mix(0.3f, 0.0f, lifetimeNorm) * intensity;

    return make_float4(r, g, b, alpha);
}

// ============================================================================
// Emission Kernels
// ============================================================================

__global__ void emitWaterParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    int particleIdx = startIndex + idx;
    if (particleIdx >= particles.maxCount) return;

    curandState localState = randStates[idx % particles.maxCount];

    // Position with slight randomization
    float3 offset = make_float3(
        randomRange(&localState, -0.5f, 0.5f),
        randomRange(&localState, -0.2f, 0.2f),
        randomRange(&localState, -0.5f, 0.5f)
    );

    const float3 spawnPos = make_float3(
        params.position.x + offset.x,
        params.position.y + offset.y,
        params.position.z + offset.z
    );
    particles.positions[particleIdx] = spawnPos;
    // Ribbon-trail seed: zero-length initial segment at birth so the first
    // frame's trail doesn't snap from the slot's previous corpse to the new
    // emit site (see ParticleKernels.cu emitParticles for the full rationale).
    particles.prevPositions[particleIdx] = spawnPos;

    // Velocity with wave pattern
    float waveOffset = randomRange(&localState, 0.0f, TWO_PI);
    float3 waveVel = make_float3(
        sinf(params.time + waveOffset) * params.water.flowSpeed,
        randomRange(&localState, -0.5f, 0.5f),
        cosf(params.time + waveOffset) * params.water.flowSpeed
    );

    particles.velocities[particleIdx] = make_float3(
        params.velocity.x + waveVel.x,
        params.velocity.y + waveVel.y,
        params.velocity.z + waveVel.z
    );

    // Color
    particles.colors[particleIdx] = getWaterColor(0.0f, params.intensity);

    // Lifetime
    float lifetime = randomRange(&localState, 0.8f, 1.5f);
    particles.lifetimes[particleIdx] = lifetime;
    particles.maxLifetimes[particleIdx] = lifetime;

    // Size
    particles.sizes[particleIdx] = randomRange(&localState, 0.1f, 0.25f) * params.sizeMult;

    // Rotation
    particles.rotations[particleIdx] = randomRange(&localState, 0.0f, TWO_PI);

    // Mark as alive
    particles.alive[particleIdx] = 1;

    randStates[idx % particles.maxCount] = localState;
}

__global__ void emitAirParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    int particleIdx = startIndex + idx;
    if (particleIdx >= particles.maxCount) return;

    curandState localState = randStates[idx % particles.maxCount];

    // Emit in a swirl pattern
    float angle = randomRange(&localState, 0.0f, TWO_PI);
    float radius = randomRange(&localState, 0.0f, params.air.swirlRadius);

    float3 offset = make_float3(
        cosf(angle) * radius,
        randomRange(&localState, -0.5f, 0.5f),
        sinf(angle) * radius
    );

    const float3 spawnPos = make_float3(
        params.position.x + offset.x,
        params.position.y + offset.y,
        params.position.z + offset.z
    );
    particles.positions[particleIdx] = spawnPos;
    // Zero-length ribbon seed — see water emit for the rationale.
    particles.prevPositions[particleIdx] = spawnPos;

    // Swirling velocity
    float3 swirlVel = make_float3(
        -sinf(angle) * params.air.swirlSpeed,
        randomRange(&localState, 1.0f, 2.0f),
        cosf(angle) * params.air.swirlSpeed
    );

    particles.velocities[particleIdx] = make_float3(
        params.velocity.x + swirlVel.x,
        params.velocity.y + swirlVel.y,
        params.velocity.z + swirlVel.z
    );

    // Color
    particles.colors[particleIdx] = getAirColor(0.0f, params.intensity);

    // Lifetime
    float lifetime = randomRange(&localState, 0.5f, 1.2f);
    particles.lifetimes[particleIdx] = lifetime;
    particles.maxLifetimes[particleIdx] = lifetime;

    // Size
    particles.sizes[particleIdx] = randomRange(&localState, 0.05f, 0.15f) * params.sizeMult;

    // Rotation
    particles.rotations[particleIdx] = randomRange(&localState, 0.0f, TWO_PI);

    // Mark as alive
    particles.alive[particleIdx] = 1;

    randStates[idx % particles.maxCount] = localState;
}

__global__ void emitEarthParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    int particleIdx = startIndex + idx;
    if (particleIdx >= particles.maxCount) return;

    curandState localState = randStates[idx % particles.maxCount];

    // Emit in expanding pattern
    float angle1 = randomRange(&localState, 0.0f, TWO_PI);
    float angle2 = randomRange(&localState, -PI * 0.25f, PI * 0.25f);
    float speed = randomRange(&localState, 2.0f, 5.0f);

    particles.positions[particleIdx] = params.position;
    // Zero-length ribbon seed — see water emit for the rationale.
    particles.prevPositions[particleIdx] = params.position;

    // Debris velocity
    float3 debrisVel = make_float3(
        cosf(angle1) * cosf(angle2) * speed,
        sinf(angle2) * speed + randomRange(&localState, 1.0f, 3.0f),
        sinf(angle1) * cosf(angle2) * speed
    );

    particles.velocities[particleIdx] = make_float3(
        params.velocity.x + debrisVel.x,
        params.velocity.y + debrisVel.y,
        params.velocity.z + debrisVel.z
    );

    // Color
    particles.colors[particleIdx] = getEarthColor(0.0f, params.intensity);

    // Lifetime
    float lifetime = randomRange(&localState, 1.0f, 2.0f);
    particles.lifetimes[particleIdx] = lifetime;
    particles.maxLifetimes[particleIdx] = lifetime;

    // Size (larger for rocks)
    particles.sizes[particleIdx] = randomRange(&localState, 0.15f, 0.4f) * params.sizeMult *
                                  params.earth.debrisSize;

    // Rotation
    particles.rotations[particleIdx] = randomRange(&localState, 0.0f, TWO_PI);

    // Mark as alive
    particles.alive[particleIdx] = 1;

    randStates[idx % particles.maxCount] = localState;
}

__global__ void emitFireParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    int startIndex,
    int count,
    curandState* randStates
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    int particleIdx = startIndex + idx;
    if (particleIdx >= particles.maxCount) return;

    curandState localState = randStates[idx % particles.maxCount];

    // Emit from base with slight spread
    float3 offset = make_float3(
        randomRange(&localState, -0.3f, 0.3f),
        randomRange(&localState, 0.0f, 0.2f),
        randomRange(&localState, -0.3f, 0.3f)
    );

    const float3 spawnPos = make_float3(
        params.position.x + offset.x,
        params.position.y + offset.y,
        params.position.z + offset.z
    );
    particles.positions[particleIdx] = spawnPos;
    // Zero-length ribbon seed — see water emit for the rationale.
    particles.prevPositions[particleIdx] = spawnPos;

    // Upward velocity with turbulence
    float3 fireVel = make_float3(
        randomRange(&localState, -0.5f, 0.5f),
        randomRange(&localState, 2.0f, 4.0f),
        randomRange(&localState, -0.5f, 0.5f)
    );

    particles.velocities[particleIdx] = make_float3(
        params.velocity.x + fireVel.x,
        params.velocity.y + fireVel.y,
        params.velocity.z + fireVel.z
    );

    // Color
    particles.colors[particleIdx] = getFireColor(0.0f, params.intensity);

    // Lifetime
    float lifetime = randomRange(&localState, 0.4f, 1.0f);
    particles.lifetimes[particleIdx] = lifetime;
    particles.maxLifetimes[particleIdx] = lifetime;

    // Size
    particles.sizes[particleIdx] = randomRange(&localState, 0.15f, 0.35f) * params.sizeMult;

    // Rotation
    particles.rotations[particleIdx] = randomRange(&localState, 0.0f, TWO_PI);

    // Mark as alive
    particles.alive[particleIdx] = 1;

    randStates[idx % particles.maxCount] = localState;
}

// ============================================================================
// Update Kernels
// ============================================================================

__global__ void updateWaterParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= particles.count) return;

    if (!particles.alive[idx]) return;

    // Update position with wave motion.
    // Snapshot the pre-step position for the ribbon-trail tangent BEFORE
    // applyWaveMotion transforms it. Same invariant the main updateParticles
    // kernel upholds in ParticleKernels.cu.
    float3 pos = particles.positions[idx];
    particles.prevPositions[idx] = pos;
    pos = applyWaveMotion(pos, params.time, params.water.waveFrequency,
                         params.water.waveAmplitude);
    particles.positions[idx] = pos;

    // Update color based on lifetime
    float lifetimeNorm = 1.0f - (particles.lifetimes[idx] / particles.maxLifetimes[idx]);
    particles.colors[idx] = getWaterColor(lifetimeNorm, params.intensity);
}

__global__ void updateAirParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= particles.count) return;

    if (!particles.alive[idx]) return;

    // Apply vortex motion. Snapshot prev before the vortex/turbulence
    // transform so the ribbon tangent reflects this step's motion.
    float3 pos = particles.positions[idx];
    particles.prevPositions[idx] = pos;
    pos = applyVortexMotion(pos, params.position, params.air.swirlRadius,
                           params.air.swirlSpeed, params.time);

    // Add turbulence
    float3 turbulence = turbulentNoise(pos, params.time, params.air.turbulence);
    pos.x += turbulence.x * deltaTime;
    pos.y += turbulence.y * deltaTime;
    pos.z += turbulence.z * deltaTime;

    particles.positions[idx] = pos;

    // Update color
    float lifetimeNorm = 1.0f - (particles.lifetimes[idx] / particles.maxLifetimes[idx]);
    particles.colors[idx] = getAirColor(lifetimeNorm, params.intensity);
}

__global__ void updateEarthParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= particles.count) return;

    if (!particles.alive[idx]) return;

    // Apply gravity
    float3 vel = particles.velocities[idx];
    vel.x += params.earth.gravityDir.x * deltaTime * 9.8f;
    vel.y += params.earth.gravityDir.y * deltaTime * 9.8f;
    vel.z += params.earth.gravityDir.z * deltaTime * 9.8f;
    particles.velocities[idx] = vel;

    // Update rotation
    particles.rotations[idx] += params.earth.rotationSpeed * deltaTime;

    // Update color
    float lifetimeNorm = 1.0f - (particles.lifetimes[idx] / particles.maxLifetimes[idx]);
    particles.colors[idx] = getEarthColor(lifetimeNorm, params.intensity);
}

__global__ void updateFireParticles(
    GpuParticles particles,
    ElementalParticleParams params,
    float deltaTime
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= particles.count) return;

    if (!particles.alive[idx]) return;

    // Apply heat rise
    float3 pos = particles.positions[idx];
    float3 vel = particles.velocities[idx];

    vel = applyHeatRise(pos, vel, params.time, params.intensity);

    // Add flame curl
    float3 curl = turbulentNoise(pos, params.time, params.fire.flameCurl);
    vel.x += curl.x;
    vel.z += curl.z;

    particles.velocities[idx] = vel;

    // Flicker size
    float flicker = 1.0f + elementalNoise(pos, params.time * 10.0f) * 0.2f;
    particles.sizes[idx] *= flicker;

    // Update color (fire fades to dark red)
    float lifetimeNorm = 1.0f - (particles.lifetimes[idx] / particles.maxLifetimes[idx]);
    particles.colors[idx] = getFireColor(lifetimeNorm, params.intensity);
}

} // namespace CUDA
} // namespace CatEngine

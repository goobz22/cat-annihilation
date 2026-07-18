// ---------------------------------------------------------------------------
// ParticleFade — the per-frame alpha fade shared by the CUDA update kernel and
// the CPU unit tests.
//
// WHY this exists (2026-07-18 audit, Round 7):
//   updateParticles used to do `color.w *= lifetimeRatio` against the PERSISTED
//   color each frame. Because the already-faded alpha was re-read and
//   re-multiplied by that frame's ratio, the stored alpha became the running
//   PRODUCT of every past ratio instead of `baseAlpha * currentRatio` — the
//   fade compounded geometrically and every death/hit burst vanished at ~25%
//   of its configured lifetime (for a 1.5 s burst at 60 fps the product hits
//   ~1e-5 by ~0.4 s while the intended single multiply would still be ~0.7).
//
// THE FIX (telescoping update, no new SoA storage):
//   The spawn alpha is per-particle (emitter.colorBase.w + random variation),
//   so a global constant cannot recover it, and adding a `baseAlphas` array
//   would have to thread through allocation, emission, AND both compaction /
//   depth-sort gathers (the GpuParticles doc block warns that forgetting an
//   array in a gather is exactly how particles corrupt). Instead each frame
//   multiplies by the RATIO OF RATIOS:
//
//       w_k = w_{k-1} * (r_k / r_{k-1})
//
//   which telescopes exactly to  w_k = w_0 * r_k / r_0,  and r_0 = 1 at the
//   first update (lifetime starts at maxLifetime), so the stored alpha is
//   always baseAlpha * currentLifetimeRatio — the intended linear fade — while
//   touching only the update kernel. Per-frame float error is ~1 ulp
//   multiplicative and is visually irrelevant over a particle's ~100 frames.
//
// WHY header-only + host/device (mirrors SimplexNoise.hpp):
//   Unit tests link the SAME function nvcc builds into the kernel, so the
//   in-game fade is bit-identical to the tested fade. The unit suite has no
//   GPU; the plain-C++ branch of PARTICLE_FADE_HD compiles the attributes away.
// ---------------------------------------------------------------------------
#pragma once

#ifdef __CUDACC__
#define PARTICLE_FADE_HD __host__ __device__ inline
#else
#define PARTICLE_FADE_HD inline
#endif

namespace CatEngine {
namespace CUDA {
namespace fade {

PARTICLE_FADE_HD float clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

// Advance the stored alpha by one frame of lifetime fade.
//
// `previousAlpha` — the alpha currently persisted in the color buffer
//                   (baseAlpha * previousRatio by this function's invariant).
// `lifetime`      — the REMAINING lifetime AFTER this frame's decrement.
// `maxLifetime`   — the particle's initial lifetime (never changes).
// `deltaTime`     — this frame's dt (so previousRatio can be reconstructed).
//
// Returns baseAlpha * clamp01(lifetime / maxLifetime) — the linear fade.
//
// The near-zero guard: previousRatio approaches 0 only when the particle is
// within one frame of death (lifetime + dt ≈ 0), at which point the kill pass
// culls it; returning the unchanged alpha for that single terminal frame is
// visually identical and avoids the divide.
PARTICLE_FADE_HD float fadedAlpha(float previousAlpha, float lifetime,
                                  float maxLifetime, float deltaTime) {
    if (maxLifetime <= 0.0f) {
        return previousAlpha;
    }
    const float previousRatio = clamp01((lifetime + deltaTime) / maxLifetime);
    const float currentRatio = clamp01(lifetime / maxLifetime);
    if (previousRatio <= 1e-6f) {
        return previousAlpha;  // terminal frame; the kill pass culls it next
    }
    return previousAlpha * (currentRatio / previousRatio);
}

}  // namespace fade
}  // namespace CUDA
}  // namespace CatEngine

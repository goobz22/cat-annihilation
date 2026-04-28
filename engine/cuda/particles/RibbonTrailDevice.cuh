// RibbonTrailDevice.cuh
// ---------------------------------------------------------------------------
// CUDA-side ribbon-trail vertex-buffer builder.
//
// WHY this exists (backlog: P1 "Ribbon-trail emitter — extend the SoA particle
// layout with a previous-position array; render as a triangle strip"):
//   The host-side reference kernel `ribbon::BuildRibbonStrip` in
//   `RibbonTrail.hpp` is intentionally host-only (it operates on
//   `Engine::vec3`, whose SSE intrinsics are incompatible with nvcc's
//   device-code path — see Vector.hpp's `<xmmintrin.h>` / `<smmintrin.h>`
//   includes). Copying every particle's position/prev/color/size from device
//   memory to host, running the host strip-builder, and re-uploading the
//   resulting vertex buffer every frame would eat the interop budget on GPUs
//   with >10k live particles (a mid-wave fireball barrage hits that easily).
//
//   This header lands the device-callable twin: the same geometry kernel,
//   rewritten against `float3` / `float4` (the CUDA-native types already used
//   by `GpuParticles`) and driven by one thread per particle. The host
//   version stays the tested reference; this device version is the runtime
//   path.
//
// OUTPUT LAYOUT (FIXED-STRIDE, PARALLEL-SAFE):
//   The host strip-builder emits a packed variable-length strip (skipping
//   degenerate particles entirely) and relies on a serial offset cursor that
//   has no natural parallelisation. The device kernel instead writes
//   exactly **4 vertices per particle** (one quad) at a predictable offset
//   `outVertices[i*4 .. i*4 + 3]` and relies on a static index buffer
//   (built once at pipeline-init time, also 6 indices per particle) to
//   stitch them into triangles. This shifts the complexity from runtime
//   per-frame prefix-scan to one-shot index-buffer construction — a trade
//   that's strictly better for real-time rendering:
//
//     - every thread writes to a disjoint contiguous 4-vertex slot
//       (fully coalesced on the output buffer, no synchronisation)
//     - particles that are degenerate (below-epsilon motion OR head-on
//       view) write four zero-area coincident vertices at the particle's
//       current position. The rasterizer early-culls the resulting
//       zero-area triangles; no branch in the fragment path.
//     - dead particles (alive[i] == 0) also degenerate-write, so the
//       renderer can draw `maxParticles` quads without a compaction
//       pass. The compaction in ParticleSystem::update() is still
//       done for the simulation; this kernel just doesn't need to
//       re-derive it.
//
//   Compared to the host's triangle-strip-with-bridges output, the quads
//   rasterize to the same on-screen pixels because:
//     - The two degenerate bridge triangles the host inserts between
//       adjacent strips produce zero area → culled.
//     - Each host quad rasterizes to exactly the same two triangles as
//       our {0,1,2,1,3,2} indexed scheme (same four corners, same
//       winding).
//
// VERTEX FORMAT:
//   `RibbonVertex` matches the host `ribbon::Vertex` (position/color/uv)
//   member-for-member so a future shared-header refactor can unify the two
//   without ABI breakage. Kept as a distinct POD here to (a) avoid pulling
//   `Engine::vec3` into nvcc device compilation and (b) give the Vulkan
//   pipeline layout a single authoritative definition rooted in CUDA
//   types (positions come from `GpuParticles.positions`, a `float3*`).
//
// WHY not make RibbonTrail.hpp `__host__ __device__`:
//   Its signatures consume `Engine::vec3`, whose `alignas(16)`-with-SSE
//   implementation can't be compiled in a CUDA device TU. Templating
//   RibbonTrail.hpp on a vec3 concept would require either a dependency
//   injection of `cross`/`length`/`operator*=` via free functions for every
//   vec3 type, or a template specialisation layer equivalent to just
//   rewriting the math — we picked the rewrite because it's strictly
//   fewer moving parts.
// ---------------------------------------------------------------------------
#pragma once

#include <cuda_runtime.h>
#include <cstdint>

#include "ParticleKernels.cuh"

namespace CatEngine {
namespace CUDA {
namespace ribbon_device {

// ---------------------------------------------------------------------------
// Vertex format
// ---------------------------------------------------------------------------
//
// Mirrors `ribbon::Vertex` in `RibbonTrail.hpp` member-for-member. Laid out
// so a Vulkan vertex input binding of:
//
//   location 0  : vec3  position   (offset 0)
//   location 1  : vec4  color      (offset 16 — float4's 16-byte alignment
//                                   pads the float3 `position` up from
//                                   offset 12 to 16)
//   location 2  : vec2  uv         (offset 32)
//   stride                          = sizeof(RibbonVertex) = 48
//
// pulls data directly from this struct. Total size is 48 bytes: 36 bytes of
// payload (12 + 16 + 8) + 4 bytes of internal padding after `position` + 8
// bytes of tail padding so the struct as a whole respects float4's 16-byte
// alignment (C/C++ requires that the struct size be a multiple of its
// largest member alignment so arrays of the struct stay aligned).
//
// We accept the 25% overhead (12 wasted bytes out of 48) because fighting
// it (e.g. `float[4] color` to avoid the 16-byte alignment, or manually
// packing into a single `float4` + `uv` into two components of another
// `float4`) would make the Vulkan vertex-attribute layout considerably
// more complex for very little bandwidth gain at the particle counts the
// game actually hits in practice (~5-10k active ribbon particles in a
// full magic-school barrage). Pin `sizeof(RibbonVertex)` in the unit tests
// so a future member reorder can't silently break the shader's vertex
// fetch stride.
struct RibbonVertex {
    float3 position;
    float4 color;
    float2 uv;
};

// ---------------------------------------------------------------------------
// Default epsilon knobs
// ---------------------------------------------------------------------------
//
// Mirror the host `kDefault*` constants. Plain `constexpr` (no `__device__`
// decoration) so they're usable from both host-compiled TUs (the Vulkan
// renderer passing them as launch parameters) and device-compiled TUs
// (the kernel body dereferencing them). CUDA treats compile-time-known
// constexpr scalars as immediate constants on both sides — there's no
// device global to access.
inline constexpr float kDefaultMinSegmentLength = 1e-4f; // metres
inline constexpr float kDefaultMinCrossLength   = 1e-4f; // |t × v|
inline constexpr float kDefaultMinHalfWidth     = 1e-5f; // metres

// ---------------------------------------------------------------------------
// Compilation-model notes
// ---------------------------------------------------------------------------
//
// This header is included from both:
//   - nvcc-compiled .cu translation units (ParticleSystem.cu) — sees the
//     kernel and device helpers;
//   - host-compiled .cpp translation units (Vulkan renderer, unit tests) —
//     sees only the POD vertex layout, the index-buffer CPU helper, and the
//     buffer-size constants.
//
// `__CUDACC__` is defined only while nvcc is driving a .cu TU. Everything
// device-only lives behind that guard so the host compiler never has to
// parse `blockIdx.x` or kernel `<<<...>>>` syntax, which would fail on a
// plain MSVC / clang host pass even with the CUDA SDK headers present.

// ---------------------------------------------------------------------------
// Device math helpers
// ---------------------------------------------------------------------------
//
// `cross3` is the only vector-cross helper this kernel needs. ParticleKernels.cu
// already defines file-local `__device__` operator+/-/*/ and `length` /
// `normalize` for float3 — we re-declare `length3` / `cross3` here as TU-local
// helpers because C++ does not allow name-sharing across distinct TUs with
// `__device__ inline`. Using unique names avoids any accidental ODR
// shenanigans with the ParticleKernels.cu local operators.

#ifdef __CUDACC__

__device__ __forceinline__ float3 cross3(const float3& a, const float3& b) noexcept {
    return make_float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

__device__ __forceinline__ float length3(const float3& v) noexcept {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

// ---------------------------------------------------------------------------
// Ribbon quad emit
// ---------------------------------------------------------------------------
//
// Compute the (tangent, side) frame for the `prev → current` segment viewed
// from `viewDir`, taper the half-width by `lifetimeRatio`, and write the
// four quad corners to `out[0..3]` in the same layout as the host version:
//
//   out[0] = prev    - side * halfTail  (back-left,   uv = (0,0))
//   out[1] = prev    + side * halfTail  (back-right,  uv = (1,0))
//   out[2] = current - side * halfHead  (front-left,  uv = (0,1))
//   out[3] = current + side * halfHead  (front-right, uv = (1,1))
//
// Static indices {0,1,2, 1,3,2} (6 entries per particle) stitch these four
// corners into two counter-clockwise triangles that match Vulkan's default
// front-face convention — so the renderer's pipeline can keep back-face
// culling on.
//
// If the segment is degenerate (sub-epsilon motion OR head-on view), all
// four output corners collapse to `current` and the colors/uvs are zeroed.
// That produces four coincident vertices whose rasterised triangles have
// zero screen-space area and are early-culled by the hardware rasterizer.
__device__ __forceinline__ void BuildQuadForParticle(
    const float3& prev,
    const float3& current,
    const float4& colorCurrent,
    float halfWidth,
    float lifetimeRatio,
    float tailWidthFactor,
    const float3& viewDir,
    float minSegmentLen,
    float minCrossLen,
    RibbonVertex* out
) noexcept {
    // --- Degenerate: zero-length motion (particle stationary this frame).
    // Collapse to four coincident corners at `current`; rasterizer culls.
    const float3 motion = make_float3(current.x - prev.x,
                                       current.y - prev.y,
                                       current.z - prev.z);
    const float motionLen = length3(motion);
    if (motionLen <= minSegmentLen) {
        const float4 zero = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
        out[0].position = current; out[0].color = zero; out[0].uv = make_float2(0.0f, 0.0f);
        out[1].position = current; out[1].color = zero; out[1].uv = make_float2(0.0f, 0.0f);
        out[2].position = current; out[2].color = zero; out[2].uv = make_float2(0.0f, 0.0f);
        out[3].position = current; out[3].color = zero; out[3].uv = make_float2(0.0f, 0.0f);
        return;
    }

    const float invMotionLen = 1.0f / motionLen;
    const float3 tangent = make_float3(motion.x * invMotionLen,
                                       motion.y * invMotionLen,
                                       motion.z * invMotionLen);

    // --- Degenerate: camera looking straight down the motion axis
    // (tangent collinear with viewDir → cross collapses to zero).
    // Collapse as above; a future improvement could fall back to a
    // screen-aligned up-vector to draw a head-on billboard, but that's a
    // policy decision the host ribbon helper also defers to the caller.
    const float3 sideRaw = cross3(tangent, viewDir);
    const float sideLen = length3(sideRaw);
    if (sideLen <= minCrossLen) {
        const float4 zero = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
        out[0].position = current; out[0].color = zero; out[0].uv = make_float2(0.0f, 0.0f);
        out[1].position = current; out[1].color = zero; out[1].uv = make_float2(0.0f, 0.0f);
        out[2].position = current; out[2].color = zero; out[2].uv = make_float2(0.0f, 0.0f);
        out[3].position = current; out[3].color = zero; out[3].uv = make_float2(0.0f, 0.0f);
        return;
    }

    const float invSideLen = 1.0f / sideLen;
    const float3 side = make_float3(sideRaw.x * invSideLen,
                                    sideRaw.y * invSideLen,
                                    sideRaw.z * invSideLen);

    // --- Clamp half-width to a strictly-positive floor: a designer who
    // sets size = 0 gets a GPU-warning-free zero-area quad instead of a
    // NaN-spewing divide downstream (the host kernel uses the same floor,
    // see RibbonTrail.hpp::BuildBillboardSegment).
    const float halfHead = halfWidth < kDefaultMinHalfWidth
                               ? kDefaultMinHalfWidth
                               : halfWidth;

    // Linear taper from full half-width at the head (lifetimeRatio=1) to
    // `tailWidthFactor * halfHead` at the tail (lifetimeRatio=0). Matches
    // `ribbon::TaperHalfWidth` exactly. The tail width sits at the *back*
    // of the quad (vertices 0 and 1), the head width at the front (2, 3).
    const float clampedRatio = lifetimeRatio < 0.0f ? 0.0f
                             : (lifetimeRatio > 1.0f ? 1.0f : lifetimeRatio);
    const float clampedTail  = tailWidthFactor < 0.0f ? 0.0f : tailWidthFactor;
    const float taperScale   = clampedTail + (1.0f - clampedTail) * clampedRatio;
    const float halfTail     = halfHead * taperScale;

    // --- Tail color: same RGB as head, alpha multiplied by lifetimeRatio
    // so the back of the quad fades as the particle ages. We do NOT
    // pre-multiply RGB because some shaders alpha-premultiply in-shader
    // and some don't — doing it here would double-clip the dynamic range
    // in the premultiplied case. The host kernel uses identical logic.
    const float4 colorTail = make_float4(
        colorCurrent.x,
        colorCurrent.y,
        colorCurrent.z,
        colorCurrent.w * clampedRatio
    );

    const float3 offsetTail  = make_float3(side.x * halfTail,  side.y * halfTail,  side.z * halfTail);
    const float3 offsetHead  = make_float3(side.x * halfHead,  side.y * halfHead,  side.z * halfHead);

    // Quad corner assignment (matches ribbon::BuildBillboardSegment).
    out[0].position = make_float3(prev.x - offsetTail.x, prev.y - offsetTail.y, prev.z - offsetTail.z);
    out[0].color    = colorTail;
    out[0].uv       = make_float2(0.0f, 0.0f);

    out[1].position = make_float3(prev.x + offsetTail.x, prev.y + offsetTail.y, prev.z + offsetTail.z);
    out[1].color    = colorTail;
    out[1].uv       = make_float2(1.0f, 0.0f);

    out[2].position = make_float3(current.x - offsetHead.x, current.y - offsetHead.y, current.z - offsetHead.z);
    out[2].color    = colorCurrent;
    out[2].uv       = make_float2(0.0f, 1.0f);

    out[3].position = make_float3(current.x + offsetHead.x, current.y + offsetHead.y, current.z + offsetHead.z);
    out[3].color    = colorCurrent;
    out[3].uv       = make_float2(1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Parallel ribbon-strip build kernel
// ---------------------------------------------------------------------------
//
// One thread per particle slot (0 .. maxParticleCount-1). Thread `i`:
//   - If `i >= liveCount` OR `alive[i] == 0`, writes four coincident
//     degenerate vertices at origin (early-culled).
//   - Otherwise computes the quad corners for particle `i` into
//     `out[4*i .. 4*i + 3]`.
//
// `liveCount` is the post-compaction particle count from
// `ParticleSystem::getRenderData().count`. After `compactParticles` the live
// particles occupy slots `[0, liveCount)` contiguously, so the alive[] check
// is redundant in the nominal path — it's kept as a belt-and-braces guard
// for callers who skip compaction (config flag) or invoke the kernel after
// a partial-frame update.
//
// `viewDir` is assumed unit-length (renderer normalises camera forward once
// per frame and passes in the result). We do NOT normalise inside the kernel
// because (a) that's redundant work per-particle, (b) a zero-viewDir should
// visibly corrupt the output so an upstream bug is caught loudly rather than
// masked to silent correctness.
__global__ void ribbonTrailBuildKernel(
    GpuParticles particles,
    RibbonVertex* __restrict__ outVertices,
    int liveCount,
    int maxParticleCount,
    float3 viewDir,
    float tailWidthFactor,
    float minSegmentLen,
    float minCrossLen
) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= maxParticleCount) {
        return;
    }

    RibbonVertex* slot = outVertices + (i * 4);

    // Guard against the dead-slot case. See notes above — four coincident
    // zero-alpha vertices rasterize to zero-area triangles that early-cull.
    const bool slotLive = (i < liveCount)
        && (particles.alive != nullptr ? particles.alive[i] != 0 : true);
    if (!slotLive) {
        const float3 zeroPos = make_float3(0.0f, 0.0f, 0.0f);
        const float4 zeroCol = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
        const float2 zeroUv  = make_float2(0.0f, 0.0f);
        slot[0].position = zeroPos; slot[0].color = zeroCol; slot[0].uv = zeroUv;
        slot[1].position = zeroPos; slot[1].color = zeroCol; slot[1].uv = zeroUv;
        slot[2].position = zeroPos; slot[2].color = zeroCol; slot[2].uv = zeroUv;
        slot[3].position = zeroPos; slot[3].color = zeroCol; slot[3].uv = zeroUv;
        return;
    }

    // Lifetime ratio: 1.0 = just emitted, 0.0 = about to expire. Protected
    // against divide-by-zero when maxLifetimes is zeroed (degenerate emitter
    // config); a zero-max particle gets ratio=0 so the trail renders as a
    // transparent no-op instead of a NaN-laden chord.
    const float maxLife = particles.maxLifetimes[i];
    const float lifetimeRatio =
        (maxLife > 1e-6f) ? (particles.lifetimes[i] / maxLife) : 0.0f;

    // Per-particle half-width comes from the existing `sizes` SoA column —
    // treat `size` as the FULL visual width of the trail (ribbon spans
    // ±halfWidth around the motion axis), so divide by two. This matches
    // the host renderer's historical convention; when the renderer wires
    // a dedicated `ribbonHalfWidth` attribute in iteration 4 the caller
    // will switch to that and this line becomes a fallback for emitters
    // that don't opt in.
    const float halfWidth = particles.sizes[i] * 0.5f;

    BuildQuadForParticle(
        particles.prevPositions[i],
        particles.positions[i],
        particles.colors[i],
        halfWidth,
        lifetimeRatio,
        tailWidthFactor,
        viewDir,
        minSegmentLen,
        minCrossLen,
        slot
    );
}

#endif // __CUDACC__

// ---------------------------------------------------------------------------
// Index buffer builder (CPU-side helper)
// ---------------------------------------------------------------------------
//
// Fills a host-side uint32_t buffer with the 6-indices-per-particle pattern
// {4i+0, 4i+1, 4i+2, 4i+1, 4i+3, 4i+2} that stitches our per-particle quads
// into two CCW triangles.
//
// The renderer builds this once at pipeline-init (the pattern is invariant
// across frames; only the vertex buffer is re-written each frame) and
// uploads it as a static index buffer. A device-side kernel would be
// wasteful — it's a one-shot write of `6 * maxParticleCount * 4` bytes.
//
// Kept in this header (rather than a .cu) so non-CUDA callers (the Vulkan
// renderer in a future iteration) can use it without linking against nvcc
// output.
inline void FillRibbonIndexBufferCPU(uint32_t* outIndices, int maxParticleCount) noexcept {
    for (int i = 0; i < maxParticleCount; ++i) {
        const uint32_t base = static_cast<uint32_t>(i) * 4u;
        uint32_t* dst = outIndices + (i * 6);
        // Two CCW triangles: {v0, v1, v2} and {v1, v3, v2}.
        dst[0] = base + 0u;
        dst[1] = base + 1u;
        dst[2] = base + 2u;
        dst[3] = base + 1u;
        dst[4] = base + 3u;
        dst[5] = base + 2u;
    }
}

// Worst-case vertex / index counts the renderer uses to size its VkBuffer
// allocations. Both grow linearly in `maxParticleCount`; with `maxParticles
// = 1_000_000` (see ParticleSystem::Config) the vertex buffer at 40 B per
// vertex is 160 MB and the index buffer at 24 B per particle is 24 MB —
// well within a modern dGPU's budget, and the renderer can clamp to a
// smaller effective cap via `enable-ribbon-trails` CLI flags.
constexpr int kVerticesPerParticle = 4;
constexpr int kIndicesPerParticle  = 6;

inline constexpr size_t RibbonVertexBufferSize(int maxParticleCount) noexcept {
    return static_cast<size_t>(maxParticleCount)
         * static_cast<size_t>(kVerticesPerParticle)
         * sizeof(RibbonVertex);
}

inline constexpr size_t RibbonIndexBufferSize(int maxParticleCount) noexcept {
    return static_cast<size_t>(maxParticleCount)
         * static_cast<size_t>(kIndicesPerParticle)
         * sizeof(uint32_t);
}

} // namespace ribbon_device
} // namespace CUDA
} // namespace CatEngine

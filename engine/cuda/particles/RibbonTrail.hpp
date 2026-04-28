// RibbonTrail.hpp
// ---------------------------------------------------------------------------
// Camera-facing ribbon-trail geometry kernel for the CUDA particle system.
//
// WHY this exists (backlog: P1 "Ribbon-trail emitter — extend the SoA particle
// layout with a previous-position array; render as a triangle strip"):
//   The in-game projectile VFX (fireballs, ice shards, lightning bolts in the
//   four elemental magic schools) need to leave a visible motion trail behind
//   them, not just a single billboarded sprite at the head. A trail is the
//   classic "stretch the previous N positions of one particle into a thin
//   ribbon" effect; the cheapest way to get it is to keep one extra float3
//   per particle ("position from the previous integration step") and emit two
//   triangles per particle that span (prev, current) facing the camera.
//
//   This header is the math kernel — given (prev, current) plus the camera
//   forward vector and a half-width in world units, it produces the four
//   billboard vertices that form one quad of the strip. The kernel is
//   deliberately decoupled from CUDA types and from any specific Vulkan
//   vertex format so the unit tests (test_ribbon_trail.cpp) can exercise the
//   exact same code path that a future GPU-side strip-builder kernel and the
//   future renderer-side CPU strip-builder will both call.
//
//   Why not just put this in ParticleKernels.cu? Because the math is pure
//   STL float math. Forcing it to live in a .cu file would (a) lock the test
//   suite out from linking against it, (b) make it impossible to call from
//   the future Vulkan vertex-buffer-upload code path on the CPU, (c) and
//   prevent inlining into either consumer. Header-only with `inline` keeps
//   one source of truth and matches the established pattern from
//   SimplexNoise.hpp / TwoBoneIK.hpp / RootMotion.hpp / SequentialImpulse.hpp
//   / CCD.hpp / CCDPrepass.hpp.
//
// WHY not __host__ __device__:
//   The current consumers are: (1) the Catch2 unit test suite (host), (2) the
//   future renderer's CPU vertex-buffer-fill loop (host). The future CUDA
//   path would call this from a __global__ kernel, in which case adding
//   `__host__ __device__` qualifiers is a one-line change at that point. We
//   don't add them speculatively because plain C++ TUs that include this
//   header would currently fail to compile if `__host__ __device__` weren't
//   defined to nothing, and pulling in <cuda_runtime.h> at this layer would
//   couple every consumer to the CUDA toolchain.
//
// SHAPE OF THE PUBLIC API:
//   - Vertex                 : the per-corner POD the four-corner quad emits
//   - SegmentBasis           : the (tangent, side) frame for one segment
//   - ComputeSegmentBasis()  : derive the basis from (prev, current, viewDir)
//   - BuildBillboardSegment(): emit the four corner Vertex records as a strip
//   - TaperHalfWidth()       : optional life-ratio taper applied per particle
//   - BuildRibbonStrip()     : convenience batch helper that emits a full
//                              triangle-strip across N particles with
//                              degenerate triangles inserted between strips
//
// All operations are deterministic and side-effect-free; outputs are written
// through caller-supplied iterators / arrays so the consumer controls the
// allocation policy (engine renderer uses a pre-sized vertex buffer; the test
// harness uses std::vector).
// ---------------------------------------------------------------------------
#pragma once

#include "../../math/Vector.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace CatEngine {
namespace CUDA {
namespace ribbon {

// ---------------------------------------------------------------------------
// Vertex layout
// ---------------------------------------------------------------------------
//
// One ribbon-quad corner. Kept intentionally minimal so the same struct can be
// rasterized by a thin Vulkan vertex shader (just position+color+uv) and so
// the unit tests can REQUIRE() against a flat memory layout without brittle
// reflection. Per-corner uv.x ∈ {0,1} (left/right side of the strip) and
// uv.y ∈ {0,1} (back/front of the segment) — that's the canonical mapping a
// shader uses to sample a flame-falloff texture along the trail.
//
// Color is propagated per-vertex (not per-segment) so the vertex shader can
// linearly interpolate alpha along the strip — usually a fade from "head
// is bright at color of the live particle" to "tail is transparent at the
// color the particle had one frame ago". The renderer fills both ends from
// the SoA color/alpha at (current, prev) sample sites respectively; this
// header doesn't enforce a fade direction so emitter-side tinting (combo
// hits, school-of-magic colour ramp) stays in the renderer's hands.
struct Vertex {
    Engine::vec3 position;
    Engine::vec4 color;
    Engine::vec2 uv;
};

// Tangent / side basis for a single segment, computed once per particle and
// reused for both leading-edge (prev) and trailing-edge (current) vertices.
//
// `tangent` points from prev → current (so screen-up in the trail's local
// frame is "the direction motion is going").
//
// `side` points "to the right of motion as seen by the camera" — i.e. the
// world-space direction perpendicular to both motion and camera view. That's
// the axis we extrude along to give the ribbon thickness.
//
// `valid` is false in two degenerate situations:
//   1. prev ≈ current (no motion, segment length below epsilon)
//   2. tangent collinear with viewDir (camera looking straight down the
//      motion axis — cross product collapses to zero)
// Callers should either skip invalid segments (renderer's choice for sparse
// trails) or fall back to a screen-space billboard quad (renderer's choice
// for dense trails so a momentary head-on view doesn't drop frames of trail).
struct SegmentBasis {
    Engine::vec3 tangent;
    Engine::vec3 side;
    bool valid;
};

// Default thresholds. Public so the renderer can override per-effect (e.g.
// the lightning-bolt VFX wants a very loose epsilon so motion-aligned strikes
// still draw a thin ribbon, while the dust-puff VFX wants a strict epsilon so
// stationary embers don't draw garbage quads).
inline constexpr float kDefaultMinSegmentLength    = 1e-4f;   // metres
inline constexpr float kDefaultMinCrossLength      = 1e-4f;   // |t × v|
inline constexpr float kDefaultMinHalfWidth        = 1e-5f;   // metres

// ---------------------------------------------------------------------------
// SegmentBasis derivation
// ---------------------------------------------------------------------------
//
// Compute the (tangent, side) frame for the segment prev→current viewed by a
// camera with forward vector `viewDir`. The side vector is normalised; the
// caller multiplies by half-width to get the extrusion offset.
//
// Input contract:
//   - viewDir is assumed normalised (camera forwards usually are; the renderer
//     normalises once per frame and passes the result through). We do NOT
//     re-normalise here because (a) it's redundant work in the inner loop,
//     (b) re-normalising masks a "viewDir is zero" bug that should surface
//     as an obviously-invalid output instead of silently producing a NaN.
//
//   - prev and current may be equal — we report `valid=false` and zero out
//     the basis vectors so a buggy consumer that ignores `valid` can't
//     accidentally render a degenerate giant quad.
//
// The minSegmentLen / minCrossLen knobs let the caller widen the
// "definitely degenerate" threshold per-effect without recompiling.
inline SegmentBasis ComputeSegmentBasis(const Engine::vec3& prev,
                                        const Engine::vec3& current,
                                        const Engine::vec3& viewDir,
                                        float minSegmentLen = kDefaultMinSegmentLength,
                                        float minCrossLen = kDefaultMinCrossLength) noexcept {
    const Engine::vec3 motion = current - prev;
    const float motionLen = motion.length();

    SegmentBasis basis;
    if (motionLen <= minSegmentLen) {
        // Degenerate: prev coincides with current. Render a zero-area quad
        // by zeroing the basis — caller checks `valid` and skips, OR
        // multiplies side*halfWidth and gets four coincident corners (still
        // a valid no-op render).
        basis.tangent = Engine::vec3(0.0f);
        basis.side    = Engine::vec3(0.0f);
        basis.valid   = false;
        return basis;
    }

    basis.tangent = motion / motionLen;

    // Side = tangent × viewDir. Right-handed frame: with tangent pointing
    // along motion and viewDir pointing into the scene, this gives the
    // screen-space "right" relative to the camera. The renderer can flip the
    // sign by negating halfWidth if it wants the ribbon to curl the other
    // way.
    const Engine::vec3 sideRaw = basis.tangent.cross(viewDir);
    const float sideLen = sideRaw.length();
    if (sideLen <= minCrossLen) {
        // Degenerate: tangent collinear with viewDir (head-on view of the
        // trail). Caller decides fallback policy via `valid`.
        basis.side  = Engine::vec3(0.0f);
        basis.valid = false;
        return basis;
    }

    basis.side  = sideRaw / sideLen;
    basis.valid = true;
    return basis;
}

// ---------------------------------------------------------------------------
// TaperHalfWidth
// ---------------------------------------------------------------------------
//
// Linear taper from full half-width at the head of the trail to `tailFactor *
// halfWidth` at the tail. `lifetimeRatio` is the standard 1.0=just-spawned →
// 0.0=about-to-die value the kernel already maintains for the alpha fade.
//
// WHY linear and not a curve: the taper is composed multiplicatively with the
// alpha fade in the shader, so the effective visual area already follows
// (lifetime × lifetime). A linear taper here keeps the kernel cheap and the
// designer's two knobs (alpha fade vs trail thickness fade) decoupled — they
// can dial each independently and predict the visual.
//
// `tailFactor` typically in [0, 1]; values > 1 widen the tail (the lightning
// "fork" effect uses 2.0, deliberately). Values < 0 are clamped to 0 because
// negative half-width would inside-out the quad winding.
inline float TaperHalfWidth(float halfWidth,
                            float lifetimeRatio,
                            float tailFactor = 0.0f) noexcept {
    // lifetimeRatio is 1.0 at spawn and decays to 0.0 at death. The head of
    // the trail (current vertex) corresponds to NOW — so it gets full width.
    // The tail (prev vertex) corresponds to one frame earlier, which we
    // approximate as the SAME lifetime ratio for cheapness; the quad is one
    // frame thick, so the visual error of using the head's lifetime for both
    // ends is sub-millisecond and invisible.
    const float clampedRatio = lifetimeRatio < 0.0f ? 0.0f
                              : (lifetimeRatio > 1.0f ? 1.0f : lifetimeRatio);
    const float clampedTail  = tailFactor < 0.0f ? 0.0f : tailFactor;

    // Lerp halfWidth*tail (at lifetimeRatio=0) → halfWidth (at ratio=1).
    const float scale = clampedTail + (1.0f - clampedTail) * clampedRatio;
    return halfWidth * scale;
}

// ---------------------------------------------------------------------------
// BuildBillboardSegment
// ---------------------------------------------------------------------------
//
// Emit the four corner vertices of one ribbon-strip segment.
//
//   v[0] = prev    - side*halfWidth   (back-left)
//   v[1] = prev    + side*halfWidth   (back-right)
//   v[2] = current - side*halfWidth   (front-left)
//   v[3] = current + side*halfWidth   (front-right)
//
// In triangle-strip topology that emits triangles {v0,v1,v2} and {v1,v3,v2},
// which are both counter-clockwise when viewed with the camera at +viewDir.
// This matches Vulkan's default front-face convention so the renderer doesn't
// need to disable culling for ribbons.
//
// `outVertices` MUST point to at least 4 writable Vertex slots. The function
// returns the number of vertices written (always 4 if `basis.valid`,
// otherwise 0) so the caller can advance an output cursor cleanly across a
// batch.
//
// Per-corner uv layout:
//   uv.x = 0 on side-left (v0, v2), 1 on side-right (v1, v3) — a fragment
//          shader can use this for a side-to-side falloff (cylindrical fake-3D).
//   uv.y = 0 at the back (v0, v1), 1 at the front (v2, v3) — a fragment
//          shader uses this for an along-trail texture lookup.
//
// Color attribution: the back two corners (v0, v1) get `colorPrev`, the front
// two (v2, v3) get `colorCurrent`. The shader linearly interpolates between
// them across the segment, which gives the canonical "head-bright,
// tail-fade" look without any extra per-particle work in this kernel.
inline std::size_t BuildBillboardSegment(const Engine::vec3& prev,
                                         const Engine::vec3& current,
                                         const SegmentBasis& basis,
                                         float halfWidthBack,
                                         float halfWidthFront,
                                         const Engine::vec4& colorPrev,
                                         const Engine::vec4& colorCurrent,
                                         Vertex* outVertices) noexcept {
    if (!basis.valid || outVertices == nullptr) {
        return 0;
    }

    // Clamp half-widths to a tiny positive value so a designer who passes 0
    // doesn't produce a NaN-spewing zero-area quad in the rasterizer (some
    // GPUs emit warnings and some don't — easier to clamp here once than to
    // chase platform-specific glitches downstream).
    const float hwBack  = halfWidthBack  < kDefaultMinHalfWidth ? kDefaultMinHalfWidth : halfWidthBack;
    const float hwFront = halfWidthFront < kDefaultMinHalfWidth ? kDefaultMinHalfWidth : halfWidthFront;

    const Engine::vec3 offsetBack  = basis.side * hwBack;
    const Engine::vec3 offsetFront = basis.side * hwFront;

    outVertices[0].position = prev    - offsetBack;
    outVertices[0].color    = colorPrev;
    outVertices[0].uv       = Engine::vec2(0.0f, 0.0f);

    outVertices[1].position = prev    + offsetBack;
    outVertices[1].color    = colorPrev;
    outVertices[1].uv       = Engine::vec2(1.0f, 0.0f);

    outVertices[2].position = current - offsetFront;
    outVertices[2].color    = colorCurrent;
    outVertices[2].uv       = Engine::vec2(0.0f, 1.0f);

    outVertices[3].position = current + offsetFront;
    outVertices[3].color    = colorCurrent;
    outVertices[3].uv       = Engine::vec2(1.0f, 1.0f);

    return 4;
}

// ---------------------------------------------------------------------------
// BuildRibbonStrip
// ---------------------------------------------------------------------------
//
// Convenience batch helper for the renderer: walk N particles and emit one
// long triangle-strip with degenerate triangles bridging adjacent particles
// so a single draw call covers every visible trail.
//
// Degenerate-triangle bridging: between segment i and segment i+1 we repeat
// the last vertex of i and the first vertex of i+1, producing two
// zero-area triangles that the rasterizer drops at the early-cull stage but
// which preserve the strip's index sequence. This is the standard PSX-era
// trick for batching many disjoint quads into one strip primitive without
// switching to indexed mode — saves a draw call per particle (worth it when
// there are 10k+ active projectiles in a wave-50 fight).
//
// Output buffer sizing: the WORST CASE is 6 vertices per particle (4 corners
// + 2 degenerates), and the FIRST particle skips the bridging pair. So:
//
//   maxVertices = 4 + 6 * (count - 1)        for count >= 1
//               = 0                           for count == 0
//
// Caller MUST pre-size `outVertices` to at least `maxVertices`. We do not
// resize through a callback because the engine's primary consumer is a
// Vulkan host-mapped vertex buffer of fixed capacity (allocated once at
// pipeline init, not per-frame).
//
// Returns the actual number of vertices written, which is ≤ maxVertices
// because some particles may be `valid=false` (degenerate motion or head-on
// view) and contribute zero vertices.
//
// Stride parameters allow this function to read directly from the engine's
// SoA layout without copying into a temporary AoS first — the renderer can
// hand it the raw `m_prevPositions.get()`, `m_positions.get()`,
// `m_colors.get()` pointers from a host-pinned mirror.
struct StripInput {
    const Engine::vec3* prev;       // length `count`, stride sizeof(Engine::vec3)
    const Engine::vec3* current;    // length `count`, stride sizeof(Engine::vec3)
    const Engine::vec4* color;      // length `count`, stride sizeof(Engine::vec4)
    const float*        halfWidth;  // length `count`, stride sizeof(float); per-particle width
    const float*        lifetimeRatio; // length `count`, stride sizeof(float); for taper
    std::size_t         count;
};

struct StripParams {
    Engine::vec3 viewDir;        // assumed normalised
    float        tailWidthFactor;  // 0..1 typical, see TaperHalfWidth
    float        minSegmentLen;
    float        minCrossLen;
};

inline StripParams DefaultStripParams(const Engine::vec3& viewDir) noexcept {
    StripParams params;
    params.viewDir          = viewDir;
    params.tailWidthFactor  = 0.0f; // Trail tapers to a point by default
    params.minSegmentLen    = kDefaultMinSegmentLength;
    params.minCrossLen      = kDefaultMinCrossLength;
    return params;
}

// Worst-case vertex count for a strip of `count` particles, including
// degenerate-bridge vertices between segments. Renderer pre-sizes its
// vertex buffer with this so the BuildRibbonStrip call can never overflow.
inline std::size_t MaxStripVertexCount(std::size_t count) noexcept {
    if (count == 0) return 0;
    return 4u + 6u * (count - 1u); // 4 first segment + 4 + 2 bridge per subsequent
}

inline std::size_t BuildRibbonStrip(const StripInput& in,
                                    const StripParams& params,
                                    Vertex* outVertices,
                                    std::size_t outCapacity) noexcept {
    if (in.count == 0 || outVertices == nullptr || outCapacity == 0) {
        return 0;
    }

    std::size_t written = 0;
    bool        haveOpenStrip = false;

    for (std::size_t i = 0; i < in.count; ++i) {
        const SegmentBasis basis = ComputeSegmentBasis(
            in.prev[i],
            in.current[i],
            params.viewDir,
            params.minSegmentLen,
            params.minCrossLen
        );

        if (!basis.valid) {
            // Degenerate segment — close the open strip (next valid segment
            // will start its own pair of bridge vertices). This avoids a
            // visual "chord" jumping across a dead trail.
            haveOpenStrip = false;
            continue;
        }

        const float headHalf = (in.halfWidth ? in.halfWidth[i] : kDefaultMinHalfWidth);
        const float ratio    = (in.lifetimeRatio ? in.lifetimeRatio[i] : 1.0f);
        const float tailHalf = TaperHalfWidth(headHalf, ratio, params.tailWidthFactor);
        const Engine::vec4 colorCurrent = (in.color ? in.color[i] : Engine::vec4(1.0f, 1.0f, 1.0f, 1.0f));

        // Tail color = current color with alpha multiplied by lifetimeRatio
        // — the canonical head-bright, tail-fade look. We don't pre-fade
        // RGB because some shaders pre-multiply alpha and some don't; doing
        // it in vertex color space would clip the dynamic range twice.
        Engine::vec4 colorPrev = colorCurrent;
        colorPrev.w *= (ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio));

        // Bridge: if a strip is already open, emit the two degenerate
        // vertices that connect the previous quad's last corner to this
        // quad's first corner. We need 2 bridge + 4 quad = 6 slots.
        const std::size_t needed = haveOpenStrip ? 6u : 4u;
        if (written + needed > outCapacity) {
            // Output buffer would overflow — silently truncate. Caller is
            // responsible for sizing via MaxStripVertexCount(); silent
            // truncation is the safest behaviour for a real-time renderer
            // (better to drop the tail of the trail than crash mid-frame).
            break;
        }

        if (haveOpenStrip) {
            // Last vertex of previous quad was outVertices[written-1]
            // (a "current+side*hw" vertex). First vertex of new quad will be
            // outVertices[written+2] = "prev-side*hw". Repeating those two
            // produces the degenerate bridge.
            outVertices[written]     = outVertices[written - 1];
            outVertices[written + 1] = Vertex{}; // overwritten below
            written += 2;
        }

        Vertex* dst = outVertices + written;
        if (haveOpenStrip) {
            // Overwrite the placeholder bridge vertex with the corner that
            // BuildBillboardSegment will write — we need the bridge vertex
            // to coincide with the FIRST corner of the new quad. Easiest
            // way: build the segment first into a temp, then copy.
            Vertex temp[4];
            BuildBillboardSegment(
                in.prev[i], in.current[i], basis,
                tailHalf, headHalf, colorPrev, colorCurrent,
                temp
            );
            outVertices[written - 1] = temp[0]; // bridge endpoint = first corner of new quad
            dst[0] = temp[0];
            dst[1] = temp[1];
            dst[2] = temp[2];
            dst[3] = temp[3];
        } else {
            BuildBillboardSegment(
                in.prev[i], in.current[i], basis,
                tailHalf, headHalf, colorPrev, colorCurrent,
                dst
            );
        }

        written += 4;
        haveOpenStrip = true;
    }

    return written;
}

} // namespace ribbon
} // namespace CUDA
} // namespace CatEngine

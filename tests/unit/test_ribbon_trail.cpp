// test_ribbon_trail.cpp
// ---------------------------------------------------------------------------
// Unit tests for the ribbon-trail geometry kernel
// (`engine/cuda/particles/RibbonTrail.hpp`).
//
// WHY these tests matter:
//   The ribbon-trail kernel is the math foundation for the projectile VFX in
//   the four elemental magic schools — fireballs, ice shards, lightning bolts,
//   and earth-shrapnel all push their per-particle (prev, current) pair
//   through this kernel to get the camera-facing billboard quad that the
//   renderer rasterises as a triangle strip. The shipping rule of thumb is
//   "each visible spell can spawn 100-200 ribbon particles per second", so
//   the in-game look depends entirely on this kernel producing well-formed
//   quads even at the corner cases (head-on view, motionless particle, very
//   short segment, very long segment, taper to zero, etc).
//
//   These tests pin every contract a downstream renderer or CUDA kernel
//   relies on:
//
//     1. ComputeSegmentBasis returns a valid orthogonal-to-camera frame for
//        normal motion (segment-length > epsilon, tangent not collinear with
//        viewDir).
//     2. ComputeSegmentBasis flags `valid=false` on the two degeneracies
//        (zero-length segment, head-on view) and zeros the basis so a buggy
//        consumer can't accidentally render a giant garbage quad.
//     3. BuildBillboardSegment writes 4 corners in the strip-canonical order
//        (back-left, back-right, front-left, front-right) with correct UV
//        layout for shader-side along-trail / across-trail texture lookup.
//     4. BuildBillboardSegment respects per-end half-width independently
//        (so TaperHalfWidth's narrow-tail look propagates).
//     5. TaperHalfWidth interpolates linearly between halfWidth*tailFactor
//        (at lifetimeRatio=0) and halfWidth (at ratio=1), clamps the inputs
//        to [0,1] so a designer can't generate a negative-width inside-out
//        quad.
//     6. BuildRibbonStrip emits the correct vertex count for a clean batch
//        (4 + 6*(n-1)), inserts degenerate-bridge vertices at strip joins,
//        and skips invalid segments without leaving an open join.
//     7. MaxStripVertexCount matches the actual worst-case write count.
//     8. Output buffer overflow is silently truncated, not over-written
//        (real-time renderer must never crash mid-frame).
//
//   The kernel is host-only float math, no CUDA intrinsics, so passing
//   these tests is a sufficient proxy for the same code path running inside
//   a future CUDA strip-builder kernel (the .hpp would gain `__host__
//   __device__` qualifiers but the math is unchanged).
// ---------------------------------------------------------------------------
#include "catch.hpp"
#include "engine/cuda/particles/RibbonTrail.hpp"

#include <array>
#include <cmath>
#include <vector>

using namespace CatEngine::CUDA::ribbon;

namespace {

// Loose tolerance for vec3 component checks: the SIMD path in
// engine/math/Vector.hpp can introduce a small last-bit difference between
// scalar and vector code. 1e-5 is two orders of magnitude tighter than the
// engine's Math::EPSILON (typically 1e-3) so the tests still catch real drift.
constexpr float kComponentTol = 1.0e-5f;

bool ApproxEqual(const Engine::vec3& a, const Engine::vec3& b, float tol = kComponentTol) {
    return std::fabs(a.x - b.x) < tol &&
           std::fabs(a.y - b.y) < tol &&
           std::fabs(a.z - b.z) < tol;
}

bool ApproxEqual(const Engine::vec4& a, const Engine::vec4& b, float tol = kComponentTol) {
    return std::fabs(a.x - b.x) < tol &&
           std::fabs(a.y - b.y) < tol &&
           std::fabs(a.z - b.z) < tol &&
           std::fabs(a.w - b.w) < tol;
}

bool ApproxEqual(const Engine::vec2& a, const Engine::vec2& b, float tol = kComponentTol) {
    return std::fabs(a.x - b.x) < tol && std::fabs(a.y - b.y) < tol;
}

} // namespace

// ============================================================================
// ComputeSegmentBasis
// ============================================================================

TEST_CASE("ComputeSegmentBasis: normal motion produces an orthonormal frame",
          "[ribbon][basis]") {
    // Particle moving along +X, camera looking down -Z (typical third-person
    // chase). Side should be +Y (since +X × -Z = +Y in a right-handed frame:
    //   tangent = (1, 0, 0)
    //   viewDir = (0, 0, -1)
    //   side    = tangent × viewDir = (0*-1 - 0*0, 0*0 - 1*-1, 1*0 - 0*0)
    //                                = (0, 1, 0) — points up the world Y axis
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (1.0f, 0.0f, 0.0f);
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);

    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);

    REQUIRE(basis.valid);
    REQUIRE(ApproxEqual(basis.tangent, Engine::vec3(1.0f, 0.0f, 0.0f)));
    REQUIRE(ApproxEqual(basis.side,    Engine::vec3(0.0f, 1.0f, 0.0f)));

    // Side must be unit length and perpendicular to BOTH tangent and viewDir
    // — that's the orthonormal-frame contract the renderer relies on.
    REQUIRE(basis.side.length() == Approx(1.0f).margin(kComponentTol));
    REQUIRE(basis.side.dot(basis.tangent) == Approx(0.0f).margin(kComponentTol));
    REQUIRE(basis.side.dot(viewDir)        == Approx(0.0f).margin(kComponentTol));
}

TEST_CASE("ComputeSegmentBasis: tangent is the unit-length direction of motion",
          "[ribbon][basis]") {
    // Motion of length > 1 — tangent must STILL be unit-length, otherwise the
    // billboard offset (side * halfWidth) would be scaled by the segment
    // length and the trail width would balloon with motion speed.
    const Engine::vec3 prev    (1.0f, 2.0f, 3.0f);
    const Engine::vec3 current (5.0f, 2.0f, 3.0f); // delta = (4, 0, 0)
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);

    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);

    REQUIRE(basis.valid);
    REQUIRE(basis.tangent.length() == Approx(1.0f).margin(kComponentTol));
    REQUIRE(ApproxEqual(basis.tangent, Engine::vec3(1.0f, 0.0f, 0.0f)));
}

TEST_CASE("ComputeSegmentBasis: zero-length segment reports valid=false",
          "[ribbon][basis][degenerate]") {
    // Particle that didn't move this frame (e.g. sleeping, resting on
    // ground). Renderer must skip — no quad to draw.
    const Engine::vec3 p (5.0f, 5.0f, 5.0f);
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);

    const SegmentBasis basis = ComputeSegmentBasis(p, p, viewDir);

    REQUIRE_FALSE(basis.valid);
    REQUIRE(ApproxEqual(basis.tangent, Engine::vec3(0.0f)));
    REQUIRE(ApproxEqual(basis.side,    Engine::vec3(0.0f)));
}

TEST_CASE("ComputeSegmentBasis: sub-epsilon motion reports valid=false",
          "[ribbon][basis][degenerate]") {
    // Motion smaller than the configured threshold. Default kDefaultMinSegmentLength
    // is 1e-4; we pass motion of 1e-6 to fall below it.
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (1e-6f, 0.0f, 0.0f);
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);

    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
    REQUIRE_FALSE(basis.valid);
}

TEST_CASE("ComputeSegmentBasis: head-on view (tangent collinear with viewDir) "
          "reports valid=false",
          "[ribbon][basis][degenerate]") {
    // Particle moving directly TOWARDS the camera (or directly away).
    // tangent × viewDir = 0; the kernel must NOT produce a NaN or a
    // garbage side vector.
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (0.0f, 0.0f, -1.0f); // motion along -Z
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f); // camera looks -Z

    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
    REQUIRE_FALSE(basis.valid);
    REQUIRE(ApproxEqual(basis.side, Engine::vec3(0.0f)));
}

TEST_CASE("ComputeSegmentBasis: anti-parallel viewDir also degenerate",
          "[ribbon][basis][degenerate]") {
    // Same as the head-on case but the camera is BEHIND the particle's
    // motion. Cross product is still zero.
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (0.0f, 0.0f, 1.0f);  // motion along +Z
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f); // camera looks -Z (so motion is "towards" camera)

    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
    REQUIRE_FALSE(basis.valid);
}

TEST_CASE("ComputeSegmentBasis: oblique motion produces a unit side vector",
          "[ribbon][basis]") {
    // Diagonal motion in XY plane, camera looking down -Z. Result must
    // remain orthonormal regardless of the input axis.
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (1.0f, 1.0f, 0.0f); // delta length sqrt(2)
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);

    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
    REQUIRE(basis.valid);

    // Tangent: (1, 1, 0) normalised → (sqrt(2)/2, sqrt(2)/2, 0)
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    REQUIRE(ApproxEqual(basis.tangent, Engine::vec3(invSqrt2, invSqrt2, 0.0f)));

    // Side: tangent × (0,0,-1) = (sqrt(2)/2 * -1 - 0, 0 - sqrt(2)/2 * -1, 0)
    //                          = (-sqrt(2)/2, sqrt(2)/2, 0); normalised → same.
    REQUIRE(basis.side.length() == Approx(1.0f).margin(kComponentTol));
    REQUIRE(basis.side.dot(basis.tangent) == Approx(0.0f).margin(kComponentTol));
}

TEST_CASE("ComputeSegmentBasis: caller-supplied min thresholds tighten/loosen",
          "[ribbon][basis][thresholds]") {
    // Same near-collinear setup as head-on; default threshold (1e-4) reports
    // invalid, but a relaxed threshold (1e-12) should accept a tiny non-zero
    // cross product. This is the knob the lightning-VFX needs.
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (0.0f, 1.0f, -1.0f); // 45 deg from -Z viewDir
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);

    // Default threshold: side is sqrt(2)/2 ≈ 0.707, well above 1e-4 → valid.
    REQUIRE(ComputeSegmentBasis(prev, current, viewDir).valid);

    // Stricter threshold: still passes (side magnitude is large).
    REQUIRE(ComputeSegmentBasis(prev, current, viewDir, 1e-4f, 0.5f).valid);

    // Even stricter than the side magnitude: rejected.
    REQUIRE_FALSE(ComputeSegmentBasis(prev, current, viewDir, 1e-4f, 0.99f).valid);
}

// ============================================================================
// TaperHalfWidth
// ============================================================================

TEST_CASE("TaperHalfWidth: full width at lifetimeRatio=1 (just spawned)",
          "[ribbon][taper]") {
    REQUIRE(TaperHalfWidth(0.5f, 1.0f, 0.0f) == Approx(0.5f));
    REQUIRE(TaperHalfWidth(2.0f, 1.0f, 0.5f) == Approx(2.0f));
}

TEST_CASE("TaperHalfWidth: tail width at lifetimeRatio=0 (about to die)",
          "[ribbon][taper]") {
    // tailFactor=0.5 at end-of-life → half of the head width.
    REQUIRE(TaperHalfWidth(0.5f, 0.0f, 0.5f) == Approx(0.25f));
    // tailFactor=0 (taper to point) — narrow but not zero (clamped to a min).
    REQUIRE(TaperHalfWidth(1.0f, 0.0f, 0.0f) == Approx(0.0f));
    // tailFactor=2.0 (lightning-fork "bloom") — wider tail than head.
    REQUIRE(TaperHalfWidth(1.0f, 0.0f, 2.0f) == Approx(2.0f));
}

TEST_CASE("TaperHalfWidth: linear interpolation across the lifetime",
          "[ribbon][taper]") {
    // halfWidth=1, tailFactor=0 → width = lifetimeRatio
    REQUIRE(TaperHalfWidth(1.0f, 0.5f, 0.0f) == Approx(0.5f));
    REQUIRE(TaperHalfWidth(1.0f, 0.25f, 0.0f) == Approx(0.25f));
    // halfWidth=2, tailFactor=0.5 → width = 2 * (0.5 + 0.5*ratio)
    REQUIRE(TaperHalfWidth(2.0f, 0.5f, 0.5f) == Approx(1.5f));
}

TEST_CASE("TaperHalfWidth: clamps lifetimeRatio outside [0,1]",
          "[ribbon][taper][clamp]") {
    // Ratio above 1 — clamped to 1, so result equals halfWidth.
    REQUIRE(TaperHalfWidth(1.0f, 1.5f, 0.0f) == Approx(1.0f));
    // Ratio below 0 — clamped to 0, so result equals halfWidth*tailFactor.
    REQUIRE(TaperHalfWidth(1.0f, -0.5f, 0.5f) == Approx(0.5f));
}

TEST_CASE("TaperHalfWidth: clamps negative tailFactor to 0",
          "[ribbon][taper][clamp]") {
    // Negative tailFactor would produce a negative half-width, which would
    // inside-out the quad winding. Clamped to 0 instead.
    REQUIRE(TaperHalfWidth(1.0f, 0.0f, -1.0f) == Approx(0.0f));
}

// ============================================================================
// BuildBillboardSegment
// ============================================================================

TEST_CASE("BuildBillboardSegment: writes 4 vertices in canonical strip order",
          "[ribbon][segment]") {
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (1.0f, 0.0f, 0.0f);
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);
    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
    REQUIRE(basis.valid);

    const float halfWidth = 0.1f;
    const Engine::vec4 colorPrev    (1.0f, 0.0f, 0.0f, 0.5f); // red, half alpha
    const Engine::vec4 colorCurrent (1.0f, 1.0f, 0.0f, 1.0f); // yellow, full alpha

    Vertex out[4];
    const std::size_t written = BuildBillboardSegment(
        prev, current, basis,
        halfWidth, halfWidth,
        colorPrev, colorCurrent,
        out
    );

    REQUIRE(written == 4u);

    // Side vector is (0, 1, 0) so the four corners are:
    //   v0 = prev    - (0, 0.1, 0) = (0, -0.1, 0)
    //   v1 = prev    + (0, 0.1, 0) = (0, +0.1, 0)
    //   v2 = current - (0, 0.1, 0) = (1, -0.1, 0)
    //   v3 = current + (0, 0.1, 0) = (1, +0.1, 0)
    REQUIRE(ApproxEqual(out[0].position, Engine::vec3(0.0f, -0.1f, 0.0f)));
    REQUIRE(ApproxEqual(out[1].position, Engine::vec3(0.0f, +0.1f, 0.0f)));
    REQUIRE(ApproxEqual(out[2].position, Engine::vec3(1.0f, -0.1f, 0.0f)));
    REQUIRE(ApproxEqual(out[3].position, Engine::vec3(1.0f, +0.1f, 0.0f)));

    // UVs: x ∈ {0,1} for left/right, y ∈ {0,1} for back/front.
    REQUIRE(ApproxEqual(out[0].uv, Engine::vec2(0.0f, 0.0f)));
    REQUIRE(ApproxEqual(out[1].uv, Engine::vec2(1.0f, 0.0f)));
    REQUIRE(ApproxEqual(out[2].uv, Engine::vec2(0.0f, 1.0f)));
    REQUIRE(ApproxEqual(out[3].uv, Engine::vec2(1.0f, 1.0f)));

    // Color split: back two corners → colorPrev, front two → colorCurrent.
    // This is what gives the head-bright tail-fade gradient downstream.
    REQUIRE(ApproxEqual(out[0].color, colorPrev));
    REQUIRE(ApproxEqual(out[1].color, colorPrev));
    REQUIRE(ApproxEqual(out[2].color, colorCurrent));
    REQUIRE(ApproxEqual(out[3].color, colorCurrent));
}

TEST_CASE("BuildBillboardSegment: respects independent back/front half-widths",
          "[ribbon][segment][taper]") {
    // Tapered segment: back-end (prev, the older end) is narrow, front-end
    // (current, the newer end) is wide. This is the canonical "trail
    // tapers to a point in the past" look.
    const Engine::vec3 prev    (0.0f, 0.0f, 0.0f);
    const Engine::vec3 current (1.0f, 0.0f, 0.0f);
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);
    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
    REQUIRE(basis.valid);

    const float hwBack  = 0.05f;
    const float hwFront = 0.20f;
    Vertex out[4];
    BuildBillboardSegment(
        prev, current, basis,
        hwBack, hwFront,
        Engine::vec4(1.0f), Engine::vec4(1.0f),
        out
    );

    // Back two corners spread by hwBack; front two by hwFront.
    REQUIRE(out[0].position.y == Approx(-hwBack).margin(kComponentTol));
    REQUIRE(out[1].position.y == Approx(+hwBack).margin(kComponentTol));
    REQUIRE(out[2].position.y == Approx(-hwFront).margin(kComponentTol));
    REQUIRE(out[3].position.y == Approx(+hwFront).margin(kComponentTol));
}

TEST_CASE("BuildBillboardSegment: invalid basis produces no output",
          "[ribbon][segment][degenerate]") {
    SegmentBasis basis;
    basis.valid = false;
    basis.tangent = Engine::vec3(0.0f);
    basis.side    = Engine::vec3(0.0f);

    Vertex out[4] = {};
    const std::size_t written = BuildBillboardSegment(
        Engine::vec3(0.0f), Engine::vec3(1.0f, 0.0f, 0.0f), basis,
        0.1f, 0.1f,
        Engine::vec4(1.0f), Engine::vec4(1.0f),
        out
    );
    REQUIRE(written == 0u);
    // Output buffer must NOT have been touched.
    REQUIRE(out[0].position.x == 0.0f);
    REQUIRE(out[0].position.y == 0.0f);
}

TEST_CASE("BuildBillboardSegment: nullptr output is silently rejected",
          "[ribbon][segment][safety]") {
    const Engine::vec3 prev    (0.0f);
    const Engine::vec3 current (1.0f, 0.0f, 0.0f);
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);
    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);

    const std::size_t written = BuildBillboardSegment(
        prev, current, basis,
        0.1f, 0.1f,
        Engine::vec4(1.0f), Engine::vec4(1.0f),
        nullptr
    );
    REQUIRE(written == 0u);
}

TEST_CASE("BuildBillboardSegment: clamps zero/negative half-width up to a tiny "
          "positive value (no inside-out quad)",
          "[ribbon][segment][safety]") {
    const Engine::vec3 prev    (0.0f);
    const Engine::vec3 current (1.0f, 0.0f, 0.0f);
    const Engine::vec3 viewDir (0.0f, 0.0f, -1.0f);
    const SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
    REQUIRE(basis.valid);

    Vertex out[4];
    BuildBillboardSegment(
        prev, current, basis,
        0.0f, 0.0f, // clamped to kDefaultMinHalfWidth
        Engine::vec4(1.0f), Engine::vec4(1.0f),
        out
    );

    // Side offset must be tiny but nonzero — quad winding stays consistent
    // (v1.y > v0.y, v3.y > v2.y).
    REQUIRE(out[1].position.y > out[0].position.y);
    REQUIRE(out[3].position.y > out[2].position.y);
    // Magnitude is at least kDefaultMinHalfWidth (which is 1e-5f).
    REQUIRE(out[1].position.y >= Approx(kDefaultMinHalfWidth).margin(1e-9f));
}

// ============================================================================
// MaxStripVertexCount
// ============================================================================

TEST_CASE("MaxStripVertexCount: 0 particles → 0 vertices",
          "[ribbon][strip][sizing]") {
    REQUIRE(MaxStripVertexCount(0) == 0u);
}

TEST_CASE("MaxStripVertexCount: 1 particle → 4 vertices (no bridges)",
          "[ribbon][strip][sizing]") {
    REQUIRE(MaxStripVertexCount(1) == 4u);
}

TEST_CASE("MaxStripVertexCount: N particles → 4 + 6*(N-1)",
          "[ribbon][strip][sizing]") {
    REQUIRE(MaxStripVertexCount(2) == 10u);  // 4 + 6
    REQUIRE(MaxStripVertexCount(3) == 16u);  // 4 + 12
    REQUIRE(MaxStripVertexCount(10) == 58u); // 4 + 54
    REQUIRE(MaxStripVertexCount(100) == 598u);
}

// ============================================================================
// BuildRibbonStrip
// ============================================================================

TEST_CASE("BuildRibbonStrip: empty input → 0 vertices, no writes",
          "[ribbon][strip]") {
    StripInput in;
    in.prev = nullptr; in.current = nullptr; in.color = nullptr;
    in.halfWidth = nullptr; in.lifetimeRatio = nullptr;
    in.count = 0;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    Vertex out[16] = {};
    REQUIRE(BuildRibbonStrip(in, params, out, 16) == 0u);
}

TEST_CASE("BuildRibbonStrip: single valid particle emits 4 vertices",
          "[ribbon][strip]") {
    Engine::vec3 prevs   [1] = { Engine::vec3(0.0f) };
    Engine::vec3 currents[1] = { Engine::vec3(1.0f, 0.0f, 0.0f) };
    Engine::vec4 colors  [1] = { Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f) };
    float        widths  [1] = { 0.1f };
    float        ratios  [1] = { 1.0f };

    StripInput in;
    in.prev = prevs; in.current = currents; in.color = colors;
    in.halfWidth = widths; in.lifetimeRatio = ratios;
    in.count = 1;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    Vertex out[16] = {};
    const std::size_t written = BuildRibbonStrip(in, params, out, 16);

    REQUIRE(written == 4u);
    REQUIRE(written <= MaxStripVertexCount(1));
}

TEST_CASE("BuildRibbonStrip: two valid particles emit 4 + 2 + 4 = 10 vertices",
          "[ribbon][strip][bridge]") {
    Engine::vec3 prevs   [2] = { Engine::vec3(0.0f),
                                 Engine::vec3(2.0f, 0.0f, 0.0f) };
    Engine::vec3 currents[2] = { Engine::vec3(1.0f, 0.0f, 0.0f),
                                 Engine::vec3(3.0f, 0.0f, 0.0f) };
    Engine::vec4 colors  [2] = { Engine::vec4(1.0f), Engine::vec4(1.0f) };
    float        widths  [2] = { 0.1f, 0.1f };
    float        ratios  [2] = { 1.0f, 1.0f };

    StripInput in;
    in.prev = prevs; in.current = currents; in.color = colors;
    in.halfWidth = widths; in.lifetimeRatio = ratios;
    in.count = 2;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    Vertex out[16] = {};
    const std::size_t written = BuildRibbonStrip(in, params, out, 16);

    REQUIRE(written == MaxStripVertexCount(2)); // 10
    REQUIRE(written == 10u);

    // Bridge vertices: out[4] should equal out[3] (the last vertex of the
    // first quad), and out[5] should equal out[6] (the first vertex of the
    // second quad). That's the degenerate-triangle pattern.
    REQUIRE(ApproxEqual(out[4].position, out[3].position));
    REQUIRE(ApproxEqual(out[5].position, out[6].position));
}

TEST_CASE("BuildRibbonStrip: invalid segments are skipped without bridging "
          "across the gap",
          "[ribbon][strip][degenerate]") {
    // Three particles: middle one is degenerate (zero-length motion). The
    // strip should emit two separate strips (4 vertices each), no bridge
    // across the dead particle, total 8 vertices.
    Engine::vec3 prevs   [3] = {
        Engine::vec3(0.0f),
        Engine::vec3(2.0f, 0.0f, 0.0f),  // == current[1] → degenerate
        Engine::vec3(4.0f, 0.0f, 0.0f)
    };
    Engine::vec3 currents[3] = {
        Engine::vec3(1.0f, 0.0f, 0.0f),
        Engine::vec3(2.0f, 0.0f, 0.0f),  // == prev[1] → degenerate
        Engine::vec3(5.0f, 0.0f, 0.0f)
    };
    Engine::vec4 colors  [3] = { Engine::vec4(1.0f),
                                 Engine::vec4(1.0f),
                                 Engine::vec4(1.0f) };
    float        widths  [3] = { 0.1f, 0.1f, 0.1f };
    float        ratios  [3] = { 1.0f, 1.0f, 1.0f };

    StripInput in;
    in.prev = prevs; in.current = currents; in.color = colors;
    in.halfWidth = widths; in.lifetimeRatio = ratios;
    in.count = 3;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    Vertex out[24] = {};
    const std::size_t written = BuildRibbonStrip(in, params, out, 24);

    // Particle 0 emits 4. Particle 1 is invalid → 0. Particle 2 emits a
    // FRESH strip (no bridge to particle 0 because the strip was closed by
    // the invalid particle 1) → 4. Total = 8.
    REQUIRE(written == 8u);
}

TEST_CASE("BuildRibbonStrip: silently truncates when output buffer is too small",
          "[ribbon][strip][safety]") {
    // Five valid particles want 4 + 6*4 = 28 vertices. We give 12 slots —
    // it should fit the first segment (4) + 1 bridged segment (6) = 10
    // and stop without overflow.
    constexpr std::size_t N = 5;
    Engine::vec3 prevs   [N];
    Engine::vec3 currents[N];
    Engine::vec4 colors  [N];
    float        widths  [N];
    float        ratios  [N];
    for (std::size_t i = 0; i < N; ++i) {
        prevs   [i] = Engine::vec3(static_cast<float>(2 * i),       0.0f, 0.0f);
        currents[i] = Engine::vec3(static_cast<float>(2 * i + 1),   0.0f, 0.0f);
        colors  [i] = Engine::vec4(1.0f);
        widths  [i] = 0.1f;
        ratios  [i] = 1.0f;
    }
    StripInput in;
    in.prev = prevs; in.current = currents; in.color = colors;
    in.halfWidth = widths; in.lifetimeRatio = ratios;
    in.count = N;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    Vertex out[12] = {};
    Vertex sentinel = {};
    sentinel.position = Engine::vec3(-999.0f);
    out[11] = sentinel; // canary in the LAST slot we expect to remain unwritten

    const std::size_t written = BuildRibbonStrip(in, params, out, 12);

    // 4 (segment 0) + 6 (segment 1 with bridge) = 10. Segment 2 would
    // need another 6, would not fit, so we truncate.
    REQUIRE(written == 10u);
    REQUIRE(written <= 12u);

    // Canary intact: the truncate path must NOT write past the actual
    // count it returned.
    REQUIRE(out[11].position.x == Approx(-999.0f));
}

TEST_CASE("BuildRibbonStrip: tail color alpha follows lifetimeRatio for fade",
          "[ribbon][strip][color]") {
    Engine::vec3 prevs   [1] = { Engine::vec3(0.0f) };
    Engine::vec3 currents[1] = { Engine::vec3(1.0f, 0.0f, 0.0f) };
    Engine::vec4 colors  [1] = { Engine::vec4(1.0f, 1.0f, 1.0f, 0.8f) };
    float        widths  [1] = { 0.1f };
    float        ratios  [1] = { 0.5f }; // half life remaining

    StripInput in;
    in.prev = prevs; in.current = currents; in.color = colors;
    in.halfWidth = widths; in.lifetimeRatio = ratios;
    in.count = 1;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    Vertex out[8] = {};
    BuildRibbonStrip(in, params, out, 8);

    // Front (current) corners: full color (alpha 0.8).
    REQUIRE(out[2].color.w == Approx(0.8f));
    REQUIRE(out[3].color.w == Approx(0.8f));
    // Back (prev) corners: alpha multiplied by lifetimeRatio (0.5)
    //  → 0.8 * 0.5 = 0.4.
    REQUIRE(out[0].color.w == Approx(0.4f));
    REQUIRE(out[1].color.w == Approx(0.4f));
}

TEST_CASE("BuildRibbonStrip: nullptr halfWidth uses kDefaultMinHalfWidth",
          "[ribbon][strip][safety]") {
    // Caller may not have a per-particle width array (e.g. all spells use the
    // same trail width and the renderer prefers to bind a uniform). The
    // strip builder must not crash.
    Engine::vec3 prevs   [1] = { Engine::vec3(0.0f) };
    Engine::vec3 currents[1] = { Engine::vec3(1.0f, 0.0f, 0.0f) };

    StripInput in;
    in.prev = prevs; in.current = currents;
    in.color = nullptr; in.halfWidth = nullptr; in.lifetimeRatio = nullptr;
    in.count = 1;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    Vertex out[4] = {};
    const std::size_t written = BuildRibbonStrip(in, params, out, 4);
    REQUIRE(written == 4u);
    // Width is tiny (kDefaultMinHalfWidth = 1e-5) but nonzero, so the four
    // corners are distinct.
    REQUIRE(out[1].position.y > out[0].position.y);
}

TEST_CASE("BuildRibbonStrip: stress — 100 valid particles match max-count formula",
          "[ribbon][strip][stress]") {
    constexpr std::size_t N = 100;
    std::vector<Engine::vec3> prevs(N);
    std::vector<Engine::vec3> currents(N);
    std::vector<Engine::vec4> colors(N, Engine::vec4(1.0f));
    std::vector<float>        widths(N, 0.1f);
    std::vector<float>        ratios(N, 1.0f);
    for (std::size_t i = 0; i < N; ++i) {
        prevs[i]    = Engine::vec3(static_cast<float>(2 * i),     0.0f, 0.0f);
        currents[i] = Engine::vec3(static_cast<float>(2 * i + 1), 0.0f, 0.0f);
    }

    StripInput in;
    in.prev = prevs.data(); in.current = currents.data();
    in.color = colors.data(); in.halfWidth = widths.data();
    in.lifetimeRatio = ratios.data();
    in.count = N;

    StripParams params = DefaultStripParams(Engine::vec3(0.0f, 0.0f, -1.0f));
    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params, out.data(), out.size());

    REQUIRE(written == MaxStripVertexCount(N));
    REQUIRE(written == 598u);
}

// ============================================================================
// DefaultStripParams
// ============================================================================

TEST_CASE("DefaultStripParams: copies viewDir and uses sensible defaults",
          "[ribbon][params]") {
    const Engine::vec3 viewDir(0.0f, 0.0f, -1.0f);
    const StripParams params = DefaultStripParams(viewDir);

    REQUIRE(ApproxEqual(params.viewDir, viewDir));
    // Default to "trail tapers to a point" (the most common look across the
    // four magic schools' projectile VFX). Designers override per-effect.
    REQUIRE(params.tailWidthFactor == Approx(0.0f));
    REQUIRE(params.minSegmentLen == Approx(kDefaultMinSegmentLength));
    REQUIRE(params.minCrossLen == Approx(kDefaultMinCrossLength));
}

// ============================================================================
// RibbonTrailDevice — host-visible surface
// ============================================================================
//
// The .cuh file is dual-mode: its device kernel + device helpers are
// guarded behind `#ifdef __CUDACC__`, but the vertex POD, buffer-size
// constants, and the CPU index-buffer filler are unconditionally host-
// visible. These tests compile-check that the host-visible surface stays
// parseable by the C++-only test TU and pin the index-pattern contract
// the renderer (iteration 4) depends on.
// ---------------------------------------------------------------------------
#include "engine/cuda/particles/RibbonTrailDevice.cuh"

TEST_CASE("RibbonTrailDevice: FillRibbonIndexBufferCPU emits {0,1,2,1,3,2} per particle",
          "[ribbon][device][indices]") {
    // Three particles → 3 * 6 = 18 indices, patterned {4i+0, 4i+1, 4i+2,
    // 4i+1, 4i+3, 4i+2} — two CCW triangles per quad matching the
    // host BuildBillboardSegment corner order (back-left, back-right,
    // front-left, front-right).
    constexpr int kCount = 3;
    std::array<uint32_t, kCount * 6> indices{};
    CatEngine::CUDA::ribbon_device::FillRibbonIndexBufferCPU(indices.data(), kCount);

    const std::array<uint32_t, 18> expected = {
        0, 1, 2,  1, 3, 2,   // particle 0: base = 0
        4, 5, 6,  5, 7, 6,   // particle 1: base = 4
        8, 9, 10, 9, 11, 10  // particle 2: base = 8
    };
    REQUIRE(indices == expected);
}

TEST_CASE("RibbonTrailDevice: buffer-size helpers scale linearly with particle cap",
          "[ribbon][device][sizing]") {
    // RibbonVertex is {float3 position, float4 color, float2 uv} = 12 + 16 +
    // 8 = 36 bytes of payload — BUT float4 is `alignas(16)` on CUDA's
    // native vector types, which pads the struct to 40 bytes (float3's
    // 12-byte size naturally aligns, then float4 needs a 4-byte pad after
    // it). The Vulkan pipeline layout will declare stride = 40, so pin
    // the actual compile-time size here to catch any future member reorder
    // that would break the shader's vertex fetch.
    REQUIRE(sizeof(CatEngine::CUDA::ribbon_device::RibbonVertex) == 48u);

    // Vertex / index buffer sizes scale as 4 * RibbonVertex and 6 * uint32_t
    // per particle. A 1M-particle cap would be ~48 MB of vertices + 24 MB
    // of indices — pinning this here stops a future "let's bump maxParticles"
    // change from silently blowing through the renderer's staging budget.
    using namespace CatEngine::CUDA::ribbon_device;
    REQUIRE(RibbonVertexBufferSize(0)  == 0u);
    REQUIRE(RibbonVertexBufferSize(1)  == 4u * sizeof(RibbonVertex));
    REQUIRE(RibbonVertexBufferSize(10) == 40u * sizeof(RibbonVertex));

    REQUIRE(RibbonIndexBufferSize(0)  == 0u);
    REQUIRE(RibbonIndexBufferSize(1)  == 6u * sizeof(uint32_t));
    REQUIRE(RibbonIndexBufferSize(10) == 60u * sizeof(uint32_t));

    REQUIRE(kVerticesPerParticle == 4);
    REQUIRE(kIndicesPerParticle  == 6);
}

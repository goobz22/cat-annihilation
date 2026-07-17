// test_cuda_property_ribbon.cpp
// ---------------------------------------------------------------------------
// Property tests for the ribbon-trail geometry kernel
// (engine/cuda/particles/RibbonTrail.hpp). Complements test_ribbon_trail.cpp
// with LARGE-N batch invariants — the kind of contract that the renderer's
// vertex-buffer-fill loop relies on across hundreds of simultaneous
// projectile trails.
//
// Properties locked here:
//   1. Vertex-count formula: BuildRibbonStrip with N=1..100 all-valid
//      particles produces EXACTLY MaxStripVertexCount(N) vertices.
//   2. UV monotonicity: across a full N-particle strip, the front-edge
//      uv.y of segment i equals 1 and the back-edge uv.y of segment i+1
//      equals 0 — the per-segment 0/1 mapping is consistent at every
//      bridge.
//   3. Color endpoint exact: the strip's first two vertices carry the
//      tail color (alpha-faded by lifetime), the last two carry the head
//      color, and every segment respects this endpoint contract.
//   4. Degenerate-particle bridge: a zero-motion particle in the middle
//      of a batch breaks the open strip (no bridge into / out of the
//      degenerate particle); the next valid particle starts a fresh
//      4-vertex quad with no bridging pair.
//
// These are STAND-ALONE from test_ribbon_trail.cpp — that file pins single-
// segment / unit-input contracts; this file pins multi-segment / batch /
// monotonicity contracts.
// ---------------------------------------------------------------------------
#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/cuda/particles/RibbonTrail.hpp"

#include <cmath>
#include <random>
#include <vector>

using Engine::vec3;
using Engine::vec4;
using Engine::vec2;
using namespace CatEngine::CUDA::ribbon;

namespace {

constexpr float kRibbonEps = 1e-4f;

// Build a "happy-path" StripInput where every particle has clear motion
// perpendicular to the camera and a stable per-particle half-width. Returns
// the backing arrays through out-params so the caller can hold the lifetime
// and pass the StripInput pointer block.
struct StripScene {
    std::vector<vec3>  prev;
    std::vector<vec3>  curr;
    std::vector<vec4>  color;
    std::vector<float> halfWidth;
    std::vector<float> lifetimeRatio;
};

StripScene makeLinearStrip(std::size_t count, float spacing = 1.0f,
                           float halfWidth = 0.05f) {
    StripScene scene;
    scene.prev.resize(count);
    scene.curr.resize(count);
    scene.color.resize(count);
    scene.halfWidth.resize(count);
    scene.lifetimeRatio.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        // Particle i sits at x = i*spacing; motion is along +x of length
        // spacing — a chain of touching segments that the renderer would
        // draw as one straight line. The chain is wide-open in the x-y plane
        // and uses a +z camera so the side-vector is along +y.
        const float xPrev = static_cast<float>(i) * spacing;
        const float xCurr = xPrev + spacing;
        scene.prev[i] = vec3(xPrev, 0.0f, 0.0f);
        scene.curr[i] = vec3(xCurr, 0.0f, 0.0f);
        scene.color[i] = vec4(1.0f, 0.5f, 0.25f, 1.0f);
        scene.halfWidth[i] = halfWidth;
        scene.lifetimeRatio[i] = 1.0f; // full alpha for the head/tail color test
    }
    return scene;
}

StripInput stripInputFrom(const StripScene& scene) {
    StripInput in;
    in.prev          = scene.prev.data();
    in.current       = scene.curr.data();
    in.color         = scene.color.data();
    in.halfWidth     = scene.halfWidth.data();
    in.lifetimeRatio = scene.lifetimeRatio.data();
    in.count         = scene.prev.size();
    return in;
}

} // anon

// ---------------------------------------------------------------------------
// Property 1: vertex-count formula matches MaxStripVertexCount for every
// N in [1, 100]. This is the formula the renderer pre-allocates against;
// drift here = vertex-buffer overrun in the worst case.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: BuildRibbonStrip vertex count == MaxStripVertexCount for N=1..100",
          "[ribbon][property]") {
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    params.tailWidthFactor = 1.0f; // disable taper so half-widths stay equal

    for (std::size_t n = 1; n <= 100; ++n) {
        StripScene scene = makeLinearStrip(n);
        StripInput in = stripInputFrom(scene);

        const std::size_t cap = MaxStripVertexCount(n);
        std::vector<Vertex> out(cap);
        const std::size_t written = BuildRibbonStrip(in, params,
                                                      out.data(), out.size());

        REQUIRE(written == cap);
        // Formula sanity check: 4 + 6*(n-1).
        REQUIRE(written == 4u + 6u * (n - 1u));
    }
}

// ---------------------------------------------------------------------------
// Property 1b: MaxStripVertexCount(0) is exactly 0 (no vertices for an
// empty batch). This is the boundary case the renderer hits between waves.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: MaxStripVertexCount(0) is 0",
          "[ribbon][property]") {
    REQUIRE(MaxStripVertexCount(0) == 0u);
    // And the resulting strip writes zero vertices into the output.
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    StripScene scene = makeLinearStrip(0);
    StripInput in = stripInputFrom(scene);
    std::vector<Vertex> out(16);
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());
    REQUIRE(written == 0u);
}

// ---------------------------------------------------------------------------
// Property 2: MaxStripVertexCount monotonically grows with N. Pin the
// formula's monotonicity so a regression that flips a sign in the formula
// (e.g. 4 + 6*(n-1) → 4 + 6*n - 6 typo'd as 4*n + 6) surfaces here.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: MaxStripVertexCount is strictly increasing in N",
          "[ribbon][property]") {
    for (std::size_t n = 0; n < 1000; ++n) {
        const std::size_t a = MaxStripVertexCount(n);
        const std::size_t b = MaxStripVertexCount(n + 1);
        REQUIRE(a < b);
    }
}

// ---------------------------------------------------------------------------
// Property 3: UV monotonicity along the strip — every segment writes
// uv.y in {0, 0, 1, 1} on its four corners (back-left, back-right,
// front-left, front-right). Across a multi-segment strip we exact-check
// this at every per-quad slot.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: per-segment UV layout is uv.y ∈ {0, 0, 1, 1}",
          "[ribbon][property]") {
    constexpr std::size_t N = 30;
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    params.tailWidthFactor = 1.0f;
    StripScene scene = makeLinearStrip(N);
    StripInput in = stripInputFrom(scene);

    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());
    REQUIRE(written == MaxStripVertexCount(N));

    // The strip layout for N segments is:
    //   [Q0_0 Q0_1 Q0_2 Q0_3]    (4 vertices, first quad)
    //   [B0_0 B0_1 Q1_0 Q1_1 Q1_2 Q1_3]   (2 bridge + 4 quad each subsequent)
    //   ...
    // Each "quad" has uv pattern (0,0)/(1,0)/(0,1)/(1,1).
    std::size_t cursor = 0;
    for (std::size_t seg = 0; seg < N; ++seg) {
        if (seg > 0) {
            // The 2 bridge vertices: we don't assert their UVs since they
            // are degenerate-area duplicates of neighbours. Just skip past.
            cursor += 2;
        }
        REQUIRE(cursor + 4 <= written);
        REQUIRE(out[cursor + 0].uv.y == 0.0f);
        REQUIRE(out[cursor + 1].uv.y == 0.0f);
        REQUIRE(out[cursor + 2].uv.y == 1.0f);
        REQUIRE(out[cursor + 3].uv.y == 1.0f);
        REQUIRE(out[cursor + 0].uv.x == 0.0f); // left
        REQUIRE(out[cursor + 1].uv.x == 1.0f); // right
        REQUIRE(out[cursor + 2].uv.x == 0.0f); // left
        REQUIRE(out[cursor + 3].uv.x == 1.0f); // right
        cursor += 4;
    }
    REQUIRE(cursor == written);
}

// ---------------------------------------------------------------------------
// Property 4: color gradient endpoints exact.
//
// Per kernel doc: the back two corners (v0, v1) carry colorPrev = current
// color with alpha multiplied by lifetimeRatio; the front two corners (v2,
// v3) carry colorCurrent. Pin this exactly across a 20-segment strip with
// varying lifetime ratios.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: per-quad color endpoints are tail/head split",
          "[ribbon][property]") {
    constexpr std::size_t N = 20;
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    params.tailWidthFactor = 1.0f;
    StripScene scene = makeLinearStrip(N);
    // Vary alpha + lifetime per particle so the head/tail split is
    // distinguishable at every segment.
    for (std::size_t i = 0; i < N; ++i) {
        scene.color[i] = vec4(0.1f * static_cast<float>(i), 0.5f, 0.5f, 1.0f);
        scene.lifetimeRatio[i] = 1.0f - 0.04f * static_cast<float>(i);
    }
    StripInput in = stripInputFrom(scene);
    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());

    std::size_t cursor = 0;
    for (std::size_t seg = 0; seg < N; ++seg) {
        if (seg > 0) cursor += 2; // skip bridge
        const float ratio = scene.lifetimeRatio[seg];
        const vec4 headColor = scene.color[seg];
        const vec4 tailColor(headColor.x, headColor.y, headColor.z,
                             headColor.w * ratio);
        // Back two vertices = tail color, front two = head color.
        REQUIRE(std::fabs(out[cursor + 0].color.x - tailColor.x) < kRibbonEps);
        REQUIRE(std::fabs(out[cursor + 0].color.w - tailColor.w) < kRibbonEps);
        REQUIRE(std::fabs(out[cursor + 1].color.w - tailColor.w) < kRibbonEps);
        REQUIRE(std::fabs(out[cursor + 2].color.w - headColor.w) < kRibbonEps);
        REQUIRE(std::fabs(out[cursor + 3].color.w - headColor.w) < kRibbonEps);
        REQUIRE(std::fabs(out[cursor + 2].color.x - headColor.x) < kRibbonEps);
        cursor += 4;
    }
    REQUIRE(cursor == written);
}

// ---------------------------------------------------------------------------
// Property 5: degenerate-particle bridge skips correctly.
//
// Place a degenerate particle (prev == current, zero motion) in the
// MIDDLE of a 5-segment batch. The strip should be:
//   - segments 0..1 form an open strip with 1 bridge pair between them
//   - segment 2 is degenerate, closes the strip, contributes 0 vertices
//   - segment 3 STARTS a fresh open strip with no bridge into it
//   - segment 4 has a bridge pair into segment 3
// So the total vertex count is:
//   4 (seg 0)  + 2 (bridge) + 4 (seg 1)  ← strip A
//   + 0 (seg 2)
//   + 4 (seg 3) + 2 (bridge) + 4 (seg 4) ← strip B
//   = 20 vertices.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: degenerate middle particle splits the strip cleanly",
          "[ribbon][property]") {
    constexpr std::size_t N = 5;
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    params.tailWidthFactor = 1.0f;
    StripScene scene = makeLinearStrip(N);

    // Segment 2: degenerate — prev coincides with current.
    scene.prev[2] = vec3(2.5f, 0.0f, 0.0f);
    scene.curr[2] = vec3(2.5f, 0.0f, 0.0f);
    StripInput in = stripInputFrom(scene);

    // Output capacity = worst case. The actual written count will be
    // SHORTER because seg 2 contributes 0.
    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());

    // Expected: 4 + 2 + 4 + 4 + 2 + 4 = 20.
    REQUIRE(written == 20u);

    // Strip A: seg 0 vertices [0..3], bridge [4..5], seg 1 [6..9].
    // Strip B: seg 3 vertices [10..13] (NO bridge in front), bridge [14..15],
    // seg 4 vertices [16..19].
    // We don't have a public marker for the strip boundary; the contract is
    // that the vertex at index 10 (start of seg 3) is the first corner of
    // seg 3's quad, NOT a bridge from seg 1. So out[10].uv.y must be 0
    // (back-left of a fresh quad).
    REQUIRE(out[10].uv.y == 0.0f);
    REQUIRE(out[10].uv.x == 0.0f);
}

// ---------------------------------------------------------------------------
// Property 5b: all-degenerate batch produces zero vertices. Renderer
// must not over-write its vertex buffer when every particle is invalid.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: all-degenerate batch writes zero vertices",
          "[ribbon][property]") {
    constexpr std::size_t N = 10;
    StripScene scene = makeLinearStrip(N);
    for (std::size_t i = 0; i < N; ++i) {
        scene.prev[i] = vec3(0.0f);
        scene.curr[i] = vec3(0.0f);
    }
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());
    REQUIRE(written == 0u);
}

// ---------------------------------------------------------------------------
// Property 6: leading-degenerate batch starts cleanly at the first valid
// particle. Tests the "first valid sets haveOpenStrip" branch.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: leading degenerate particles are skipped",
          "[ribbon][property]") {
    constexpr std::size_t N = 5;
    StripScene scene = makeLinearStrip(N);
    // First three particles are degenerate; particles 3 and 4 are valid.
    for (std::size_t i = 0; i < 3; ++i) {
        scene.prev[i] = vec3(0.0f);
        scene.curr[i] = vec3(0.0f);
    }
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());
    // Two valid segments → 4 + 6 = 10 vertices.
    REQUIRE(written == 10u);
    // First vertex is the fresh-quad back-left.
    REQUIRE(out[0].uv.y == 0.0f);
    REQUIRE(out[0].uv.x == 0.0f);
}

// ---------------------------------------------------------------------------
// Property 7: BuildBillboardSegment writes EXACTLY 4 vertices when basis
// is valid, EXACTLY 0 when basis is invalid. Pins the API contract.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: BuildBillboardSegment returns 4 on valid, 0 on invalid",
          "[ribbon][property]") {
    Vertex outVerts[4];
    const vec3 viewDir(0.0f, 0.0f, 1.0f);

    // Valid basis case.
    SegmentBasis goodBasis = ComputeSegmentBasis(
        vec3(0.0f), vec3(1.0f, 0.0f, 0.0f), viewDir);
    REQUIRE(goodBasis.valid);
    const std::size_t goodN = BuildBillboardSegment(
        vec3(0.0f), vec3(1.0f, 0.0f, 0.0f), goodBasis,
        0.1f, 0.1f, vec4(1.0f), vec4(1.0f), outVerts);
    REQUIRE(goodN == 4u);

    // Invalid basis case (zero-motion segment).
    SegmentBasis badBasis = ComputeSegmentBasis(
        vec3(0.0f), vec3(0.0f), viewDir);
    REQUIRE_FALSE(badBasis.valid);
    const std::size_t badN = BuildBillboardSegment(
        vec3(0.0f), vec3(0.0f), badBasis,
        0.1f, 0.1f, vec4(1.0f), vec4(1.0f), outVerts);
    REQUIRE(badN == 0u);

    // Null output pointer returns 0 too.
    const std::size_t nullN = BuildBillboardSegment(
        vec3(0.0f), vec3(1.0f, 0.0f, 0.0f), goodBasis,
        0.1f, 0.1f, vec4(1.0f), vec4(1.0f), nullptr);
    REQUIRE(nullN == 0u);
}

// ---------------------------------------------------------------------------
// Property 8: vertex positions are mirror-symmetric across the tangent.
//
// For a valid segment with halfWidthBack == halfWidthFront, the four
// corners are arranged as a parallelogram. The midpoint of (v0, v1) lies
// on the prev position; the midpoint of (v2, v3) lies on the current
// position. This is the geometric definition of the camera-facing quad —
// pin it across 500 random configurations.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: corner-pair midpoints land on the prev/current samples",
          "[ribbon][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_ribbon:0xab1"));
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
    std::uniform_real_distribution<float> halfWidthDist(0.01f, 0.5f);

    for (int trial = 0; trial < 500; ++trial) {
        const vec3 prev   (dist(rng), dist(rng), dist(rng));
        const vec3 current(dist(rng), dist(rng), dist(rng));
        const vec3 viewDir = vec3(0.0f, 0.0f, 1.0f); // fixed camera

        SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
        if (!basis.valid) continue; // skip degenerate random samples

        const float hw = halfWidthDist(rng);
        Vertex out[4];
        const std::size_t n = BuildBillboardSegment(
            prev, current, basis, hw, hw,
            vec4(1.0f), vec4(1.0f), out);
        REQUIRE(n == 4u);

        const vec3 midBack  = (out[0].position + out[1].position) * 0.5f;
        const vec3 midFront = (out[2].position + out[3].position) * 0.5f;
        REQUIRE(std::fabs(midBack.x  - prev.x)    < kRibbonEps);
        REQUIRE(std::fabs(midBack.y  - prev.y)    < kRibbonEps);
        REQUIRE(std::fabs(midBack.z  - prev.z)    < kRibbonEps);
        REQUIRE(std::fabs(midFront.x - current.x) < kRibbonEps);
        REQUIRE(std::fabs(midFront.y - current.y) < kRibbonEps);
        REQUIRE(std::fabs(midFront.z - current.z) < kRibbonEps);
    }
}

// ---------------------------------------------------------------------------
// Property 9: tangent vector is unit length for any valid basis.
// Pin the ComputeSegmentBasis normalisation contract under random input.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: ComputeSegmentBasis tangent and side are unit length",
          "[ribbon][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_ribbon:0xab2"));
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);

    int validCount = 0;
    for (int trial = 0; trial < 1000; ++trial) {
        vec3 prev   (dist(rng), dist(rng), dist(rng));
        vec3 current(dist(rng), dist(rng), dist(rng));
        // Force a non-trivial motion: add an offset if prev and current
        // happen to coincide for this random draw.
        if ((current - prev).length() < 0.01f) {
            current = current + vec3(1.0f, 0.0f, 0.0f);
        }
        const vec3 viewDir(0.0f, 0.0f, 1.0f);
        SegmentBasis basis = ComputeSegmentBasis(prev, current, viewDir);
        if (!basis.valid) continue;
        ++validCount;
        REQUIRE(std::fabs(basis.tangent.length() - 1.0f) < kRibbonEps);
        REQUIRE(std::fabs(basis.side.length()    - 1.0f) < kRibbonEps);
        // Side is perpendicular to viewDir (by construction: cross product).
        REQUIRE(std::fabs(basis.side.dot(viewDir)) < kRibbonEps);
        // Side is also perpendicular to tangent.
        REQUIRE(std::fabs(basis.side.dot(basis.tangent)) < kRibbonEps);
    }
    REQUIRE(validCount > 900); // overwhelmingly most random trials are valid
}

// ---------------------------------------------------------------------------
// Property 10: TaperHalfWidth clamps input ratio to [0, 1].
//
// Negative ratios and ratios > 1 should clamp; the output must still be
// non-negative and finite. Pin the clamp contract under random over- and
// under-flow ratios.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: TaperHalfWidth clamps lifetime ratio to [0, 1]",
          "[ribbon][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_ribbon:0xab3"));
    std::uniform_real_distribution<float> hwDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> ratioDist(-10.0f, 10.0f);
    std::uniform_real_distribution<float> tailDist(0.0f, 2.0f);

    for (int trial = 0; trial < 1000; ++trial) {
        const float hw    = hwDist(rng);
        const float ratio = ratioDist(rng);
        const float tail  = tailDist(rng);
        const float result = TaperHalfWidth(hw, ratio, tail);
        REQUIRE(std::isfinite(result));
        REQUIRE(result >= 0.0f);
        // At ratio >= 1 we expect halfWidth * 1 = halfWidth.
        if (ratio >= 1.0f) {
            REQUIRE(std::fabs(result - hw) < kRibbonEps);
        }
        // At ratio <= 0 we expect halfWidth * tailFactor.
        if (ratio <= 0.0f) {
            REQUIRE(std::fabs(result - hw * tail) < kRibbonEps);
        }
    }
}

// ---------------------------------------------------------------------------
// Property 11: TaperHalfWidth interpolates linearly in [0, 1]. Verify the
// interior of the clamp range matches the analytic formula
// scale = tail + (1 - tail) * ratio.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: TaperHalfWidth is linear in ratio on [0, 1]",
          "[ribbon][property]") {
    constexpr float hw = 0.5f;
    constexpr float tail = 0.3f;
    for (int i = 0; i <= 100; ++i) {
        const float ratio = i / 100.0f;
        const float expected = hw * (tail + (1.0f - tail) * ratio);
        const float actual   = TaperHalfWidth(hw, ratio, tail);
        REQUIRE(std::fabs(actual - expected) < kRibbonEps);
    }
}

// ---------------------------------------------------------------------------
// Property 12: undersized output buffer is silently truncated (renderer
// safety). Pin that BuildRibbonStrip never writes past the capacity.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: undersized output buffer truncates without overflow",
          "[ribbon][property]") {
    constexpr std::size_t N = 10;
    StripScene scene = makeLinearStrip(N);
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    params.tailWidthFactor = 1.0f;

    // Capacities below the worst case: every test must NOT overflow and
    // must return a count <= cap.
    for (std::size_t cap : { (std::size_t)4, (std::size_t)5, (std::size_t)10,
                              (std::size_t)15, (std::size_t)30 }) {
        // Allocate one extra slot we can sentinel-check after the call —
        // if the kernel overflows, the sentinel is corrupted.
        std::vector<Vertex> out(cap + 1);
        Vertex sentinel{};
        sentinel.position = vec3(-99999.0f);
        out[cap] = sentinel;
        const std::size_t written = BuildRibbonStrip(in, params,
                                                      out.data(), cap);
        REQUIRE(written <= cap);
        // Sentinel intact — no overflow.
        REQUIRE(out[cap].position.x == sentinel.position.x);
    }
}

// ---------------------------------------------------------------------------
// Property 13: head-on view (tangent parallel to viewDir) flags invalid
// basis. Pin the second degeneracy branch in ComputeSegmentBasis.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: head-on view produces invalid basis",
          "[ribbon][property]") {
    // Motion along +z, camera also looking +z → tangent collinear with
    // viewDir, cross product collapses, basis invalid.
    SegmentBasis basis = ComputeSegmentBasis(
        vec3(0.0f), vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 0.0f, 1.0f));
    REQUIRE_FALSE(basis.valid);
    REQUIRE(basis.side.length() < kRibbonEps);
}

// ---------------------------------------------------------------------------
// Property 14: passing null SoA pointers in StripInput is handled — the
// kernel falls back to defaults for missing color/half-width/lifetime
// streams. The renderer can pass a partially-populated SoA when only the
// motion buffers matter.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: missing optional SoA streams fall back to defaults",
          "[ribbon][property]") {
    constexpr std::size_t N = 3;
    StripScene scene = makeLinearStrip(N);
    StripInput in;
    in.prev          = scene.prev.data();
    in.current       = scene.curr.data();
    in.color         = nullptr;
    in.halfWidth     = nullptr;
    in.lifetimeRatio = nullptr;
    in.count         = N;

    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());
    // 3 valid segments → 4 + 2*6 = 16 vertices.
    REQUIRE(written == 16u);
    // Default color is white; default alpha = 1 * ratio (ratio defaults
    // to 1) = 1.
    REQUIRE(std::fabs(out[0].color.w - 1.0f) < kRibbonEps);
}

// ---------------------------------------------------------------------------
// Property 15: ApplyImpulse-style positional invariants in the strip.
// For a straight chain of identical motions, every prev vertex lies on
// the line from the first prev to the last current. Pin this geometric
// invariant on a 50-segment straight strip.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: straight-chain strip vertices stay on the chain line",
          "[ribbon][property]") {
    constexpr std::size_t N = 50;
    StripScene scene = makeLinearStrip(N);
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    params.tailWidthFactor = 1.0f;

    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());

    // For a chain along +x in the x-y plane (z=0) with camera +z, the
    // side vector is +/-y, so all vertices have z = 0 exactly. The y
    // coordinate of every back/front vertex equals +/- halfWidth.
    // Run that check across the whole strip.
    for (std::size_t i = 0; i < written; ++i) {
        REQUIRE(std::fabs(out[i].position.z) < kRibbonEps);
        REQUIRE(std::fabs(std::fabs(out[i].position.y) - 0.05f) < kRibbonEps);
    }
}

// ---------------------------------------------------------------------------
// Property 16: capacity 0 returns 0 written even with non-zero input.
// Pin the early-out path.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: zero capacity early-outs without touching memory",
          "[ribbon][property]") {
    StripScene scene = makeLinearStrip(5);
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    Vertex dummy[4];
    const std::size_t written = BuildRibbonStrip(in, params, dummy, 0u);
    REQUIRE(written == 0u);
}

// ---------------------------------------------------------------------------
// Property 17: null output pointer returns 0 (renderer-safety).
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: null output pointer returns 0",
          "[ribbon][property]") {
    StripScene scene = makeLinearStrip(5);
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    const std::size_t written = BuildRibbonStrip(in, params, nullptr, 100u);
    REQUIRE(written == 0u);
}

// ---------------------------------------------------------------------------
// Property 18: stress 1000-particle batch — all valid, all linear motion.
// The renderer's per-frame vertex-buffer fill loop calls this with
// hundreds of particles; pin no crash, no NaN, exact vertex count.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: 1000-particle batch produces no NaN vertices",
          "[ribbon][property]") {
    constexpr std::size_t N = 1000;
    StripScene scene = makeLinearStrip(N);
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    params.tailWidthFactor = 1.0f;

    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());
    REQUIRE(written == MaxStripVertexCount(N));
    for (std::size_t i = 0; i < written; ++i) {
        REQUIRE(std::isfinite(out[i].position.x));
        REQUIRE(std::isfinite(out[i].position.y));
        REQUIRE(std::isfinite(out[i].position.z));
        REQUIRE(std::isfinite(out[i].color.w));
        REQUIRE(std::isfinite(out[i].uv.x));
        REQUIRE(std::isfinite(out[i].uv.y));
    }
}

// ---------------------------------------------------------------------------
// Property 19: tiny half-width (below the floor) clamps to the minimum,
// not zero. Pin the BuildBillboardSegment clamp contract.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: zero half-width clamps to minimum non-zero",
          "[ribbon][property]") {
    SegmentBasis basis = ComputeSegmentBasis(
        vec3(0.0f), vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(basis.valid);
    Vertex out[4];
    const std::size_t n = BuildBillboardSegment(
        vec3(0.0f), vec3(1.0f, 0.0f, 0.0f), basis,
        0.0f, 0.0f, vec4(1.0f), vec4(1.0f), out);
    REQUIRE(n == 4u);
    // After clamping, the quad has a NON-ZERO area (renderer doesn't see
    // a NaN-spew zero-area quad).
    const float widthBack = (out[1].position - out[0].position).length();
    const float widthFront = (out[3].position - out[2].position).length();
    REQUIRE(widthBack  > 0.0f);
    REQUIRE(widthFront > 0.0f);
    REQUIRE(widthBack  < 1e-3f); // very small but not zero
    REQUIRE(widthFront < 1e-3f);
}

// ---------------------------------------------------------------------------
// Property 20: degenerate-then-valid-then-degenerate sequence produces
// only the valid segment's 4 vertices. Combines the degeneracy edge cases.
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: dgn-valid-dgn sequence yields 4 vertices only",
          "[ribbon][property]") {
    constexpr std::size_t N = 3;
    StripScene scene = makeLinearStrip(N);
    scene.prev[0] = vec3(0.0f); scene.curr[0] = vec3(0.0f); // dgn
    scene.prev[2] = vec3(5.0f); scene.curr[2] = vec3(5.0f); // dgn
    // particle 1 stays as set by makeLinearStrip: valid motion.
    StripInput in = stripInputFrom(scene);
    StripParams params = DefaultStripParams(vec3(0.0f, 0.0f, 1.0f));
    std::vector<Vertex> out(MaxStripVertexCount(N));
    const std::size_t written = BuildRibbonStrip(in, params,
                                                  out.data(), out.size());
    REQUIRE(written == 4u);
}

// ---------------------------------------------------------------------------
// Property 21: long-segment strip — motion magnitude up to 100 m doesn't
// break the basis or produce NaN. Pin no overflow in very-large-motion
// edge case (e.g. an instantaneous teleport-respawn that produces a huge
// (prev, current) gap).
// ---------------------------------------------------------------------------
TEST_CASE("Ribbon property: large-motion segments produce finite output",
          "[ribbon][property]") {
    SegmentBasis basis = ComputeSegmentBasis(
        vec3(0.0f), vec3(100.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(basis.valid);
    REQUIRE(std::fabs(basis.tangent.length() - 1.0f) < kRibbonEps);
    Vertex out[4];
    const std::size_t n = BuildBillboardSegment(
        vec3(0.0f), vec3(100.0f, 0.0f, 0.0f), basis,
        0.1f, 0.1f, vec4(1.0f), vec4(1.0f), out);
    REQUIRE(n == 4u);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(std::isfinite(out[i].position.x));
        REQUIRE(std::isfinite(out[i].position.y));
        REQUIRE(std::isfinite(out[i].position.z));
    }
}

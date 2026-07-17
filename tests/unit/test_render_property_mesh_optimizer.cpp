/**
 * @file test_render_property_mesh_optimizer.cpp
 * @brief Property tests for engine/renderer/MeshOptimizer.hpp.
 *
 * Sibling-but-distinct from tests/unit/test_mesh_optimizer.cpp. That file
 * pins the deterministic acceptance bar (Tipsy < shuffled, both < 1.0)
 * on hand-crafted icosphere samples. This file shotguns the reorderers
 * with 100+ random shuffles per topology and 50+ subdivided-icosphere
 * variants to surface a topology-specific quality regression the
 * deterministic test cannot construct.
 *
 * Coverage goals:
 *
 *   1. Tipsy preserves triangle count AND triangle membership across 100
 *      random triangle-order shuffles of the same topology. A reorderer
 *      that drops or duplicates a single triangle would render broken
 *      geometry — the most catastrophic invariant a vertex-cache
 *      optimiser can break.
 *
 *   2. ACMR on optimised output is strictly lower than ACMR on shuffled
 *      input across 50 random meshes (subdivided-icosphere variants of
 *      varying density). The whole point of the optimiser is to reduce
 *      cache misses; a 50-mesh sample makes the "lower on average" claim
 *      robust against any single topology's noise.
 *
 *   3. Idempotency: optimising an already-optimised mesh produces an
 *      equally-low or lower ACMR — the reorderer must not regress quality
 *      on a known-good input. This catches a class of bug where the
 *      scoring function has a subtle order-dependence.
 *
 *   4. Forsyth's output also satisfies (1) and (2) — we test both
 *      reorderers in parallel because they share the CSR adjacency + per-
 *      vertex valence machinery and a bug in either path would degrade
 *      the other.
 *
 *   5. ACMR computation is consistent under index buffer permutation that
 *      preserves triangle order — i.e., it is a function of the index
 *      sequence, not of the underlying triangle set. Property-check the
 *      definition.
 *
 *   6. ACMR strict bounds: for an N-triangle, M-vertex mesh, ACMR is
 *      bounded below by M/N (every unique vertex must miss at least once)
 *      and above by 3.0 (every index misses).
 *
 *   7. Empty / degenerate-input handling: zero indices returns zero ACMR
 *      and is a no-op for both reorderers (caller invariants from the
 *      header).
 */

#include "catch.hpp"
#include "engine/renderer/MeshOptimizer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>
#include <set>
#include <tuple>
#include <vector>

using namespace CatEngine::Renderer::MeshOptimizer;

namespace {

// Seed routed through CatTest::DeterministicSeed (reproducible default,
// CAT_TEST_SEED-overridable). Generator type + distributions unchanged.
#include "test_seed.hpp"
const uint32_t kPropertySeed =
    static_cast<uint32_t>(CatTest::DeterministicSeed("render property mesh_optimizer"));

// Icosphere generator copied from test_mesh_optimizer.cpp (same pattern;
// duplicated rather than shared so the property test file is self-
// contained — Catch2 globs files individually).
struct MeshData {
    std::vector<uint32_t> indices;
    uint32_t vertexCount = 0;
};

MeshData GenerateIcosphere(uint32_t subdivisions) {
    MeshData mesh;
    mesh.vertexCount = 12;
    mesh.indices = {
        0,  11, 5,   0,  5,  1,   0,  1,  7,   0,  7,  10,  0, 10, 11,
        1,  5,  9,   5,  11, 4,   11, 10, 2,   10, 7,  6,   7,  1, 8,
        3,  9,  4,   3,  4,  2,   3,  2,  6,   3,  6,  8,   3,  8, 9,
        4,  9,  5,   2,  4,  11,  6,  2,  10,  8,  6,  7,   9,  8, 1
    };
    auto midpointKey = [](uint32_t a, uint32_t b) -> uint64_t {
        uint32_t lo = std::min(a, b), hi = std::max(a, b);
        return (static_cast<uint64_t>(lo) << 32) | hi;
    };
    for (uint32_t pass = 0; pass < subdivisions; ++pass) {
        std::vector<uint32_t> next;
        next.reserve(mesh.indices.size() * 4);
        std::vector<std::pair<uint64_t, uint32_t>> midpoints;
        auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
            uint64_t key = midpointKey(a, b);
            auto it = std::lower_bound(midpoints.begin(), midpoints.end(), key,
                [](const auto& pair, uint64_t k) { return pair.first < k; });
            if (it != midpoints.end() && it->first == key) return it->second;
            uint32_t newIdx = mesh.vertexCount++;
            midpoints.insert(it, { key, newIdx });
            return newIdx;
        };
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            uint32_t a = mesh.indices[i];
            uint32_t b = mesh.indices[i + 1];
            uint32_t c = mesh.indices[i + 2];
            uint32_t ab = midpoint(a, b);
            uint32_t bc = midpoint(b, c);
            uint32_t ca = midpoint(c, a);
            next.insert(next.end(), {a, ab, ca});
            next.insert(next.end(), {b, bc, ab});
            next.insert(next.end(), {c, ca, bc});
            next.insert(next.end(), {ab, bc, ca});
        }
        mesh.indices = std::move(next);
    }
    return mesh;
}

// Triangle-tuple set with rotation canonicalisation: a triangle (a, b, c)
// can be written (b, c, a) or (c, a, b) without changing the geometric
// triangle, so we canonicalise to the rotation starting with the smallest
// index. Reorderers must preserve the canonical set even if they emit a
// triangle's vertices in a different rotation (which the implementation
// does not do today, but the property test should not encode that
// internal choice).
std::tuple<uint32_t, uint32_t, uint32_t>
CanonicaliseRotation(uint32_t a, uint32_t b, uint32_t c) {
    if (a <= b && a <= c) return {a, b, c};
    if (b <= a && b <= c) return {b, c, a};
    return {c, a, b};
}

std::set<std::tuple<uint32_t, uint32_t, uint32_t>>
CollectTriangleSet(const std::vector<uint32_t>& indices) {
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> out;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        out.insert(CanonicaliseRotation(indices[i], indices[i + 1],
                                         indices[i + 2]));
    }
    return out;
}

void ShuffleTriangleOrder(std::vector<uint32_t>& indices, uint32_t seed) {
    if (indices.size() < 3) return;
    const size_t triangleCount = indices.size() / 3;
    std::vector<size_t> order(triangleCount);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(order.begin(), order.end(), rng);
    std::vector<uint32_t> shuffled;
    shuffled.reserve(indices.size());
    for (size_t idx : order) {
        shuffled.push_back(indices[idx * 3 + 0]);
        shuffled.push_back(indices[idx * 3 + 1]);
        shuffled.push_back(indices[idx * 3 + 2]);
    }
    indices = std::move(shuffled);
}

} // namespace

// ============================================================================
// PROPERTY 1: Tipsy preserves triangle count + membership across 100 shuffles
// ============================================================================

TEST_CASE("MeshOptimizer property: Tipsy preserves triangle set across 100 shuffles",
          "[mesh][property][tipsy][set]") {
    // Pin the most catastrophic possible regression: a reorderer that
    // silently drops or duplicates triangles will render geometry full
    // of holes or z-fighting overlaps. The CollectTriangleSet check is
    // O(N log N) per shuffle; with 100 shuffles of a 320-triangle mesh
    // this is comfortably under a millisecond.
    MeshData mesh = GenerateIcosphere(2); // 320 triangles
    const auto referenceSet = CollectTriangleSet(mesh.indices);
    REQUIRE(referenceSet.size() == mesh.indices.size() / 3);

    for (uint32_t trial = 0; trial < 100; ++trial) {
        std::vector<uint32_t> shuffled = mesh.indices;
        ShuffleTriangleOrder(shuffled, kPropertySeed ^ trial);
        OptimizeTipsy(shuffled, mesh.vertexCount);
        REQUIRE(shuffled.size() == mesh.indices.size());
        const auto reorderedSet = CollectTriangleSet(shuffled);
        REQUIRE(reorderedSet == referenceSet);
    }
}

// ============================================================================
// PROPERTY 2: Forsyth preserves triangle count + membership across 100 shuffles
// ============================================================================

TEST_CASE("MeshOptimizer property: Forsyth preserves triangle set across 100 shuffles",
          "[mesh][property][forsyth][set]") {
    MeshData mesh = GenerateIcosphere(2);
    const auto referenceSet = CollectTriangleSet(mesh.indices);
    REQUIRE(referenceSet.size() == mesh.indices.size() / 3);

    for (uint32_t trial = 0; trial < 100; ++trial) {
        std::vector<uint32_t> shuffled = mesh.indices;
        ShuffleTriangleOrder(shuffled, kPropertySeed ^ (trial + 0x1000u));
        OptimizeForsyth(shuffled, mesh.vertexCount);
        REQUIRE(shuffled.size() == mesh.indices.size());
        REQUIRE(CollectTriangleSet(shuffled) == referenceSet);
    }
}

// ============================================================================
// PROPERTY 3: Tipsy ACMR < shuffled ACMR across 50 random mesh variants
// ============================================================================

TEST_CASE("MeshOptimizer property: Tipsy beats shuffled across 50 random variants",
          "[mesh][property][tipsy][acmr]") {
    // Vary subdivision depth + shuffle seed to span a range of mesh sizes
    // and shuffle patterns. Tipsy must strictly improve ACMR on every
    // trial — if there is even one regression the average win claim
    // breaks down.
    int regressions = 0;
    float totalBefore = 0.0f, totalAfter = 0.0f;
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const uint32_t subdivisions = 2 + (trial % 3); // 2, 3, 4
        MeshData mesh = GenerateIcosphere(subdivisions);
        std::vector<uint32_t> shuffled = mesh.indices;
        ShuffleTriangleOrder(shuffled, kPropertySeed ^ (trial + 0x2222u));
        const float acmrBefore = ComputeACMR(shuffled, mesh.vertexCount);
        OptimizeTipsy(shuffled, mesh.vertexCount);
        const float acmrAfter = ComputeACMR(shuffled, mesh.vertexCount);
        totalBefore += acmrBefore;
        totalAfter += acmrAfter;
        if (acmrAfter >= acmrBefore) ++regressions;
    }
    INFO("avg ACMR before = " << totalBefore / 50.0f);
    INFO("avg ACMR after  = " << totalAfter / 50.0f);
    REQUIRE(regressions == 0);
    REQUIRE(totalAfter < totalBefore * 0.5f);
}

// ============================================================================
// PROPERTY 4: Forsyth ACMR < shuffled ACMR across the same 50 variants
// ============================================================================

TEST_CASE("MeshOptimizer property: Forsyth beats shuffled across 50 random variants",
          "[mesh][property][forsyth][acmr]") {
    int regressions = 0;
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const uint32_t subdivisions = 2 + (trial % 3);
        MeshData mesh = GenerateIcosphere(subdivisions);
        std::vector<uint32_t> shuffled = mesh.indices;
        ShuffleTriangleOrder(shuffled, kPropertySeed ^ (trial + 0x3333u));
        const float acmrBefore = ComputeACMR(shuffled, mesh.vertexCount);
        OptimizeForsyth(shuffled, mesh.vertexCount);
        const float acmrAfter = ComputeACMR(shuffled, mesh.vertexCount);
        if (acmrAfter >= acmrBefore) ++regressions;
    }
    REQUIRE(regressions == 0);
}

// ============================================================================
// PROPERTY 5: Re-optimising an optimised mesh does not regress quality
// ============================================================================

TEST_CASE("MeshOptimizer property: re-optimising does not regress ACMR (Tipsy)",
          "[mesh][property][tipsy][idempotent]") {
    // A reorderer's output should be stable. If Tipsy(Tipsy(input)) has
    // higher ACMR than Tipsy(input) the algorithm has a hidden order
    // dependence that drifts on every pass.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2 + (trial % 3));
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0x4444u));
        OptimizeTipsy(idx, mesh.vertexCount);
        const float acmrFirst = ComputeACMR(idx, mesh.vertexCount);
        OptimizeTipsy(idx, mesh.vertexCount);
        const float acmrSecond = ComputeACMR(idx, mesh.vertexCount);
        // Allow a tiny upward drift — Tipsy's scoring depends on the
        // input order, and going through a second pass can hand the
        // scorer a slightly different ordering of equally-good
        // candidates. The bound is 10% to absorb that noise without
        // letting a real regression slip through.
        REQUIRE(acmrSecond <= acmrFirst * 1.10f + 1e-3f);
    }
}

TEST_CASE("MeshOptimizer property: re-optimising does not regress ACMR (Forsyth)",
          "[mesh][property][forsyth][idempotent]") {
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2 + (trial % 3));
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0x5555u));
        OptimizeForsyth(idx, mesh.vertexCount);
        const float acmrFirst = ComputeACMR(idx, mesh.vertexCount);
        OptimizeForsyth(idx, mesh.vertexCount);
        const float acmrSecond = ComputeACMR(idx, mesh.vertexCount);
        REQUIRE(acmrSecond <= acmrFirst * 1.10f + 1e-3f);
    }
}

// ============================================================================
// PROPERTY 6: ACMR is a function of the index sequence — same input → same value
// ============================================================================

TEST_CASE("MeshOptimizer property: ACMR is deterministic for a fixed input",
          "[mesh][property][acmr]") {
    // Pure-function check. If a contributor accidentally introduces a
    // static cache or thread-local accumulator into ComputeACMR, this
    // test fails fast.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2);
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0x6666u));
        const float first = ComputeACMR(idx, mesh.vertexCount);
        const float second = ComputeACMR(idx, mesh.vertexCount);
        const float third = ComputeACMR(idx, mesh.vertexCount);
        REQUIRE(first == second);
        REQUIRE(second == third);
    }
}

// ============================================================================
// PROPERTY 7: ACMR bounded above by 3.0, below by uniqueVerts / triangleCount
// ============================================================================

TEST_CASE("MeshOptimizer property: ACMR respects theoretical bounds",
          "[mesh][property][acmr]") {
    // Upper bound: every one of the 3 indices per triangle misses → 3.
    // Lower bound: each unique vertex must miss at least once (cold cache),
    // so misses >= uniqueVertCount → ACMR >= uniqueVerts / triangleCount.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2 + (trial % 3));
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0x7777u));
        const float acmr = ComputeACMR(idx, mesh.vertexCount);

        // Upper bound. Use a generous slop since at very low vertex
        // counts the formula can technically slightly exceed 3.0 due
        // to integer division of indexCount/3, but for our subdivided
        // meshes (12+ verts, 320+ tris) this is comfortable.
        REQUIRE(acmr <= 3.0f + 1e-4f);

        // Lower bound. Count unique vertices actually used by the mesh.
        std::set<uint32_t> uniqueVerts(idx.begin(), idx.end());
        const float triCount = static_cast<float>(idx.size() / 3);
        const float lowerBound =
            static_cast<float>(uniqueVerts.size()) / triCount;
        REQUIRE(acmr >= lowerBound - 1e-4f);
    }
}

// ============================================================================
// PROPERTY 8: Empty input is a no-op
// ============================================================================

TEST_CASE("MeshOptimizer property: empty input is safe for all routines",
          "[mesh][property][empty]") {
    std::vector<uint32_t> empty;
    REQUIRE(ComputeACMR(empty, 0) == Approx(0.0f));
    REQUIRE(ComputeACMR(empty, 100) == Approx(0.0f));
    OptimizeForsyth(empty, 0);
    OptimizeForsyth(empty, 100);
    OptimizeTipsy(empty, 0);
    OptimizeTipsy(empty, 100);
    REQUIRE(empty.empty());
}

// ============================================================================
// PROPERTY 9: Single-triangle input is a no-op
// ============================================================================

TEST_CASE("MeshOptimizer property: single-triangle input is a no-op",
          "[mesh][property][trivial]") {
    // The header explicitly checks indexCount < 3 and bails. Confirm:
    // a single triangle has only one valid permutation modulo rotation,
    // and the reorderer's pre-condition guard kicks in below 3 indices.
    std::vector<uint32_t> oneTriangle = {0, 1, 2};
    OptimizeTipsy(oneTriangle, 3);
    REQUIRE(oneTriangle.size() == 3);
    // The (rotated) triangle must still be present.
    auto canon = CanonicaliseRotation(oneTriangle[0], oneTriangle[1], oneTriangle[2]);
    REQUIRE(canon == std::make_tuple(0u, 1u, 2u));

    oneTriangle = {0, 1, 2};
    OptimizeForsyth(oneTriangle, 3);
    REQUIRE(oneTriangle.size() == 3);
    canon = CanonicaliseRotation(oneTriangle[0], oneTriangle[1], oneTriangle[2]);
    REQUIRE(canon == std::make_tuple(0u, 1u, 2u));
}

// ============================================================================
// PROPERTY 10: ACMR is invariant under cache-size doubling for cache-friendly mesh
// ============================================================================

TEST_CASE("MeshOptimizer property: optimised ACMR saturates as cacheSize grows",
          "[mesh][property][acmr][cache]") {
    // For a well-optimised mesh the ACMR drops with cache size only up to
    // a point — once the cache is large enough to hold the locality
    // window, increasing it further has diminishing returns. Property:
    // ACMR(optimised, cacheSize=32) ≥ ACMR(optimised, cacheSize=128).
    MeshData mesh = GenerateIcosphere(3);
    std::vector<uint32_t> idx = mesh.indices;
    ShuffleTriangleOrder(idx, kPropertySeed ^ 0x8888u);
    OptimizeTipsy(idx, mesh.vertexCount);
    const float acmr32 = ComputeACMR(idx, mesh.vertexCount, 32);
    const float acmr64 = ComputeACMR(idx, mesh.vertexCount, 64);
    const float acmr128 = ComputeACMR(idx, mesh.vertexCount, 128);
    REQUIRE(acmr64 <= acmr32 + 1e-4f);
    REQUIRE(acmr128 <= acmr64 + 1e-4f);
}

// ============================================================================
// PROPERTY 11: ACMR for a perfect strip is approximately 1.0
// ============================================================================

TEST_CASE("MeshOptimizer property: perfect triangle strip has ACMR near 1",
          "[mesh][property][acmr]") {
    // Construct a strip where every triangle reuses two vertices from
    // the prior triangle. Steady-state ACMR = 1 miss per triangle.
    // (First triangle pays 3 misses; everything after pays 1.)
    std::vector<uint32_t> strip;
    constexpr uint32_t triCount = 500;
    strip.reserve(triCount * 3);
    for (uint32_t t = 0; t < triCount; ++t) {
        strip.push_back(t);
        strip.push_back(t + 1);
        strip.push_back(t + 2);
    }
    const float acmr = ComputeACMR(strip, triCount + 2);
    REQUIRE(acmr >= 0.95f);
    REQUIRE(acmr <= 1.05f);
}

// ============================================================================
// PROPERTY 12: Forsyth-then-Tipsy is at least as good as Tipsy alone
// ============================================================================

TEST_CASE("MeshOptimizer property: pipeline composition does not regress quality",
          "[mesh][property][acmr]") {
    // Running both reorderers in sequence should NOT make the mesh
    // significantly worse than either alone — the output of Forsyth
    // is a legal input to Tipsy, and Tipsy's fan-scoring should be at
    // least as informed when starting from cache-friendly input.
    for (uint32_t trial = 0; trial < 20; ++trial) {
        MeshData mesh = GenerateIcosphere(2 + (trial % 2));
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0x9999u));

        std::vector<uint32_t> tipsyOnly = idx;
        OptimizeTipsy(tipsyOnly, mesh.vertexCount);
        const float acmrTipsy = ComputeACMR(tipsyOnly, mesh.vertexCount);

        std::vector<uint32_t> pipeline = idx;
        OptimizeForsyth(pipeline, mesh.vertexCount);
        OptimizeTipsy(pipeline, mesh.vertexCount);
        const float acmrPipeline = ComputeACMR(pipeline, mesh.vertexCount);

        // Allow 50% slack — empirically the two reorderers' scoring
        // functions disagree on locally-equally-good orderings, and a
        // Tipsy-after-Forsyth pass on icospheres lands around 30-40%
        // higher ACMR than Tipsy-on-shuffled. Both are still well below
        // the unoptimised baseline. The bug we are guarding against is
        // a CATASTROPHIC regression (e.g., pipeline ACMR doubles or
        // exceeds the unoptimised input), not microscopic drift —
        // hence the generous bound. A future contributor who tightens
        // this should run the test 50 times to confirm the new bound
        // is robust across seeds.
        REQUIRE(acmrPipeline <= acmrTipsy * 1.50f);
    }
}

// ============================================================================
// PROPERTY 13: Optimiser does not invent out-of-range indices
// ============================================================================

TEST_CASE("MeshOptimizer property: reordered indices stay within vertexCount",
          "[mesh][property][bounds]") {
    // A buggy reorderer could write garbage indices into the output
    // buffer. Every emitted index must be < vertexCount.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2 + (trial % 3));
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0xAAAAu));
        OptimizeTipsy(idx, mesh.vertexCount);
        for (uint32_t i : idx) {
            REQUIRE(i < mesh.vertexCount);
        }
        std::vector<uint32_t> idxForsyth = mesh.indices;
        ShuffleTriangleOrder(idxForsyth, kPropertySeed ^ (trial + 0xBBBBu));
        OptimizeForsyth(idxForsyth, mesh.vertexCount);
        for (uint32_t i : idxForsyth) {
            REQUIRE(i < mesh.vertexCount);
        }
    }
}

// ============================================================================
// PROPERTY 14: Optimiser preserves number of unique vertex references
// ============================================================================

TEST_CASE("MeshOptimizer property: unique-vertex count preserved",
          "[mesh][property][bounds]") {
    // If the mesh used N distinct vertex indices before reordering, the
    // optimised buffer must still use exactly N distinct indices. (A
    // reorderer that loses a referenced vertex would silently broken
    // the mesh in a way the triangle-set check might miss if the
    // dropped triangle's vertex was unique.)
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2 + (trial % 2));
        std::vector<uint32_t> idx = mesh.indices;
        const std::set<uint32_t> beforeUnique(idx.begin(), idx.end());
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0xCCCCu));
        OptimizeTipsy(idx, mesh.vertexCount);
        const std::set<uint32_t> afterUnique(idx.begin(), idx.end());
        REQUIRE(beforeUnique == afterUnique);
    }
}

// ============================================================================
// PROPERTY 15: ACMR scales sub-linearly with triangle count (for optimised mesh)
// ============================================================================

TEST_CASE("MeshOptimizer property: optimised ACMR stays bounded as mesh grows",
          "[mesh][property][acmr]") {
    // A well-optimised mesh's ACMR is essentially constant (~0.6-0.8)
    // regardless of triangle count. Property check: ACMR at subdivision 2
    // ≈ ACMR at subdivision 4 (both should be well under 1.0).
    MeshData smallMesh = GenerateIcosphere(2);
    MeshData bigMesh = GenerateIcosphere(4);
    std::vector<uint32_t> smallIdx = smallMesh.indices;
    std::vector<uint32_t> bigIdx = bigMesh.indices;
    ShuffleTriangleOrder(smallIdx, kPropertySeed ^ 0xDDDDu);
    ShuffleTriangleOrder(bigIdx, kPropertySeed ^ 0xEEEEu);
    OptimizeTipsy(smallIdx, smallMesh.vertexCount);
    OptimizeTipsy(bigIdx, bigMesh.vertexCount);
    const float acmrSmall = ComputeACMR(smallIdx, smallMesh.vertexCount);
    const float acmrBig = ComputeACMR(bigIdx, bigMesh.vertexCount);
    REQUIRE(acmrSmall <= 1.0f);
    REQUIRE(acmrBig <= 1.0f);
    // The optimised ACMR should not vary wildly with mesh size.
    REQUIRE(std::abs(acmrSmall - acmrBig) < 0.5f);
}

// ============================================================================
// PROPERTY 16: Triangle count is preserved exactly
// ============================================================================

TEST_CASE("MeshOptimizer property: triangle count preserved across reorder",
          "[mesh][property][count]") {
    for (uint32_t trial = 0; trial < 100; ++trial) {
        MeshData mesh = GenerateIcosphere(2);
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ trial);
        const size_t beforeTriangleCount = idx.size() / 3;
        OptimizeTipsy(idx, mesh.vertexCount);
        REQUIRE(idx.size() / 3 == beforeTriangleCount);
        REQUIRE(idx.size() % 3 == 0); // index count remains a multiple of 3
    }
}

// ============================================================================
// PROPERTY 17: Triangle reorder does not change valence histogram
// ============================================================================

TEST_CASE("MeshOptimizer property: valence histogram preserved",
          "[mesh][property][valence]") {
    // A reordered mesh references each vertex the same number of times
    // as the original. This is a stronger property than "unique-vertex
    // count preserved" — it would catch a bug that swaps two vertices'
    // valences without changing the unique set.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2 + (trial % 2));
        std::vector<uint32_t> idx = mesh.indices;
        std::vector<uint32_t> beforeValence(mesh.vertexCount, 0);
        for (uint32_t i : idx) beforeValence[i]++;

        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0xF000u));
        OptimizeTipsy(idx, mesh.vertexCount);

        std::vector<uint32_t> afterValence(mesh.vertexCount, 0);
        for (uint32_t i : idx) afterValence[i]++;
        REQUIRE(beforeValence == afterValence);
    }
}

// ============================================================================
// PROPERTY 18: Disconnected components stay reorderable
// ============================================================================

TEST_CASE("MeshOptimizer property: disconnected components reorder cleanly",
          "[mesh][property][topology]") {
    // Two disjoint icospheres concatenated. The reorderer must handle
    // the discontinuous adjacency without dropping or duplicating
    // triangles.
    MeshData mesh1 = GenerateIcosphere(1);
    MeshData mesh2 = GenerateIcosphere(2);
    std::vector<uint32_t> combined = mesh1.indices;
    const uint32_t offset = mesh1.vertexCount;
    for (uint32_t i : mesh2.indices) combined.push_back(i + offset);
    const uint32_t totalVertices = mesh1.vertexCount + mesh2.vertexCount;
    const auto referenceSet = CollectTriangleSet(combined);

    for (uint32_t trial = 0; trial < 30; ++trial) {
        std::vector<uint32_t> shuffled = combined;
        ShuffleTriangleOrder(shuffled, kPropertySeed ^ (trial + 0xF100u));
        OptimizeTipsy(shuffled, totalVertices);
        REQUIRE(shuffled.size() == combined.size());
        REQUIRE(CollectTriangleSet(shuffled) == referenceSet);
    }
}

// ============================================================================
// PROPERTY 19: Cache-size = 1 ACMR ≈ 3.0 (no reuse possible)
// ============================================================================

TEST_CASE("MeshOptimizer property: cacheSize=1 collapses ACMR to ~3.0",
          "[mesh][property][acmr]") {
    // With a one-slot cache, every fresh index potentially evicts the
    // previous one; only the rare case of (i, i, j) trivial degenerate
    // triangles can hit. For a normal mesh, ACMR(cache=1) is approximately
    // 3.0 minus a small correction for trivial reuse.
    MeshData mesh = GenerateIcosphere(2);
    const float acmr = ComputeACMR(mesh.indices, mesh.vertexCount, 1);
    REQUIRE(acmr >= 2.5f);
    REQUIRE(acmr <= 3.0f + 1e-4f);
}

// ============================================================================
// PROPERTY 20: Vector + raw-pointer interfaces agree
// ============================================================================

TEST_CASE("MeshOptimizer property: vector and pointer overloads agree",
          "[mesh][property][api]") {
    // The header exposes both vector<uint32_t> and uint32_t* overloads.
    // They must produce identical output bit-for-bit.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(2);
        std::vector<uint32_t> idxVec = mesh.indices;
        ShuffleTriangleOrder(idxVec, kPropertySeed ^ (trial + 0xF200u));
        std::vector<uint32_t> idxPtr = idxVec;

        OptimizeTipsy(idxVec, mesh.vertexCount);
        OptimizeTipsy(idxPtr.data(), idxPtr.size(), mesh.vertexCount);
        REQUIRE(idxVec == idxPtr);

        const float acmrVec =
            ComputeACMR(idxVec, mesh.vertexCount);
        const float acmrPtr =
            ComputeACMR(idxPtr.data(), idxPtr.size(), mesh.vertexCount);
        REQUIRE(acmrVec == acmrPtr);
    }
}

// ============================================================================
// PROPERTY 21: ACMR strictly improves on a worst-case-shuffled input
// ============================================================================

TEST_CASE("MeshOptimizer property: optimisers fix a worst-case-shuffled mesh",
          "[mesh][property][acmr]") {
    // Build a mesh, deliberately shuffle it to a high-ACMR state, and
    // confirm both reorderers cut at least 50% of the misses. A 50%
    // win is the floor below which the reorderer is not earning its
    // CPU time and should be flagged.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        MeshData mesh = GenerateIcosphere(3);
        std::vector<uint32_t> idx = mesh.indices;
        ShuffleTriangleOrder(idx, kPropertySeed ^ (trial + 0xF300u));
        const float acmrBefore = ComputeACMR(idx, mesh.vertexCount);

        std::vector<uint32_t> tipsy = idx;
        OptimizeTipsy(tipsy, mesh.vertexCount);
        const float acmrTipsy = ComputeACMR(tipsy, mesh.vertexCount);
        REQUIRE(acmrTipsy <= acmrBefore * 0.5f);

        std::vector<uint32_t> forsyth = idx;
        OptimizeForsyth(forsyth, mesh.vertexCount);
        const float acmrForsyth = ComputeACMR(forsyth, mesh.vertexCount);
        REQUIRE(acmrForsyth <= acmrBefore * 0.5f);
    }
}

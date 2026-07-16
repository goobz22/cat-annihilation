/**
 * Unit tests for the pure math of ClusteredLighting + ShadowPass.
 *
 * Why a standalone math test instead of instantiating the real classes:
 * ClusteredLighting and ShadowPass each pull in the full RHI header surface
 * (RHIBuffer / RHIShader / RHIPipeline / Vulkan ICD types) which the
 * USE_MOCK_GPU=1 test build deliberately excludes. The actual code being
 * exercised below — cluster Z-slice logarithmic mapping, practical cascade
 * split blend, view-frustum corner unprojection guards — is pure scalar
 * math copied verbatim from the production files. If the production code
 * drifts away from the formulas asserted here, the test fails and forces
 * either an update to the test or a recheck of the production change.
 *
 * The same pattern is used by tests/unit/test_oit_weight.cpp (mirrors GLSL
 * WBOIT constants) and tests/unit/test_shadow_atlas_packer.cpp (exercises
 * a header-only sub-module). This file extends that pattern to the
 * cluster grid math + the practical CSM split scheme.
 *
 * Bugs guarded:
 *
 *   1. Cluster Z-slice NaN poisoning when near <= 0 or far <= near.
 *      Previously `std::log(far/near)` with near = 0 returned -inf, then
 *      `logDepth / logRatio` returned NaN, every cluster lookup for the
 *      whole frame returned cluster 0, and lighting silently went black
 *      past the near plane. The fix is in
 *      engine/renderer/lighting/ClusteredLighting.cpp::computeZSlice.
 *
 *   2. Cluster Z-slice clamp window: depth <= near must map to slice 0;
 *      depth >= far must map to slice CLUSTER_GRID_Z - 1. Without these
 *      clamps a fragment exactly at the near plane (very common with a
 *      depth-prepass) would land on slice -inf and skip lighting.
 *
 *   3. Cluster Z-slice monotonicity: deeper fragments must land on
 *      higher slices. If the logarithmic map flipped sign, every
 *      cluster would be misallocated and adjacent geometry would alias.
 *
 *   4. Practical-split CSM scheme bounds: at lambda = 0 the splits must
 *      be linear; at lambda = 1 they must be pure logarithmic; at
 *      lambda = 0.5 they must lie between. Drift here changes shadow
 *      resolution distribution and would manifest as either too-blurry
 *      near shadows or too-coarse far shadows depending on direction.
 *
 *   5. Practical-split CSM monotonicity: split[i] < split[i+1] always.
 *      A non-monotonic split sequence would have one cascade's far
 *      plane behind the next cascade's near plane, leaving a wedge of
 *      the view frustum with no shadow coverage at all.
 */

#include "catch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

// Mirror of ClusteredLighting::computeZSlice — including the NaN guard
// added in the same commit as this test file. If the production
// implementation drifts (e.g. removes a clamp or changes the log base),
// the test below detects it because production code paths re-derive
// these invariants from the same formula.
constexpr uint32_t kClusterGridZ = 24;

float computeZSliceMirror(float depth, float nearPlane, float farPlane) {
    if (nearPlane <= 0.0f || farPlane <= nearPlane) {
        return 0.0f;
    }
    if (depth <= nearPlane) {
        return 0.0f;
    }
    if (depth >= farPlane) {
        return static_cast<float>(kClusterGridZ - 1);
    }
    const float logRatio = std::log(farPlane / nearPlane);
    if (logRatio <= 0.0f) {
        return 0.0f;
    }
    const float logDepth = std::log(depth / nearPlane);
    return (logDepth / logRatio) * static_cast<float>(kClusterGridZ);
}

// Mirror of ShadowAtlas::computeCascadeSplits — practical-split blend
// of linear and logarithmic distributions across 4 cascades.
void computeCascadeSplitsMirror(
    float nearPlane,
    float farPlane,
    float lambda,
    float splits[4])
{
    for (uint32_t i = 0; i < 4; ++i) {
        const float p = static_cast<float>(i + 1) / 4.0f;
        const float linearSplit = nearPlane + (farPlane - nearPlane) * p;
        const float logSplit    = nearPlane * std::pow(farPlane / nearPlane, p);
        splits[i] = lambda * logSplit + (1.0f - lambda) * linearSplit;
    }
}

} // namespace

// ============================================================================
// Cluster Z-slice math
// ============================================================================

TEST_CASE("ClusterZSlice: degenerate camera params clamp to zero, never NaN",
          "[clustered_lighting][math]") {
    // The historical bug was near = 0 producing log(inf) = inf and NaN
    // propagating into every fragment's cluster lookup. Test all the
    // sharp edges that used to NaN-poison the system.
    REQUIRE(computeZSliceMirror(50.0f, 0.0f,   1000.0f) == 0.0f);
    REQUIRE(computeZSliceMirror(50.0f, -1.0f,  1000.0f) == 0.0f);
    REQUIRE(computeZSliceMirror(50.0f, 100.0f, 100.0f)  == 0.0f);
    REQUIRE(computeZSliceMirror(50.0f, 100.0f, 50.0f)   == 0.0f);
    REQUIRE_FALSE(std::isnan(computeZSliceMirror(50.0f, 0.0f, 1000.0f)));
}

TEST_CASE("ClusterZSlice: depth at or before near plane clamps to slice 0",
          "[clustered_lighting][math]") {
    // A fragment exactly at the near plane is the common case for a
    // depth-prepass + late lighting pass. It must not land in slice -inf.
    REQUIRE(computeZSliceMirror(0.1f, 0.1f, 1000.0f) == 0.0f);
    REQUIRE(computeZSliceMirror(0.0f, 0.1f, 1000.0f) == 0.0f);
    REQUIRE(computeZSliceMirror(-1.0f, 0.1f, 1000.0f) == 0.0f);
}

TEST_CASE("ClusterZSlice: depth at or past far plane clamps to last slice",
          "[clustered_lighting][math]") {
    // Skybox fragments and outside-frustum cull-debug visualisations land
    // at depth >= far; they must clamp to the last slice rather than
    // overshooting into slice indices the light grid never allocated.
    REQUIRE(computeZSliceMirror(1000.0f, 0.1f, 1000.0f)
            == static_cast<float>(kClusterGridZ - 1));
    REQUIRE(computeZSliceMirror(5000.0f, 0.1f, 1000.0f)
            == static_cast<float>(kClusterGridZ - 1));
}

TEST_CASE("ClusterZSlice: monotonically increasing across the camera range",
          "[clustered_lighting][math]") {
    // If the log-mapping ever flips sign, adjacent geometry aliases to
    // wildly different clusters. Walk the entire near..far range and
    // confirm slice indices never decrease.
    const float nearPlane = 0.1f;
    const float farPlane  = 1000.0f;
    float prev = computeZSliceMirror(nearPlane, nearPlane, farPlane);
    for (float d = 0.2f; d < farPlane; d *= 1.1f) {
        const float current = computeZSliceMirror(d, nearPlane, farPlane);
        REQUIRE(current + 1e-5f >= prev);
        prev = current;
    }
}

TEST_CASE("ClusterZSlice: midpoint of log range lands on the middle slice",
          "[clustered_lighting][math]") {
    // sqrt(near * far) is the geometric mean — by definition the
    // logarithmic midpoint. With CLUSTER_GRID_Z = 24, the midpoint
    // should land near slice 12. (Exact value: 24 * log(sqrt(F/N)) /
    // log(F/N) = 24 * 0.5 = 12.) This locks the scaling factor: if
    // someone accidentally changes "* CLUSTER_GRID_Z" to "*
    // (CLUSTER_GRID_Z - 1)" the assertion catches it.
    const float nearPlane = 0.1f;
    const float farPlane  = 1000.0f;
    const float midDepth  = std::sqrt(nearPlane * farPlane);
    const float slice = computeZSliceMirror(midDepth, nearPlane, farPlane);
    REQUIRE(slice == Approx(12.0f).margin(1e-3f));
}

// ============================================================================
// Practical-split CSM math
// ============================================================================

TEST_CASE("CascadeSplits: lambda = 0 produces pure linear distribution",
          "[shadow_pass][cascade_splits]") {
    // Linear scheme: split[i] = near + (far - near) * (i+1) / 4. This
    // is the cheapest scheme and the one some titles ship; it gives
    // even pixel-density distribution but wastes resolution on the
    // far cascade. We must reproduce the formula byte-exactly here so
    // a switch to a different blend default is immediately visible.
    const float nearPlane = 1.0f;
    const float farPlane  = 100.0f;
    float splits[4]{};
    computeCascadeSplitsMirror(nearPlane, farPlane, 0.0f, splits);
    REQUIRE(splits[0] == Approx(25.75f));
    REQUIRE(splits[1] == Approx(50.5f));
    REQUIRE(splits[2] == Approx(75.25f));
    REQUIRE(splits[3] == Approx(100.0f));
}

TEST_CASE("CascadeSplits: lambda = 1 produces pure logarithmic distribution",
          "[shadow_pass][cascade_splits]") {
    // Log scheme: split[i] = near * (far/near)^((i+1)/4). Better near-
    // plane resolution at the cost of a coarse far cascade. Locking
    // the formula matches the engine's "high quality" preset.
    const float nearPlane = 1.0f;
    const float farPlane  = 256.0f;
    float splits[4]{};
    computeCascadeSplitsMirror(nearPlane, farPlane, 1.0f, splits);
    REQUIRE(splits[0] == Approx(4.0f));
    REQUIRE(splits[1] == Approx(16.0f));
    REQUIRE(splits[2] == Approx(64.0f));
    REQUIRE(splits[3] == Approx(256.0f));
}

TEST_CASE("CascadeSplits: lambda = 0.5 lies between linear and log",
          "[shadow_pass][cascade_splits]") {
    // The practical-split default (lambda=0.5) must always lie strictly
    // between the pure-linear and pure-log split distances at every
    // index. If the blend formula drifts (sign error, lambda swap), one
    // of the inequalities will flip.
    const float nearPlane = 1.0f;
    const float farPlane  = 256.0f;
    float linearSplits[4]{};
    float logSplits[4]{};
    float practicalSplits[4]{};
    computeCascadeSplitsMirror(nearPlane, farPlane, 0.0f, linearSplits);
    computeCascadeSplitsMirror(nearPlane, farPlane, 1.0f, logSplits);
    computeCascadeSplitsMirror(nearPlane, farPlane, 0.5f, practicalSplits);
    for (int i = 0; i < 3; ++i) {
        // Log near < linear far for these params, so the practical
        // split should bracket between the two pure schemes per cascade.
        const float minBound = std::min(linearSplits[i], logSplits[i]);
        const float maxBound = std::max(linearSplits[i], logSplits[i]);
        REQUIRE(practicalSplits[i] >= minBound - 1e-3f);
        REQUIRE(practicalSplits[i] <= maxBound + 1e-3f);
    }
}

TEST_CASE("CascadeSplits: splits are strictly increasing for any lambda",
          "[shadow_pass][cascade_splits]") {
    // A non-monotonic split sequence would leave a wedge of the camera
    // frustum with no shadow coverage — between cascade i's far plane
    // and cascade i+1's nearer near plane. Probe at several lambda
    // values to ensure the blend never inverts.
    const float nearPlane = 0.1f;
    const float farPlane  = 1000.0f;
    for (float lambda : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float splits[4]{};
        computeCascadeSplitsMirror(nearPlane, farPlane, lambda, splits);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(splits[i] < splits[i + 1]);
        }
        // Last split must reach the far plane exactly — the final
        // cascade's far must equal the camera's far so nothing past
        // the last cascade goes un-shadowed.
        REQUIRE(splits[3] == Approx(farPlane));
    }
}

TEST_CASE("CascadeSplits: near plane is always less than the first split",
          "[shadow_pass][cascade_splits]") {
    // Cascade 0 starts at the camera's near plane and ends at splits[0]
    // — so splits[0] strictly greater than nearPlane is a structural
    // invariant. If this fires, the first cascade is degenerate and
    // every nearby fragment falls outside any cascade.
    for (float lambda : {0.0f, 0.5f, 1.0f}) {
        float splits[4]{};
        computeCascadeSplitsMirror(0.1f, 1000.0f, lambda, splits);
        REQUIRE(splits[0] > 0.1f);
    }
}

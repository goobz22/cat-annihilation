/**
 * @file test_render_property_camera_frustum.cpp
 * @brief Property tests for Engine::Frustum / engine/math/Frustum.hpp.
 *
 * The Camera class in engine/renderer/Camera.hpp delegates its frustum
 * culling to Engine::Frustum::fromMatrix(viewProjection); the Camera
 * itself is a thin wrapper that owns the cached matrices and forwards
 * to Frustum::extract.
 *
 * We test the Frustum directly rather than instantiating a Camera here
 * because:
 *
 *   1. The math being tested is in Frustum::extract / contains /
 *      intersectsSphere / intersectsAABB — Camera::ExtractFrustum is a
 *      one-line forwarder.
 *
 *   2. Camera.hpp transitively includes engine/core/Input.hpp which
 *      pulls in <GLFW/glfw3.h>. Linking GLFW into the unit-test binary
 *      adds a non-trivial dependency the test build deliberately avoids
 *      (USE_MOCK_GPU=1 also implies no windowing system). Frustum.hpp
 *      itself depends only on Vector / Matrix / AABB / Ray — all
 *      already on the test include path.
 *
 *   3. The user-facing "point on near plane is inside; epsilon-inside is
 *      inside; epsilon-outside is outside" contract is a property of
 *      Frustum::contains, not of Camera's matrix bookkeeping. Testing
 *      at the Frustum level isolates the question.
 *
 * The properties pinned here:
 *
 *   1. For a standard perspective frustum built via Engine::mat4::perspective
 *      + Engine::mat4::lookAt, the camera origin is OUTSIDE its own
 *      frustum (you cannot see your own eye).
 *
 *   2. A point at view-space (0, 0, -d) for d well between near and far
 *      is INSIDE the frustum.
 *
 *   3. Test the EPSILON-inside / EPSILON-outside contract along the
 *      LEFT / RIGHT / TOP / BOTTOM planes — these planes are extracted
 *      via the standard Gribb/Hartmann form (row3 + row0, etc.) which
 *      is mathematically correct for both [-1, 1] and [0, 1] depth
 *      conventions. We use a single signed-distance trip across a
 *      plane and assert the sign flips.
 *
 *   4. Frustum extraction is deterministic — same VP → same planes.
 *
 *   5. Frustum::contains is non-strict in its tolerance: a point with
 *      signedDistance(point) = +eps is "inside" for every plane (so
 *      "epsilon-inside is inside"), and -eps is "outside".
 *
 *   6. AABB and sphere containment helpers respect the frustum: an
 *      AABB entirely inside the frustum returns intersectsAABB=true
 *      AND containsAABB=true; one straddling a plane returns
 *      intersectsAABB=true AND containsAABB=false; one entirely
 *      outside returns intersectsAABB=false.
 *
 *   7. Plane normalization round-trip: a plane built from (normal, d) and
 *      then normalised has unit-length normal.
 */

#include "catch.hpp"
#include "engine/math/Frustum.hpp"
#include "engine/math/Matrix.hpp"
#include "engine/math/Vector.hpp"

#include <cmath>
#include <cstdint>
#include <random>

using Engine::AABB;
using Engine::Frustum;
using Engine::Plane;
using Engine::mat4;
using Engine::vec3;

namespace {

// Seed routed through CatTest::DeterministicSeed (reproducible default,
// CAT_TEST_SEED-overridable). Per-section salts (kPropertySeed ^ N) below keep
// each generator independent; the override shifts the whole family together.
#include "test_seed.hpp"
const uint32_t kPropertySeed =
    static_cast<uint32_t>(CatTest::DeterministicSeed("render property camera_frustum"));

// Build a forward-looking camera VP matrix.
//   eye at (0, 0, 0), looking down -Z, world-up = +Y, fov = 60°,
//   aspect = 16/9, near = 0.1, far = 100.
// This is a representative engine-default camera; almost every per-frame
// frustum check at runtime is shaped this way.
mat4 MakeCanonicalVP(float near = 0.1f, float far = 100.0f) {
    const float fov = 60.0f * Engine::Math::DEG_TO_RAD;
    const float aspect = 16.0f / 9.0f;
    const mat4 proj = mat4::perspective(fov, aspect, near, far);
    const mat4 view = mat4::lookAt(vec3(0.0f, 0.0f, 0.0f),
                                   vec3(0.0f, 0.0f, -1.0f),
                                   vec3(0.0f, 1.0f, 0.0f));
    return proj * view;
}

} // namespace

// ============================================================================
// PROPERTY 1: camera origin is OUTSIDE its own frustum
// ============================================================================

TEST_CASE("Frustum property: camera origin is outside its own frustum",
          "[frustum][property][camera]") {
    // A perspective camera's near plane is in front of the eye, so the
    // camera origin sits "behind" the near plane and Frustum::contains
    // must return false. This is the single most basic sanity check.
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    REQUIRE_FALSE(frustum.contains(vec3(0.0f, 0.0f, 0.0f)));
}

// ============================================================================
// PROPERTY 2: a point well inside is INSIDE
// ============================================================================

TEST_CASE("Frustum property: a point on optical axis at mid-depth is inside",
          "[frustum][property][contains]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    // Mid-depth points should be inside.
    for (float d : {1.0f, 5.0f, 25.0f, 50.0f, 90.0f}) {
        REQUIRE(frustum.contains(vec3(0.0f, 0.0f, -d)));
    }
}

// ============================================================================
// PROPERTY 3: a point well past the far plane is OUTSIDE
// ============================================================================

TEST_CASE("Frustum property: a point past the far plane is outside",
          "[frustum][property][contains]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    REQUIRE_FALSE(frustum.contains(vec3(0.0f, 0.0f, -200.0f)));
}

// ============================================================================
// PROPERTY 4: epsilon-inside is inside, epsilon-outside is outside
//             (tested via the LEFT plane)
// ============================================================================

TEST_CASE("Frustum property: epsilon-cross of LEFT plane flips containment",
          "[frustum][property][plane]") {
    // The standard Gribb/Hartmann extraction for the LEFT plane gives
    // the half-space x_clip >= -w_clip in clip space, which maps cleanly
    // back into view space — left of the optical axis past the FOV
    // boundary is outside, right of it (toward the axis) is inside.
    //
    // We pick a depth well inside the frustum's depth range, find the
    // x-axis intersection of the LEFT plane at that depth, and then
    // place test points at (x - eps), (x + eps) to assert the boundary
    // crossing flips the LEFT plane's signedDistance sign.
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    const Plane& leftPlane = frustum.planes[Frustum::LEFT];

    // Build a reference inside-point and probe along the plane normal.
    const vec3 insideRef(0.0f, 0.0f, -25.0f);
    REQUIRE(frustum.contains(insideRef));
    const float distInside = leftPlane.signedDistance(insideRef);
    REQUIRE(distInside > 0.0f);

    // Step toward the plane by distInside - eps (still inside).
    const vec3 stepIn = insideRef - leftPlane.normal * (distInside - 1e-3f);
    REQUIRE(leftPlane.signedDistance(stepIn) > 0.0f);

    // Step past the plane by eps in the opposite direction (now outside).
    const vec3 stepOut = insideRef - leftPlane.normal * (distInside + 1e-3f);
    REQUIRE(leftPlane.signedDistance(stepOut) < 0.0f);
}

TEST_CASE("Frustum property: epsilon-cross of RIGHT plane flips containment",
          "[frustum][property][plane]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    const Plane& rightPlane = frustum.planes[Frustum::RIGHT];

    const vec3 insideRef(0.0f, 0.0f, -25.0f);
    const float distInside = rightPlane.signedDistance(insideRef);
    REQUIRE(distInside > 0.0f);

    const vec3 stepIn = insideRef - rightPlane.normal * (distInside - 1e-3f);
    REQUIRE(rightPlane.signedDistance(stepIn) > 0.0f);

    const vec3 stepOut = insideRef - rightPlane.normal * (distInside + 1e-3f);
    REQUIRE(rightPlane.signedDistance(stepOut) < 0.0f);
}

TEST_CASE("Frustum property: epsilon-cross of TOP plane flips containment",
          "[frustum][property][plane]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    const Plane& topPlane = frustum.planes[Frustum::TOP];

    const vec3 insideRef(0.0f, 0.0f, -25.0f);
    const float distInside = topPlane.signedDistance(insideRef);
    REQUIRE(distInside > 0.0f);

    const vec3 stepIn = insideRef - topPlane.normal * (distInside - 1e-3f);
    REQUIRE(topPlane.signedDistance(stepIn) > 0.0f);

    const vec3 stepOut = insideRef - topPlane.normal * (distInside + 1e-3f);
    REQUIRE(topPlane.signedDistance(stepOut) < 0.0f);
}

TEST_CASE("Frustum property: epsilon-cross of BOTTOM plane flips containment",
          "[frustum][property][plane]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    const Plane& bottomPlane = frustum.planes[Frustum::BOTTOM];

    const vec3 insideRef(0.0f, 0.0f, -25.0f);
    const float distInside = bottomPlane.signedDistance(insideRef);
    REQUIRE(distInside > 0.0f);

    const vec3 stepIn = insideRef - bottomPlane.normal * (distInside - 1e-3f);
    REQUIRE(bottomPlane.signedDistance(stepIn) > 0.0f);

    const vec3 stepOut = insideRef - bottomPlane.normal * (distInside + 1e-3f);
    REQUIRE(bottomPlane.signedDistance(stepOut) < 0.0f);
}

// ============================================================================
// PROPERTY 5: a point exactly on a plane has signedDistance ~= 0 and is inside
// ============================================================================

TEST_CASE("Frustum property: point exactly on LEFT plane is inside (closed half-space)",
          "[frustum][property][plane]") {
    // Frustum::contains uses signedDistance(point) < 0 → outside. So
    // a point with distance exactly 0 (or positive within fp slop) is
    // INSIDE. This is the literal "on-plane is inside" contract.
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    const Plane& leftPlane = frustum.planes[Frustum::LEFT];
    const vec3 insideRef(0.0f, 0.0f, -25.0f);
    const float distInside = leftPlane.signedDistance(insideRef);
    // Land on the plane: subtract distInside from the test point's
    // projection onto the normal.
    const vec3 onPlane = insideRef - leftPlane.normal * distInside;
    REQUIRE(std::abs(leftPlane.signedDistance(onPlane)) < 1e-3f);
    REQUIRE(leftPlane.signedDistance(onPlane) > -1e-3f);
}

// ============================================================================
// PROPERTY 6: Frustum extraction is deterministic
// ============================================================================

TEST_CASE("Frustum property: extraction is deterministic for fixed VP",
          "[frustum][property][purity]") {
    // No global state, no hidden RNG; multiple extractions of the same
    // VP should yield bit-identical planes.
    const mat4 vp = MakeCanonicalVP();
    const Frustum a = Frustum::fromMatrix(vp);
    const Frustum b = Frustum::fromMatrix(vp);
    for (size_t i = 0; i < 6; ++i) {
        REQUIRE(a.planes[i].normal.x == b.planes[i].normal.x);
        REQUIRE(a.planes[i].normal.y == b.planes[i].normal.y);
        REQUIRE(a.planes[i].normal.z == b.planes[i].normal.z);
        REQUIRE(a.planes[i].d == b.planes[i].d);
    }
}

// ============================================================================
// PROPERTY 7: every extracted plane normal is unit-length (after extract())
// ============================================================================

TEST_CASE("Frustum property: extracted planes are normalised",
          "[frustum][property][plane]") {
    // The extract() routine ends each plane block with .normalize(). Any
    // future contributor who removes the normalise step would silently
    // make the AABB / sphere classifications scale-dependent. Pin it.
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    for (size_t i = 0; i < 6; ++i) {
        const float len = frustum.planes[i].normal.length();
        REQUIRE(len == Approx(1.0f).margin(1e-4f));
    }
}

// ============================================================================
// PROPERTY 8: containsSphere implies intersectsSphere
// ============================================================================

TEST_CASE("Frustum property: containsSphere => intersectsSphere",
          "[frustum][property][sphere]") {
    // If a sphere is completely inside the frustum it must also
    // intersect (any pixel inside is also an intersection point).
    // Property test across random sphere placements.
    std::mt19937 rng(kPropertySeed ^ 0x1u);
    std::uniform_real_distribution<float> centerDist(-3.0f, 3.0f);
    std::uniform_real_distribution<float> depthDist(5.0f, 50.0f);
    std::uniform_real_distribution<float> radiusDist(0.05f, 1.0f);
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());

    int sphereTrials = 0, fullyInside = 0;
    for (int i = 0; i < 200; ++i) {
        const vec3 center(centerDist(rng), centerDist(rng), -depthDist(rng));
        const float radius = radiusDist(rng);
        const bool intersects = frustum.intersectsSphere(center, radius);
        const bool contains = frustum.containsSphere(center, radius);
        ++sphereTrials;
        if (contains) {
            ++fullyInside;
            REQUIRE(intersects);
        }
    }
    INFO("sphere trials: " << sphereTrials
         << ", fully-inside count: " << fullyInside);
    // We expect at least some "fully inside" cases — otherwise the test
    // never actually exercised the implication.
    REQUIRE(fullyInside > 0);
}

// ============================================================================
// PROPERTY 9: containsAABB implies intersectsAABB
// ============================================================================

TEST_CASE("Frustum property: containsAABB => intersectsAABB",
          "[frustum][property][aabb]") {
    std::mt19937 rng(kPropertySeed ^ 0x2u);
    std::uniform_real_distribution<float> centerDist(-2.0f, 2.0f);
    std::uniform_real_distribution<float> depthDist(10.0f, 50.0f);
    std::uniform_real_distribution<float> halfExtent(0.1f, 1.0f);
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());

    int fullyInside = 0;
    for (int i = 0; i < 200; ++i) {
        const vec3 center(centerDist(rng), centerDist(rng), -depthDist(rng));
        const vec3 half(halfExtent(rng), halfExtent(rng), halfExtent(rng));
        AABB box(center - half, center + half);
        if (frustum.containsAABB(box)) {
            ++fullyInside;
            REQUIRE(frustum.intersectsAABB(box));
        }
    }
    REQUIRE(fullyInside > 0);
}

// ============================================================================
// PROPERTY 10: AABB far outside the frustum is rejected
// ============================================================================

TEST_CASE("Frustum property: AABB behind the camera is rejected",
          "[frustum][property][aabb]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    // AABB centred behind the camera (positive Z is "behind" the eye in
    // the engine's right-handed convention).
    AABB behind(vec3(-1.0f, -1.0f, 5.0f), vec3(1.0f, 1.0f, 10.0f));
    REQUIRE_FALSE(frustum.intersectsAABB(behind));
    REQUIRE_FALSE(frustum.containsAABB(behind));
}

// ============================================================================
// PROPERTY 11: AABB straddling a plane has intersects=true, contains=false
// ============================================================================

TEST_CASE("Frustum property: AABB straddling far plane intersects but is not contained",
          "[frustum][property][aabb]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP(0.1f, 100.0f));
    // AABB from z = -50 (inside) to z = -150 (past the 100m far plane).
    AABB straddle(vec3(-1.0f, -1.0f, -150.0f), vec3(1.0f, 1.0f, -50.0f));
    REQUIRE(frustum.intersectsAABB(straddle));
    REQUIRE_FALSE(frustum.containsAABB(straddle));
}

// ============================================================================
// PROPERTY 12: signedDistance respects plane direction
// ============================================================================

TEST_CASE("Frustum property: plane signedDistance signs match plane definition",
          "[frustum][property][plane]") {
    // Build a known plane: normal = +Y, d = 0 → "ground plane" at y = 0.
    // A point above (y > 0) should have positive signed distance; below
    // (y < 0) negative; on the plane exactly zero.
    Plane ground(vec3(0.0f, 1.0f, 0.0f), 0.0f);
    REQUIRE(ground.signedDistance(vec3(0.0f, 2.0f, 0.0f)) == Approx(2.0f));
    REQUIRE(ground.signedDistance(vec3(0.0f, -1.5f, 0.0f)) == Approx(-1.5f));
    REQUIRE(ground.signedDistance(vec3(10.0f, 0.0f, -3.0f)) == Approx(0.0f));
}

// ============================================================================
// PROPERTY 13: Plane normalisation is idempotent
// ============================================================================

TEST_CASE("Frustum property: Plane::normalize() is idempotent",
          "[frustum][property][plane]") {
    Plane plane(vec3(3.0f, 4.0f, 0.0f), 5.0f);
    plane.normalize();
    REQUIRE(plane.normal.length() == Approx(1.0f).margin(1e-5f));
    Plane copy = plane;
    copy.normalize();
    REQUIRE(copy.normal.x == Approx(plane.normal.x).margin(1e-6f));
    REQUIRE(copy.normal.y == Approx(plane.normal.y).margin(1e-6f));
    REQUIRE(copy.normal.z == Approx(plane.normal.z).margin(1e-6f));
    REQUIRE(copy.d == Approx(plane.d).margin(1e-6f));
}

// ============================================================================
// PROPERTY 14: Plane built from 3 points has unit normal
// ============================================================================

TEST_CASE("Frustum property: Plane(p0, p1, p2) constructs unit-normal plane",
          "[frustum][property][plane]") {
    Plane plane(vec3(0.0f, 0.0f, 0.0f),
                vec3(1.0f, 0.0f, 0.0f),
                vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(plane.normal.length() == Approx(1.0f).margin(1e-5f));
    // The triangle lies in z=0 plane → normal should be ±Z. The cross
    // (e1 - e0) × (e2 - e0) for these points gives (+0, +0, +1).
    REQUIRE(std::abs(plane.normal.z) == Approx(1.0f).margin(1e-5f));
}

// ============================================================================
// PROPERTY 15: Frustum corners reconstruct under inverse-VP
// ============================================================================

TEST_CASE("Frustum property: getCorners produces 8 distinct points",
          "[frustum][property][corners]") {
    const mat4 vp = MakeCanonicalVP();
    const mat4 invVp = vp.inverse();
    const Frustum frustum = Frustum::fromMatrix(vp);
    const auto corners = frustum.getCorners(invVp);
    REQUIRE(corners.size() == 8);
    // All 8 corners distinct (otherwise the frustum degenerated).
    for (size_t i = 0; i < 8; ++i) {
        for (size_t j = i + 1; j < 8; ++j) {
            const vec3 diff = corners[i] - corners[j];
            REQUIRE(diff.length() > 1e-3f);
        }
    }
}

// ============================================================================
// PROPERTY 16: All 8 frustum corners are inside (or on the boundary of) the frustum
// ============================================================================

TEST_CASE("Frustum property: extracted corners are on or inside the frustum",
          "[frustum][property][corners]") {
    const mat4 vp = MakeCanonicalVP();
    const mat4 invVp = vp.inverse();
    const Frustum frustum = Frustum::fromMatrix(vp);
    const auto corners = frustum.getCorners(invVp);
    for (size_t i = 0; i < 8; ++i) {
        // Each corner is on the boundary, so signedDistance is ~0 for
        // 3 of the 6 planes and positive for the other 3. We just
        // require that the WORST signed distance is small in magnitude
        // (within plane-extraction fp tolerance).
        float worstNegative = 0.0f;
        for (size_t p = 0; p < 6; ++p) {
            const float d = frustum.planes[p].signedDistance(corners[i]);
            worstNegative = std::min(worstNegative, d);
        }
        // The corners are mathematically on three planes simultaneously;
        // fp error gives a small negative drift. Tolerate 1e-2 (the
        // matrix divides through (near, far) of ~100, so 1e-2 is
        // generous).
        REQUIRE(worstNegative > -1e-2f);
    }
}

// ============================================================================
// PROPERTY 17: Frustum::contains is consistent under random sweep
// ============================================================================

TEST_CASE("Frustum property: contains agrees with per-plane signedDistance loop",
          "[frustum][property][contains]") {
    // Frustum::contains() is implemented as a 6-plane signedDistance
    // loop with < 0 as outside. Manually replicate that and compare.
    std::mt19937 rng(kPropertySeed ^ 0x3u);
    std::uniform_real_distribution<float> coord(-50.0f, 50.0f);
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    for (int i = 0; i < 500; ++i) {
        const vec3 p(coord(rng), coord(rng), coord(rng));
        bool manualInside = true;
        for (const auto& plane : frustum.planes) {
            if (plane.signedDistance(p) < 0.0f) {
                manualInside = false;
                break;
            }
        }
        REQUIRE(frustum.contains(p) == manualInside);
    }
}

// ============================================================================
// PROPERTY 18: Sphere with radius > distance from center to plane intersects
// ============================================================================

TEST_CASE("Frustum property: sphere reaching across plane is detected as intersecting",
          "[frustum][property][sphere]") {
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP());
    const Plane& farPlane = frustum.planes[Frustum::FAR];

    // Find a point INSIDE the frustum near the far plane.
    const vec3 insideRef(0.0f, 0.0f, -50.0f);
    const float distInside = farPlane.signedDistance(insideRef);
    REQUIRE(distInside > 0.0f);

    // Place a sphere at insideRef with radius < distInside → fully
    // inside frustum w.r.t. the far plane → intersects=true.
    REQUIRE(frustum.intersectsSphere(insideRef, distInside * 0.5f));

    // Place a sphere centered slightly outside the far plane with a
    // large enough radius to spill back in. The sphere intersects the
    // frustum (its near-side surface is inside) but is not entirely
    // contained.
    const vec3 outside = insideRef - farPlane.normal * (distInside * 2.0f);
    REQUIRE_FALSE(frustum.contains(outside));
    REQUIRE(frustum.intersectsSphere(outside, distInside * 3.0f));
}

// ============================================================================
// PROPERTY 19: Frustum scales with near/far values
// ============================================================================

TEST_CASE("Frustum property: increasing far plane extends frustum",
          "[frustum][property][geometry]") {
    // A point at z = -200 should be OUTSIDE the default 100m far frustum
    // and INSIDE a 500m far frustum.
    const Frustum near100 = Frustum::fromMatrix(MakeCanonicalVP(0.1f, 100.0f));
    const Frustum near500 = Frustum::fromMatrix(MakeCanonicalVP(0.1f, 500.0f));
    const vec3 deepPoint(0.0f, 0.0f, -200.0f);
    REQUIRE_FALSE(near100.contains(deepPoint));
    REQUIRE(near500.contains(deepPoint));
}

// ============================================================================
// PROPERTY 20: A point on the optical axis at varying depths
// ============================================================================

TEST_CASE("Frustum property: optical-axis depth sweep matches near/far range",
          "[frustum][property][geometry]") {
    const float near = 0.5f;
    const float far = 50.0f;
    const Frustum frustum = Frustum::fromMatrix(MakeCanonicalVP(near, far));

    // Outside (in front of near).
    REQUIRE_FALSE(frustum.contains(vec3(0.0f, 0.0f, -0.1f)));
    // Inside.
    REQUIRE(frustum.contains(vec3(0.0f, 0.0f, -1.0f)));
    REQUIRE(frustum.contains(vec3(0.0f, 0.0f, -25.0f)));
    REQUIRE(frustum.contains(vec3(0.0f, 0.0f, -49.0f)));
    // Outside (past far).
    REQUIRE_FALSE(frustum.contains(vec3(0.0f, 0.0f, -100.0f)));
}

// ============================================================================
// PROPERTY 21: Ray-plane intersection succeeds for non-parallel rays
// ============================================================================

TEST_CASE("Frustum property: Plane::intersects handles non-parallel rays",
          "[frustum][property][plane]") {
    // Ground plane at y=0; a downward-pointing ray from (0, 5, 0) hits
    // at t=5. An upward-pointing ray misses (or hits with negative t
    // → returns false).
    Plane ground(vec3(0.0f, 1.0f, 0.0f), 0.0f);
    Engine::Ray downward(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f));
    float t = 0.0f;
    REQUIRE(ground.intersects(downward, t));
    REQUIRE(t == Approx(5.0f).margin(1e-5f));

    Engine::Ray upward(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
    REQUIRE_FALSE(ground.intersects(upward, t));

    // Ray parallel to plane: returns false regardless of origin.
    Engine::Ray parallel(vec3(0.0f, 5.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));
    REQUIRE_FALSE(ground.intersects(parallel, t));
}

// ============================================================================
// PROPERTY 22: Plane::project lands on the plane
// ============================================================================

TEST_CASE("Frustum property: Plane::project produces zero signed-distance result",
          "[frustum][property][plane]") {
    std::mt19937 rng(kPropertySeed ^ 0x4u);
    std::uniform_real_distribution<float> coord(-10.0f, 10.0f);
    for (int i = 0; i < 100; ++i) {
        Plane plane(vec3(coord(rng), coord(rng), coord(rng)), coord(rng));
        plane.normalize();
        if (plane.normal.length() < 0.5f) continue; // degenerate
        const vec3 p(coord(rng), coord(rng), coord(rng));
        const vec3 projected = plane.project(p);
        REQUIRE(std::abs(plane.signedDistance(projected)) < 1e-3f);
    }
}

// ============================================================================
// PROPERTY 23: Frustum from view*proj == Frustum from VP
// ============================================================================

TEST_CASE("Frustum property: fromMatrices and fromMatrix agree",
          "[frustum][property][api]") {
    const mat4 proj = mat4::perspective(60.0f * Engine::Math::DEG_TO_RAD,
                                         16.0f / 9.0f, 0.1f, 100.0f);
    const mat4 view = mat4::lookAt(vec3(2.0f, 3.0f, 5.0f),
                                    vec3(0.0f, 0.0f, 0.0f),
                                    vec3(0.0f, 1.0f, 0.0f));
    const Frustum a = Frustum::fromMatrix(proj * view);
    const Frustum b = Frustum::fromMatrices(view, proj);
    for (size_t i = 0; i < 6; ++i) {
        REQUIRE(a.planes[i].normal.x == Approx(b.planes[i].normal.x).margin(1e-6f));
        REQUIRE(a.planes[i].normal.y == Approx(b.planes[i].normal.y).margin(1e-6f));
        REQUIRE(a.planes[i].normal.z == Approx(b.planes[i].normal.z).margin(1e-6f));
        REQUIRE(a.planes[i].d == Approx(b.planes[i].d).margin(1e-6f));
    }
}

// ============================================================================
// PROPERTY 24: Enclosing AABB contains every frustum corner
// ============================================================================

TEST_CASE("Frustum property: getEnclosingAABB contains every frustum corner",
          "[frustum][property][aabb]") {
    const mat4 vp = MakeCanonicalVP();
    const mat4 invVp = vp.inverse();
    const Frustum frustum = Frustum::fromMatrix(vp);
    const auto corners = frustum.getCorners(invVp);
    const AABB aabb = frustum.getEnclosingAABB(invVp);
    for (const auto& corner : corners) {
        REQUIRE(corner.x >= aabb.min.x - 1e-3f);
        REQUIRE(corner.x <= aabb.max.x + 1e-3f);
        REQUIRE(corner.y >= aabb.min.y - 1e-3f);
        REQUIRE(corner.y <= aabb.max.y + 1e-3f);
        REQUIRE(corner.z >= aabb.min.z - 1e-3f);
        REQUIRE(corner.z <= aabb.max.z + 1e-3f);
    }
}

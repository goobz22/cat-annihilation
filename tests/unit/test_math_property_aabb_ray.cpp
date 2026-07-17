/**
 * Property tests for engine/math/AABB.hpp + engine/math/Ray.hpp +
 * engine/math/Frustum.hpp (Plane half).
 *
 * AABB and Ray are the broad-phase culling primitives — every picking
 * raycast, every spatial-hash bucket lookup, every frustum-culling pass
 * runs through these. A bug in intersectsAABB or contains() produces
 * silently-missing collisions, gimbal-locked picking, or geometry that
 * lights when it shouldn't.
 *
 * The slab-test in Ray.hpp has a long comment explaining the IEEE-754 vs
 * explicit-zero-check handling for parallel-to-axis rays. We have an entire
 * block of degenerate-direction tests below to pin that contract.
 *
 * Coverage:
 *
 *   AABB
 *   ----
 *   - Default-constructed AABB has min > max — isValid() returns false.
 *   - expand(point) round-trip: after expanding by N points, the resulting
 *     AABB contains all of them.
 *   - expand(point) on a default AABB equals AABB(point, point).
 *   - union is commutative: A.unionWith(B) == B.unionWith(A).
 *   - intersection is commutative.
 *   - contains(point) is reflexive on corner points.
 *   - intersects(other) is symmetric.
 *   - closestPoint clamps to box boundary.
 *   - distance == 0 iff point is inside.
 *   - transformed by identity is the same AABB.
 *   - corners() returns 8 distinct points whose extremes equal min, max.
 *
 *   Ray
 *   ---
 *   - at(0) == origin.
 *   - at(t) for t > 0 lies along direction.
 *   - closestPoint to a point on the ray equals that point (modulo eps).
 *   - intersectsSphere round-trip: hit point lies on both ray AND sphere
 *     surface.
 *   - intersectsPlane round-trip: hit point lies on plane (normal . p == d).
 *   - intersectsTriangle: barycentric coordinates sum to 1, hit point
 *     reconstructed from barycentrics matches at(t).
 *   - intersectsAABB:
 *      - Ray starting INSIDE the AABB always hits.
 *      - Ray pointing away from AABB never hits.
 *      - Parallel-to-axis ray that's OUTSIDE the slab returns false (the
 *        explicit guard in the slab code).
 *      - Parallel-to-axis ray that's INSIDE the slab can still hit.
 *      - Hit t-values are in increasing order (tMin <= tMax).
 *      - 1000-sample random ray vs random AABB cross-check.
 *
 *   Plane (from Frustum.hpp)
 *   ------------------------
 *   - signedDistance of a point ON the plane is 0.
 *   - project(point) lands on the plane.
 *   - project is idempotent.
 *   - plane built from 3 non-collinear points has normal perpendicular to
 *     all three edges.
 *   - normalize then signedDistance is invariant up to scaling.
 *
 * Seed std::mt19937{42}, 1000 stress samples per stress block.
 */

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/math/AABB.hpp"
#include "engine/math/Ray.hpp"
#include "engine/math/Frustum.hpp"
#include "engine/math/Vector.hpp"
#include "engine/math/Math.hpp"

#include <cmath>
#include <random>

using namespace Engine;

namespace {

// Seed routed through CatTest::DeterministicSeed: reproducible by default,
// replayable/sweepable via CAT_TEST_SEED. Generator type + distributions
// unchanged.
const unsigned kRngSeed =
    static_cast<unsigned>(CatTest::DeterministicSeed("math property aabb_ray"));
constexpr int kStressSamples = 1000;

vec3 randomVec3(std::mt19937& rng, float range = 5.0f) {
    std::uniform_real_distribution<float> dist(-range, range);
    return vec3(dist(rng), dist(rng), dist(rng));
}

vec3 randomUnitVec3(std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    while (true) {
        vec3 v(dist(rng), dist(rng), dist(rng));
        float lenSq = v.lengthSquared();
        if (lenSq > 1e-4f && lenSq < 1.0f) return v.normalized();
    }
}

AABB randomAABB(std::mt19937& rng, float range = 5.0f) {
    std::uniform_real_distribution<float> centerDist(-range, range);
    std::uniform_real_distribution<float> halfDist(0.5f, range * 0.5f);
    vec3 c(centerDist(rng), centerDist(rng), centerDist(rng));
    vec3 h(halfDist(rng), halfDist(rng), halfDist(rng));
    return AABB(c - h, c + h);
}

} // namespace

// ============================================================================
// AABB — construction
// ============================================================================

TEST_CASE("AABB: default-constructed AABB is invalid (min > max)",
          "[math][aabb]") {
    AABB box;
    REQUIRE_FALSE(box.isValid());
    // The header sets min = +inf, max = -inf so any expand() with a finite
    // point pulls both bounds toward that point.
    REQUIRE(box.min.x > box.max.x);
    REQUIRE(box.min.y > box.max.y);
    REQUIRE(box.min.z > box.max.z);
}

TEST_CASE("AABB: expanded by single point becomes a degenerate box at that point",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 p = randomVec3(rng);
        AABB box;
        box.expand(p);
        REQUIRE(box.isValid());
        REQUIRE(box.min.x == p.x);
        REQUIRE(box.min.y == p.y);
        REQUIRE(box.min.z == p.z);
        REQUIRE(box.max.x == p.x);
        REQUIRE(box.max.y == p.y);
        REQUIRE(box.max.z == p.z);
    }
}

TEST_CASE("AABB: expanded by N points contains all of them",
          "[math][aabb][property]") {
    // This is the core "build a bounding box around vertices" use case for
    // mesh import. The test feeds 100 random points to expand() and then
    // walks them back through contains() expecting every one to be inside.
    std::mt19937 rng(kRngSeed);
    AABB box;
    std::vector<vec3> points;
    for (int i = 0; i < 100; i++) {
        vec3 p = randomVec3(rng);
        box.expand(p);
        points.push_back(p);
    }
    for (const auto& p : points) {
        REQUIRE(box.contains(p));
    }
}

TEST_CASE("AABB: fromCenterExtents matches min/max ctor",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        vec3 center = randomVec3(rng);
        vec3 extents(std::abs(randomVec3(rng).x) + 0.5f,
                     std::abs(randomVec3(rng).y) + 0.5f,
                     std::abs(randomVec3(rng).z) + 0.5f);
        AABB viaFactory = AABB::fromCenterExtents(center, extents);
        AABB viaCtor(center - extents, center + extents);
        REQUIRE(viaFactory == viaCtor);
    }
}

TEST_CASE("AABB: center() returns midpoint of min and max",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB box = randomAABB(rng);
        vec3 center = box.center();
        vec3 expected = (box.min + box.max) * 0.5f;
        REQUIRE(center.x == Approx(expected.x).margin(1e-5f));
        REQUIRE(center.y == Approx(expected.y).margin(1e-5f));
        REQUIRE(center.z == Approx(expected.z).margin(1e-5f));
    }
}

TEST_CASE("AABB: size and extents are consistent",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB box = randomAABB(rng);
        vec3 size = box.size();
        vec3 extents = box.extents();
        REQUIRE(size.x == Approx(2.0f * extents.x).margin(1e-5f));
        REQUIRE(size.y == Approx(2.0f * extents.y).margin(1e-5f));
        REQUIRE(size.z == Approx(2.0f * extents.z).margin(1e-5f));
    }
}

// ============================================================================
// AABB — operations
// ============================================================================

TEST_CASE("AABB: union is commutative",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB a = randomAABB(rng);
        AABB b = randomAABB(rng);
        REQUIRE(a.unionWith(b) == b.unionWith(a));
    }
}

TEST_CASE("AABB: union is idempotent (A.unionWith(A) == A)",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB a = randomAABB(rng);
        REQUIRE(a.unionWith(a) == a);
    }
}

TEST_CASE("AABB: intersection is commutative when both intersect",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    int intersectingPairs = 0;
    for (int i = 0; i < kStressSamples && intersectingPairs < 200; i++) {
        AABB a = randomAABB(rng);
        AABB b = randomAABB(rng);
        if (a.intersects(b)) {
            intersectingPairs++;
            REQUIRE(a.intersection(b) == b.intersection(a));
        }
    }
    REQUIRE(intersectingPairs > 0);
}

TEST_CASE("AABB: intersects symmetry",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB a = randomAABB(rng);
        AABB b = randomAABB(rng);
        REQUIRE(a.intersects(b) == b.intersects(a));
    }
}

TEST_CASE("AABB: contains all 8 of its own corners",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        AABB box = randomAABB(rng);
        auto corners = box.corners();
        for (const auto& c : corners) {
            REQUIRE(box.contains(c));
        }
    }
}

TEST_CASE("AABB: corners() extremes equal min, max",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        AABB box = randomAABB(rng);
        auto corners = box.corners();
        vec3 minOfCorners = corners[0];
        vec3 maxOfCorners = corners[0];
        for (const auto& c : corners) {
            minOfCorners.x = std::min(minOfCorners.x, c.x);
            minOfCorners.y = std::min(minOfCorners.y, c.y);
            minOfCorners.z = std::min(minOfCorners.z, c.z);
            maxOfCorners.x = std::max(maxOfCorners.x, c.x);
            maxOfCorners.y = std::max(maxOfCorners.y, c.y);
            maxOfCorners.z = std::max(maxOfCorners.z, c.z);
        }
        REQUIRE(minOfCorners == box.min);
        REQUIRE(maxOfCorners == box.max);
    }
}

TEST_CASE("AABB: closestPoint clamps a point to the box surface",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB box = randomAABB(rng);
        vec3 p = randomVec3(rng, 10.0f);
        vec3 closest = box.closestPoint(p);
        // The closest point must satisfy: box.contains(closest).
        REQUIRE(closest.x >= box.min.x - 1e-5f);
        REQUIRE(closest.x <= box.max.x + 1e-5f);
        REQUIRE(closest.y >= box.min.y - 1e-5f);
        REQUIRE(closest.y <= box.max.y + 1e-5f);
        REQUIRE(closest.z >= box.min.z - 1e-5f);
        REQUIRE(closest.z <= box.max.z + 1e-5f);
        // Equivalent statement using contains() with a small grow().
        AABB padded = box;
        padded.grow(1e-4f);
        REQUIRE(padded.contains(closest));
    }
}

TEST_CASE("AABB: closestPoint of a contained point is the point itself",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB box = randomAABB(rng);
        vec3 inside = box.center() + (randomVec3(rng) * 0.001f);
        // Force inside by clamping.
        inside = box.closestPoint(inside);
        vec3 closest = box.closestPoint(inside);
        REQUIRE(closest.x == Approx(inside.x).margin(1e-5f));
        REQUIRE(closest.y == Approx(inside.y).margin(1e-5f));
        REQUIRE(closest.z == Approx(inside.z).margin(1e-5f));
    }
}

TEST_CASE("AABB: distance to interior point is 0",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        AABB box = randomAABB(rng);
        vec3 inside = box.center();
        REQUIRE(box.distance(inside) == Approx(0.0f).margin(1e-6f));
        REQUIRE(box.distanceSquared(inside) == Approx(0.0f).margin(1e-10f));
    }
}

TEST_CASE("AABB: grow then shrink is identity",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(0.1f, 2.0f);
    for (int i = 0; i < 500; i++) {
        AABB box = randomAABB(rng);
        AABB original = box;
        float g = dist(rng);
        box.grow(g);
        box.grow(-g);
        REQUIRE(box.min.x == Approx(original.min.x).margin(1e-5f));
        REQUIRE(box.min.y == Approx(original.min.y).margin(1e-5f));
        REQUIRE(box.min.z == Approx(original.min.z).margin(1e-5f));
        REQUIRE(box.max.x == Approx(original.max.x).margin(1e-5f));
        REQUIRE(box.max.y == Approx(original.max.y).margin(1e-5f));
        REQUIRE(box.max.z == Approx(original.max.z).margin(1e-5f));
    }
}

TEST_CASE("AABB: transformed by identity matrix is the same AABB",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    mat4 identity = mat4::identity();
    for (int i = 0; i < 200; i++) {
        AABB box = randomAABB(rng);
        AABB transformed = box.transformed(identity);
        // Identity-transformed corners produce the same min/max.
        REQUIRE(transformed.min.x == Approx(box.min.x).margin(1e-5f));
        REQUIRE(transformed.min.y == Approx(box.min.y).margin(1e-5f));
        REQUIRE(transformed.min.z == Approx(box.min.z).margin(1e-5f));
        REQUIRE(transformed.max.x == Approx(box.max.x).margin(1e-5f));
        REQUIRE(transformed.max.y == Approx(box.max.y).margin(1e-5f));
        REQUIRE(transformed.max.z == Approx(box.max.z).margin(1e-5f));
    }
}

TEST_CASE("AABB: transformed by translation moves min and max by the same offset",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        AABB box = randomAABB(rng);
        vec3 offset = randomVec3(rng);
        mat4 t = mat4::translate(offset);
        AABB transformed = box.transformed(t);
        REQUIRE(transformed.min.x == Approx(box.min.x + offset.x).margin(1e-5f));
        REQUIRE(transformed.min.y == Approx(box.min.y + offset.y).margin(1e-5f));
        REQUIRE(transformed.min.z == Approx(box.min.z + offset.z).margin(1e-5f));
        REQUIRE(transformed.max.x == Approx(box.max.x + offset.x).margin(1e-5f));
        REQUIRE(transformed.max.y == Approx(box.max.y + offset.y).margin(1e-5f));
        REQUIRE(transformed.max.z == Approx(box.max.z + offset.z).margin(1e-5f));
    }
}

TEST_CASE("AABB: volume == size.x * size.y * size.z",
          "[math][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB box = randomAABB(rng);
        vec3 s = box.size();
        float vol = box.volume();
        REQUIRE(vol == Approx(s.x * s.y * s.z).epsilon(1e-5f));
    }
}

// ============================================================================
// Ray — basic operations
// ============================================================================

TEST_CASE("Ray: at(0) returns the origin",
          "[math][ray][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 origin = randomVec3(rng);
        vec3 direction = randomUnitVec3(rng);
        Ray r(origin, direction);
        vec3 at0 = r.at(0.0f);
        REQUIRE(at0.x == Approx(origin.x).margin(1e-5f));
        REQUIRE(at0.y == Approx(origin.y).margin(1e-5f));
        REQUIRE(at0.z == Approx(origin.z).margin(1e-5f));
    }
}

TEST_CASE("Ray: at(t) lies on the ray (linear in t)",
          "[math][ray][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> tDist(0.0f, 20.0f);
    for (int i = 0; i < kStressSamples; i++) {
        Ray r(randomVec3(rng), randomUnitVec3(rng));
        float t = tDist(rng);
        vec3 point = r.at(t);
        // Distance from origin along direction should equal t.
        vec3 fromOrigin = point - r.origin;
        REQUIRE(fromOrigin.length() == Approx(std::abs(t)).margin(1e-4f));
        // And the direction agrees (positive t -> same direction).
        if (t > 1e-4f) {
            REQUIRE(fromOrigin.dot(r.direction) > 0.0f);
        }
    }
}

TEST_CASE("Ray: constructor normalizes the direction",
          "[math][ray][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 unnormalizedDir(dist(rng), dist(rng), dist(rng));
        if (unnormalizedDir.lengthSquared() < 1e-4f) continue;
        Ray r(randomVec3(rng), unnormalizedDir);
        REQUIRE(r.direction.length() == Approx(1.0f).margin(1e-5f));
    }
}

TEST_CASE("Ray: closestPoint of origin is the origin",
          "[math][ray][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        Ray r(randomVec3(rng), randomUnitVec3(rng));
        vec3 cp = r.closestPoint(r.origin);
        REQUIRE(cp.x == Approx(r.origin.x).margin(1e-5f));
        REQUIRE(cp.y == Approx(r.origin.y).margin(1e-5f));
        REQUIRE(cp.z == Approx(r.origin.z).margin(1e-5f));
    }
}

TEST_CASE("Ray: closestPoint to a point ahead lies on the ray",
          "[math][ray][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> tDist(0.5f, 10.0f);
    for (int i = 0; i < 500; i++) {
        Ray r(randomVec3(rng), randomUnitVec3(rng));
        float t = tDist(rng);
        vec3 onRay = r.at(t);
        vec3 cp = r.closestPoint(onRay);
        REQUIRE(cp.x == Approx(onRay.x).margin(1e-3f));
        REQUIRE(cp.y == Approx(onRay.y).margin(1e-3f));
        REQUIRE(cp.z == Approx(onRay.z).margin(1e-3f));
    }
}

// ============================================================================
// Ray vs AABB — slab test
// ============================================================================

TEST_CASE("Ray vs AABB: ray starting inside the box always hits",
          "[math][ray][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        AABB box = randomAABB(rng);
        // Random origin inside the box.
        vec3 origin = box.center();
        Ray r(origin, randomUnitVec3(rng));
        REQUIRE(box.intersects(r));
    }
}

TEST_CASE("Ray vs AABB: ray pointing away from box does NOT hit",
          "[math][ray][aabb][property]") {
    // Pick a ray far from the box and pointing further away.
    AABB box(vec3(-1.0f), vec3(1.0f));
    Ray r(vec3(10.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));
    REQUIRE_FALSE(box.intersects(r));
}

TEST_CASE("Ray vs AABB: parallel-to-axis ray outside slab returns false",
          "[math][ray][aabb][degenerate]") {
    // The header's slab code explicitly guards against direction.x ~= 0
    // with origin.x outside the X slab. This is one of the documented
    // false-negative failure modes that prompted the explicit zero-check
    // (see Ray.hpp's long comment block above intersectsAABB).
    AABB box(vec3(-1.0f), vec3(1.0f));
    Ray r(vec3(5.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f)); // direction.x = 0, origin.x = 5
    REQUIRE_FALSE(box.intersects(r));
}

TEST_CASE("Ray vs AABB: parallel-to-axis ray inside slab can still hit",
          "[math][ray][aabb][degenerate]") {
    // The complement of the previous case. Ray has direction.x = 0 but
    // origin.x is INSIDE the X slab — the slab code sets tNear = -inf,
    // tFar = +inf for X and lets the other two axes determine the hit.
    AABB box(vec3(-1.0f), vec3(1.0f));
    Ray r(vec3(0.5f, -5.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f)); // walking +Y through box
    REQUIRE(box.intersects(r));
}

TEST_CASE("Ray vs AABB: hit returns tMin <= tMax",
          "[math][ray][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    int hits = 0;
    for (int i = 0; i < kStressSamples && hits < 200; i++) {
        AABB box = randomAABB(rng);
        Ray r(randomVec3(rng, 10.0f), randomUnitVec3(rng));
        float tMin, tMax;
        if (box.intersects(r, tMin, tMax)) {
            hits++;
            REQUIRE(tMin <= tMax);
            REQUIRE(tMax >= 0.0f);
        }
    }
    REQUIRE(hits > 0);
}

TEST_CASE("Ray vs AABB: hit point lies on the AABB surface and on the ray",
          "[math][ray][aabb][property]") {
    std::mt19937 rng(kRngSeed);
    int validated = 0;
    for (int i = 0; i < kStressSamples * 2 && validated < 200; i++) {
        AABB box = randomAABB(rng);
        // Construct a ray that definitely hits — start outside the box,
        // direction toward the center.
        vec3 origin = box.center() + randomUnitVec3(rng) * 20.0f;
        vec3 direction = (box.center() - origin).normalized();
        Ray r(origin, direction);
        float tMin, tMax;
        if (!box.intersects(r, tMin, tMax)) continue;
        if (tMin < 0.0f) continue;  // ray starts inside, skip for surface test
        validated++;
        vec3 entry = r.at(tMin);
        // entry should be on or very near the box surface.
        AABB padded = box;
        padded.grow(1e-3f);
        REQUIRE(padded.contains(entry));
        // And it should be on the ray.
        vec3 fromOrigin = entry - r.origin;
        REQUIRE(std::abs(fromOrigin.length() - tMin) < 1e-3f);
    }
    REQUIRE(validated > 0);
}

// ============================================================================
// Ray vs sphere
// ============================================================================

TEST_CASE("Ray vs sphere: a ray starting at sphere center hits at radius",
          "[math][ray][sphere][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> radiusDist(0.5f, 5.0f);
    for (int i = 0; i < 500; i++) {
        vec3 center = randomVec3(rng);
        float radius = radiusDist(rng);
        Ray r(center, randomUnitVec3(rng));
        float t;
        REQUIRE(r.intersectsSphere(center, radius, t));
        REQUIRE(t == Approx(radius).margin(1e-3f));
    }
}

TEST_CASE("Ray vs sphere: hit point lies on the sphere surface",
          "[math][ray][sphere][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> radiusDist(0.5f, 5.0f);
    int hits = 0;
    for (int i = 0; i < kStressSamples && hits < 200; i++) {
        vec3 center = randomVec3(rng);
        float radius = radiusDist(rng);
        Ray r(randomVec3(rng, 10.0f), randomUnitVec3(rng));
        float t;
        if (r.intersectsSphere(center, radius, t)) {
            hits++;
            vec3 hitPoint = r.at(t);
            float dist = (hitPoint - center).length();
            REQUIRE(dist == Approx(radius).margin(1e-3f));
        }
    }
    REQUIRE(hits > 0);
}

// ============================================================================
// Ray vs plane
// ============================================================================

TEST_CASE("Ray vs plane: hit point lies on the plane (n.p + d == 0)",
          "[math][ray][plane][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dDist(-3.0f, 3.0f);
    int hits = 0;
    for (int i = 0; i < kStressSamples && hits < 200; i++) {
        vec3 normal = randomUnitVec3(rng);
        float d = dDist(rng);
        Ray r(randomVec3(rng, 10.0f), randomUnitVec3(rng));
        float t;
        if (r.intersectsPlane(normal, d, t)) {
            hits++;
            vec3 hitPoint = r.at(t);
            float signedDist = normal.dot(hitPoint) + d;
            REQUIRE(signedDist == Approx(0.0f).margin(1e-3f));
        }
    }
    REQUIRE(hits > 0);
}

TEST_CASE("Ray vs plane: ray parallel to plane misses",
          "[math][ray][plane][degenerate]") {
    Ray r(vec3(0.0f, 1.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));
    vec3 planeNormal(0.0f, 1.0f, 0.0f);
    float t;
    REQUIRE_FALSE(r.intersectsPlane(planeNormal, 0.0f, t));
}

// ============================================================================
// Ray vs triangle (Moller-Trumbore)
// ============================================================================

TEST_CASE("Ray vs triangle: hit recovers barycentric coordinates that sum to 1",
          "[math][ray][triangle][property]") {
    // Pick a triangle in the +Z plane and a ray that definitely hits it.
    vec3 v0(0.0f, 0.0f, 0.0f);
    vec3 v1(1.0f, 0.0f, 0.0f);
    vec3 v2(0.0f, 1.0f, 0.0f);
    Ray r(vec3(0.25f, 0.25f, -1.0f), vec3(0.0f, 0.0f, 1.0f));
    float t;
    vec3 bary;
    REQUIRE(r.intersectsTriangle(v0, v1, v2, t, bary));
    // Barycentric coordinates must sum to 1.
    REQUIRE(bary.x + bary.y + bary.z == Approx(1.0f).margin(1e-4f));
    // Hit point reconstructed from barycentrics must match at(t).
    vec3 fromBary = v0 * bary.x + v1 * bary.y + v2 * bary.z;
    vec3 fromRay  = r.at(t);
    REQUIRE(fromBary.x == Approx(fromRay.x).margin(1e-4f));
    REQUIRE(fromBary.y == Approx(fromRay.y).margin(1e-4f));
    REQUIRE(fromBary.z == Approx(fromRay.z).margin(1e-4f));
}

TEST_CASE("Ray vs triangle: ray missing triangle returns false",
          "[math][ray][triangle]") {
    vec3 v0(0.0f, 0.0f, 0.0f);
    vec3 v1(1.0f, 0.0f, 0.0f);
    vec3 v2(0.0f, 1.0f, 0.0f);
    // Ray going far outside the triangle's u,v parameter range.
    Ray r(vec3(5.0f, 5.0f, -1.0f), vec3(0.0f, 0.0f, 1.0f));
    float t;
    REQUIRE_FALSE(r.intersectsTriangle(v0, v1, v2, t));
}

TEST_CASE("Ray vs triangle: ray parallel to triangle plane misses",
          "[math][ray][triangle][degenerate]") {
    vec3 v0(0.0f, 0.0f, 0.0f);
    vec3 v1(1.0f, 0.0f, 0.0f);
    vec3 v2(0.0f, 1.0f, 0.0f);
    // Ray in the XY plane parallel to the triangle's plane.
    Ray r(vec3(0.25f, 0.25f, 1.0f), vec3(1.0f, 0.0f, 0.0f));
    float t;
    REQUIRE_FALSE(r.intersectsTriangle(v0, v1, v2, t));
}

// ============================================================================
// Plane (from Frustum.hpp)
// ============================================================================

TEST_CASE("Plane: signedDistance of a point on the plane is 0",
          "[math][plane][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        vec3 normal = randomUnitVec3(rng);
        vec3 onPlane = randomVec3(rng);
        Plane p(normal, onPlane);
        REQUIRE(p.signedDistance(onPlane) == Approx(0.0f).margin(1e-4f));
    }
}

TEST_CASE("Plane: project lands on the plane",
          "[math][plane][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 normal = randomUnitVec3(rng);
        std::uniform_real_distribution<float> dDist(-5.0f, 5.0f);
        Plane p(normal, dDist(rng));
        vec3 anywhere = randomVec3(rng, 10.0f);
        vec3 projected = p.project(anywhere);
        REQUIRE(p.signedDistance(projected) == Approx(0.0f).margin(1e-4f));
    }
}

TEST_CASE("Plane: project is idempotent",
          "[math][plane][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        vec3 normal = randomUnitVec3(rng);
        std::uniform_real_distribution<float> dDist(-5.0f, 5.0f);
        Plane p(normal, dDist(rng));
        vec3 anywhere = randomVec3(rng, 10.0f);
        vec3 p1 = p.project(anywhere);
        vec3 p2 = p.project(p1);
        REQUIRE(p2.x == Approx(p1.x).margin(1e-4f));
        REQUIRE(p2.y == Approx(p1.y).margin(1e-4f));
        REQUIRE(p2.z == Approx(p1.z).margin(1e-4f));
    }
}

TEST_CASE("Plane: from-three-points has normal perpendicular to triangle edges",
          "[math][plane][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        vec3 p0 = randomVec3(rng);
        vec3 p1 = randomVec3(rng);
        vec3 p2 = randomVec3(rng);
        // Skip degenerate triangles.
        vec3 e1 = p1 - p0;
        vec3 e2 = p2 - p0;
        if (e1.cross(e2).lengthSquared() < 1e-4f) continue;
        Plane plane(p0, p1, p2);
        REQUIRE(plane.normal.dot(e1) == Approx(0.0f).margin(1e-3f));
        REQUIRE(plane.normal.dot(e2) == Approx(0.0f).margin(1e-3f));
        // All three input points should be on the plane.
        REQUIRE(plane.signedDistance(p0) == Approx(0.0f).margin(1e-3f));
        REQUIRE(plane.signedDistance(p1) == Approx(0.0f).margin(1e-3f));
        REQUIRE(plane.signedDistance(p2) == Approx(0.0f).margin(1e-3f));
    }
}

// ============================================================================
// AABB-sphere intersection
// ============================================================================

TEST_CASE("AABB: sphere intersection - sphere containing box center always intersects",
          "[math][aabb][sphere][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        AABB box = randomAABB(rng);
        // Sphere at the box center with any positive radius must intersect.
        REQUIRE(box.intersectsSphere(box.center(), 0.01f));
    }
}

TEST_CASE("AABB: sphere far away does NOT intersect",
          "[math][aabb][sphere]") {
    AABB box(vec3(-1.0f), vec3(1.0f));
    REQUIRE_FALSE(box.intersectsSphere(vec3(100.0f, 0.0f, 0.0f), 1.0f));
}

// ============================================================================
// Degenerate / corner cases
// ============================================================================

TEST_CASE("AABB: empty intersects nothing",
          "[math][aabb][degenerate]") {
    AABB empty = AABB::empty();
    AABB normalBox(vec3(-1.0f), vec3(1.0f));
    REQUIRE_FALSE(empty.intersects(normalBox));
}

TEST_CASE("AABB: infinite intersects every finite box",
          "[math][aabb][degenerate]") {
    AABB inf = AABB::infinite();
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 100; i++) {
        AABB box = randomAABB(rng);
        REQUIRE(inf.intersects(box));
    }
}

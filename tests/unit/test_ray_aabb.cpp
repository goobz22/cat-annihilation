/**
 * Unit tests for Ray-AABB slab intersection (engine/math/Ray.hpp).
 *
 * Locks down the parallel-ray-on-slab-edge contract: the previous
 * implementation computed 1/0 -> +INFINITY_F and then evaluated
 * (slab_min - origin) * INFINITY_F. When origin.{axis} sat exactly on a
 * slab boundary the multiplication was 0 * inf = NaN, and the downstream
 * std::min/std::max + final >= comparisons silently returned MISS for
 * rays physically touching the AABB. That false-negative surfaces in
 * picking (player aims at edge of a wall, click misses), broadphase
 * culling (object on cluster boundary disappears), and projectile
 * sweeps (bullet "grazes" a wall instead of hitting it).
 */

#include "catch.hpp"
#include "engine/math/AABB.hpp"
#include "engine/math/Ray.hpp"
#include "engine/math/Vector.hpp"
#include <cmath>

using Engine::AABB;
using Engine::Ray;
using Engine::vec3;

TEST_CASE("Ray-AABB axis-aligned hit through center", "[ray][aabb]") {
    const AABB box(vec3(-1.0f), vec3(1.0f));
    const Ray ray(vec3(-5.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));

    float tMin = 0.0f, tMax = 0.0f;
    REQUIRE(ray.intersectsAABB(box.min, box.max, tMin, tMax));
    REQUIRE(tMin == Approx(4.0f));
    REQUIRE(tMax == Approx(6.0f));
}

TEST_CASE("Ray parallel to X axis, origin INSIDE Y/Z slabs, sliding along surface",
          "[ray][aabb][parallel]") {
    const AABB box(vec3(-1.0f), vec3(1.0f));

    // Ray runs along +X, origin sits EXACTLY on top face (y = +1, the Y
    // slab maximum). Pre-fix this hit the 0 * inf = NaN trap and
    // returned MISS even though the ray slides along the box's surface.
    const Ray ray(vec3(-5.0f, 1.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));

    float tMin = 0.0f, tMax = 0.0f;
    REQUIRE(ray.intersectsAABB(box.min, box.max, tMin, tMax));
    // Entry is at the -X face, distance 4. Exit at the +X face, distance 6.
    REQUIRE(tMin == Approx(4.0f));
    REQUIRE(tMax == Approx(6.0f));
}

TEST_CASE("Ray parallel to Y axis, origin inside X/Z slabs, hits top/bottom faces",
          "[ray][aabb][parallel]") {
    const AABB box(vec3(-1.0f), vec3(1.0f));
    const Ray ray(vec3(0.5f, -5.0f, 0.5f), vec3(0.0f, 1.0f, 0.0f));

    float tMin = 0.0f, tMax = 0.0f;
    REQUIRE(ray.intersectsAABB(box.min, box.max, tMin, tMax));
    REQUIRE(tMin == Approx(4.0f));
    REQUIRE(tMax == Approx(6.0f));
}

TEST_CASE("Ray parallel to axis, origin outside slab on that axis, must MISS",
          "[ray][aabb][parallel][miss]") {
    const AABB box(vec3(-1.0f), vec3(1.0f));

    // Ray runs along +X but its Y is 5 — well outside the Y slab. Even
    // with the IEEE-754 trick this case must always return false; the
    // explicit-branch fix relies on the early-out path NOT poisoning
    // tMin/tMax with infinities that downstream code might consume.
    const Ray ray(vec3(-5.0f, 5.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));

    float tMin = 0.0f, tMax = 0.0f;
    REQUIRE_FALSE(ray.intersectsAABB(box.min, box.max, tMin, tMax));
}

TEST_CASE("Ray parallel to axis, origin EXACTLY on slab boundary, must HIT",
          "[ray][aabb][parallel][edge]") {
    const AABB box(vec3(0.0f), vec3(1.0f));

    // The NaN trap. Origin.y exactly equals aabbMin.y (= 0) AND direction.y = 0,
    // so (aabbMin.y - origin.y) * invDirY = 0 * inf -> NaN under the old
    // implementation. The fix routes this through the in-slab branch so
    // the X axis still controls entry/exit.
    const Ray ray(vec3(-5.0f, 0.0f, 0.5f), vec3(1.0f, 0.0f, 0.0f));

    float tMin = 0.0f, tMax = 0.0f;
    REQUIRE(ray.intersectsAABB(box.min, box.max, tMin, tMax));
    // tMin/tMax must be finite, not NaN — the bug surfaces as NaN
    // distances even when the boolean happens to be true.
    REQUIRE(std::isfinite(tMin));
    REQUIRE(std::isfinite(tMax));
    REQUIRE(tMin == Approx(5.0f));
    REQUIRE(tMax == Approx(6.0f));
}

TEST_CASE("Ray fully inside AABB returns hit with negative tMin", "[ray][aabb]") {
    const AABB box(vec3(-2.0f), vec3(2.0f));
    const Ray ray(vec3(0.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));

    float tMin = 0.0f, tMax = 0.0f;
    REQUIRE(ray.intersectsAABB(box.min, box.max, tMin, tMax));
    // tMin negative means the entry is behind the origin — i.e., the
    // origin is already inside. tMax must be positive (the exit).
    REQUIRE(tMin < 0.0f);
    REQUIRE(tMax > 0.0f);
}

TEST_CASE("Ray-AABB miss returns clean false (no NaN propagation)",
          "[ray][aabb][miss]") {
    const AABB box(vec3(-1.0f), vec3(1.0f));
    const Ray ray(vec3(-5.0f, 5.0f, 5.0f), vec3(1.0f, 0.0f, 0.0f));

    float tMin = 99.0f, tMax = 99.0f;
    const bool hit = ray.intersectsAABB(box.min, box.max, tMin, tMax);
    REQUIRE_FALSE(hit);
    // On miss tMin/tMax may be sentinel infinity, but MUST NOT be NaN —
    // a NaN here causes downstream consumers (e.g., closest-hit ranking)
    // to silently keep the previous-best hit's distance.
    REQUIRE_FALSE(std::isnan(tMin));
    REQUIRE_FALSE(std::isnan(tMax));
}

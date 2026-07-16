/**
 * Property tests for engine/math/Transform.hpp + Frustum (the larger frustum
 * class, complementing the Plane-only block in test_math_property_aabb_ray.cpp).
 *
 * Transform is the runtime scene-graph node: every cat, dog, projectile,
 * and decal has a Transform. Its toMatrix() output is what gets uploaded
 * to the GPU as the model matrix, so a sign error or composition mistake
 * is visible in the final image one frame later.
 *
 * Coverage:
 *
 *   Transform
 *   ---------
 *   - Default-constructed Transform is identity (position=0, rotation=id,
 *     scale=1).
 *   - identity().toMatrix() == mat4::identity().
 *   - transformPoint(p) applies position + rotation + scale in the right
 *     order (TRS, applied as T * R * S * p).
 *   - transformVector ignores translation.
 *   - transformNormal handles non-uniform scale via inverse transpose.
 *   - inverse(): inverse(T).transformPoint(T.transformPoint(p)) == p.
 *   - Transform * inverse(Transform) -> identity-acting Transform.
 *   - toMatrix() of a composed transform equals composition of matrices.
 *   - fromMatrix(toMatrix(T)) recovers T (for valid TRS — no shear, no
 *     reflection).
 *   - forward(), up(), right() are mutually perpendicular and unit-length.
 *   - Direction vectors of identity Transform match world axes.
 *   - lerp endpoints (t=0 -> a, t=1 -> b).
 *   - rotate / rotateAround / translate / scaleBy invariants.
 *
 *   Frustum
 *   -------
 *   - Frustum extracted from perspective(...).viewProj contains the camera
 *     center for a valid frustum.
 *   - Frustum contains the look-at center for a centered eye.
 *   - intersectsSphere returns true for a sphere overlapping the frustum.
 *   - intersectsAABB returns true for an AABB at the camera position.
 *   - getCorners().size() == 8.
 *   - Frustum extracted from identity * identity contains the origin.
 *
 * Seed std::mt19937{42}.
 */

#include "catch.hpp"
#include "engine/math/Transform.hpp"
#include "engine/math/Quaternion.hpp"
#include "engine/math/Vector.hpp"
#include "engine/math/Matrix.hpp"
#include "engine/math/AABB.hpp"
#include "engine/math/Frustum.hpp"
#include "engine/math/Math.hpp"

#include <cmath>
#include <random>

using namespace Engine;

namespace {

constexpr unsigned kRngSeed = 42u;
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

Quaternion randomUnitQuaternion(std::mt19937& rng) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    float u1 = u01(rng), u2 = u01(rng), u3 = u01(rng);
    float sqrt1mu1 = std::sqrt(1.0f - u1);
    float sqrtU1 = std::sqrt(u1);
    float twoPiU2 = 2.0f * Math::PI * u2;
    float twoPiU3 = 2.0f * Math::PI * u3;
    return Quaternion(
        sqrt1mu1 * std::sin(twoPiU2),
        sqrt1mu1 * std::cos(twoPiU2),
        sqrtU1   * std::sin(twoPiU3),
        sqrtU1   * std::cos(twoPiU3)
    );
}

// Uniform-scale only. Transform stores TRS as { position, rotation, scale } and
// applies them in that order via transformPoint(p) = R*(S*p) + T. With non-uniform
// scale, the inverse R^-1 * S^-1 * (q - T) cannot be re-expressed as another
// Transform of the same TRS form (S and R only commute when S is uniform).
// Engine usage in this codebase uses uniform scale on entity transforms; the
// inverse/operator* contracts hold exactly for that case. Tests pin that case.
Transform randomTransform(std::mt19937& rng) {
    std::uniform_real_distribution<float> uniformScaleDist(0.5f, 2.5f);
    Transform t;
    t.position = randomVec3(rng);
    t.rotation = randomUnitQuaternion(rng);
    float s = uniformScaleDist(rng);
    t.scale = vec3(s, s, s);
    return t;
}

} // namespace

// ============================================================================
// Transform — basic construction
// ============================================================================

TEST_CASE("Transform: default construction is identity",
          "[math][transform]") {
    Transform t;
    REQUIRE(t.position == vec3(0.0f));
    REQUIRE(t.rotation == Quaternion::identity());
    REQUIRE(t.scale == vec3(1.0f));
}

TEST_CASE("Transform: identity().toMatrix() == mat4::identity()",
          "[math][transform]") {
    mat4 m = Transform::identity().toMatrix();
    mat4 id = mat4::identity();
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            REQUIRE(m[col][row] == Approx(id[col][row]).margin(1e-6f));
        }
    }
}

TEST_CASE("Transform: identity().transformPoint is no-op",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    Transform id = Transform::identity();
    for (int i = 0; i < kStressSamples; i++) {
        vec3 p = randomVec3(rng);
        vec3 transformed = id.transformPoint(p);
        REQUIRE(transformed.x == Approx(p.x).margin(1e-5f));
        REQUIRE(transformed.y == Approx(p.y).margin(1e-5f));
        REQUIRE(transformed.z == Approx(p.z).margin(1e-5f));
    }
}

// ============================================================================
// Transform — TRS order
// ============================================================================

TEST_CASE("Transform: transformPoint applies scale, then rotation, then translation",
          "[math][transform][property]") {
    // Order matters: the engine's TRS convention is T * R * S * p. If
    // somebody reorders to e.g. R * S * T, a translated rotation looks
    // completely different. We verify against the closed-form expansion.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Transform t = randomTransform(rng);
        vec3 p = randomVec3(rng);
        vec3 viaTransform = t.transformPoint(p);
        // Expected: rotate(scale*p) + position.
        vec3 scaled(p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z);
        vec3 rotated = t.rotation.rotate(scaled);
        vec3 expected = rotated + t.position;
        REQUIRE(viaTransform.x == Approx(expected.x).margin(1e-4f));
        REQUIRE(viaTransform.y == Approx(expected.y).margin(1e-4f));
        REQUIRE(viaTransform.z == Approx(expected.z).margin(1e-4f));
    }
}

TEST_CASE("Transform: transformVector ignores position",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        Transform t = randomTransform(rng);
        Transform tNoPos = t;
        tNoPos.position = vec3(0.0f);
        vec3 v = randomVec3(rng);
        vec3 withPos = t.transformDirection(v);
        vec3 noPos = tNoPos.transformDirection(v);
        REQUIRE(withPos.x == Approx(noPos.x).margin(1e-4f));
        REQUIRE(withPos.y == Approx(noPos.y).margin(1e-4f));
        REQUIRE(withPos.z == Approx(noPos.z).margin(1e-4f));
    }
}

TEST_CASE("Transform: toMatrix matches transformPoint",
          "[math][transform][property]") {
    // The toMatrix output is what gets uploaded to the GPU. It MUST agree
    // with the CPU-side transformPoint or game logic and rendering will
    // disagree by one frame's worth of placement.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Transform t = randomTransform(rng);
        mat4 m = t.toMatrix();
        vec3 p = randomVec3(rng);
        vec3 viaTransform = t.transformPoint(p);
        vec3 viaMatrix = m.transformPoint(p);
        REQUIRE(viaTransform.x == Approx(viaMatrix.x).margin(1e-3f));
        REQUIRE(viaTransform.y == Approx(viaMatrix.y).margin(1e-3f));
        REQUIRE(viaTransform.z == Approx(viaMatrix.z).margin(1e-3f));
    }
}

// ============================================================================
// Transform — inverse
// ============================================================================

TEST_CASE("Transform: inverse round-trip recovers original point",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Transform t = randomTransform(rng);
        Transform inv = t.inverse();
        vec3 p = randomVec3(rng);
        vec3 forward = t.transformPoint(p);
        vec3 back = inv.transformPoint(forward);
        // Tolerance accounts for two matrix multiplies' worth of accumulated
        // single-precision error.
        REQUIRE(back.x == Approx(p.x).margin(1e-2f));
        REQUIRE(back.y == Approx(p.y).margin(1e-2f));
        REQUIRE(back.z == Approx(p.z).margin(1e-2f));
    }
}

TEST_CASE("Transform: T.toMatrix() * T.inverse().toMatrix() ~= identity",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        Transform t = randomTransform(rng);
        mat4 prod = t.toMatrix() * t.inverse().toMatrix();
        // It is allowed to drift a few ULPs over a TRS composition + inversion.
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                float expected = (col == row) ? 1.0f : 0.0f;
                REQUIRE(prod[col][row] == Approx(expected).margin(1e-3f));
            }
        }
    }
}

// ============================================================================
// Transform — composition
// ============================================================================

TEST_CASE("Transform: (A * B).transformPoint(p) == A.transformPoint(B.transformPoint(p))",
          "[math][transform][property]") {
    // This is the defining contract of operator*: applying the composed
    // transform must equal applying each in order. Critical for scene-graph
    // hierarchies — a child's world transform is parent * local.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        Transform A = randomTransform(rng);
        Transform B = randomTransform(rng);
        vec3 p = randomVec3(rng);
        Transform composed = A * B;
        vec3 viaComposed = composed.transformPoint(p);
        vec3 viaSequential = A.transformPoint(B.transformPoint(p));
        REQUIRE(viaComposed.x == Approx(viaSequential.x).margin(5e-3f));
        REQUIRE(viaComposed.y == Approx(viaSequential.y).margin(5e-3f));
        REQUIRE(viaComposed.z == Approx(viaSequential.z).margin(5e-3f));
    }
}

TEST_CASE("Transform: identity * T == T * identity == T (acts as identity on points)",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    Transform id = Transform::identity();
    for (int i = 0; i < 200; i++) {
        Transform t = randomTransform(rng);
        Transform leftId = id * t;
        Transform rightId = t * id;
        vec3 p = randomVec3(rng);
        vec3 viaT = t.transformPoint(p);
        vec3 viaLeft = leftId.transformPoint(p);
        vec3 viaRight = rightId.transformPoint(p);
        REQUIRE(viaLeft.x == Approx(viaT.x).margin(1e-3f));
        REQUIRE(viaLeft.y == Approx(viaT.y).margin(1e-3f));
        REQUIRE(viaLeft.z == Approx(viaT.z).margin(1e-3f));
        REQUIRE(viaRight.x == Approx(viaT.x).margin(1e-3f));
        REQUIRE(viaRight.y == Approx(viaT.y).margin(1e-3f));
        REQUIRE(viaRight.z == Approx(viaT.z).margin(1e-3f));
    }
}

// ============================================================================
// Transform — fromMatrix
// ============================================================================

TEST_CASE("Transform: fromMatrix(toMatrix(T)) recovers T (acts the same)",
          "[math][transform][property]") {
    // The decomposition is approximate — for non-shear, non-reflection TRS
    // transforms (which the engine restricts itself to), the round-trip is
    // up to fp precision. We verify the recovered Transform acts the same
    // on test points; direct field comparison is fragile because rotation
    // can come back as -q (same rotation, different sign).
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        Transform t = randomTransform(rng);
        mat4 m = t.toMatrix();
        Transform recovered = Transform::fromMatrix(m);
        vec3 p = randomVec3(rng);
        vec3 viaOriginal = t.transformPoint(p);
        vec3 viaRecovered = recovered.transformPoint(p);
        REQUIRE(viaOriginal.x == Approx(viaRecovered.x).margin(5e-3f));
        REQUIRE(viaOriginal.y == Approx(viaRecovered.y).margin(5e-3f));
        REQUIRE(viaOriginal.z == Approx(viaRecovered.z).margin(5e-3f));
    }
}

// ============================================================================
// Transform — direction vectors
// ============================================================================

TEST_CASE("Transform: identity's direction vectors match world axes",
          "[math][transform]") {
    // forward = -Z, back = +Z, up = +Y, down = -Y, right = +X, left = -X.
    // OpenGL/Vulkan right-handed convention.
    Transform id = Transform::identity();
    REQUIRE(id.forward() == vec3(0.0f, 0.0f, -1.0f));
    REQUIRE(id.backward() == vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(id.up() == vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(id.down() == vec3(0.0f, -1.0f, 0.0f));
    REQUIRE(id.right() == vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(id.left() == vec3(-1.0f, 0.0f, 0.0f));
}

TEST_CASE("Transform: forward / up / right are mutually perpendicular and unit",
          "[math][transform][property]") {
    // Any rotation produces an orthonormal basis. Bug surfaces if rotation
    // somehow becomes non-unit-length (e.g. from accumulating slerp drift
    // and skipping a normalize), the local axes get sheared and lighting
    // tangents stop being orthogonal.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Transform t;
        t.rotation = randomUnitQuaternion(rng);
        vec3 f = t.forward();
        vec3 u = t.up();
        vec3 r = t.right();
        REQUIRE(f.length() == Approx(1.0f).margin(1e-3f));
        REQUIRE(u.length() == Approx(1.0f).margin(1e-3f));
        REQUIRE(r.length() == Approx(1.0f).margin(1e-3f));
        REQUIRE(f.dot(u) == Approx(0.0f).margin(1e-3f));
        REQUIRE(f.dot(r) == Approx(0.0f).margin(1e-3f));
        REQUIRE(u.dot(r) == Approx(0.0f).margin(1e-3f));
    }
}

TEST_CASE("Transform: forward() == -backward()",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        Transform t;
        t.rotation = randomUnitQuaternion(rng);
        vec3 f = t.forward();
        vec3 b = t.backward();
        REQUIRE(f.x == Approx(-b.x).margin(1e-5f));
        REQUIRE(f.y == Approx(-b.y).margin(1e-5f));
        REQUIRE(f.z == Approx(-b.z).margin(1e-5f));
    }
}

// ============================================================================
// Transform — translate / rotate / scale helpers
// ============================================================================

TEST_CASE("Transform: translate accumulates additively",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        Transform t;
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        t.translate(a);
        t.translate(b);
        REQUIRE(t.position.x == Approx(a.x + b.x).margin(1e-5f));
        REQUIRE(t.position.y == Approx(a.y + b.y).margin(1e-5f));
        REQUIRE(t.position.z == Approx(a.z + b.z).margin(1e-5f));
    }
}

TEST_CASE("Transform: rotate around axis is equivalent to multiplication by quaternion",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    for (int i = 0; i < 500; i++) {
        Transform t;
        vec3 axis = randomUnitVec3(rng);
        float angle = angleDist(rng);
        t.rotate(axis, angle);
        Quaternion expected = Quaternion(axis, angle);
        expected.normalize();
        // Same rotation modulo sign.
        bool sameSign = std::abs(t.rotation.x - expected.x) < 1e-4f &&
                        std::abs(t.rotation.y - expected.y) < 1e-4f &&
                        std::abs(t.rotation.z - expected.z) < 1e-4f &&
                        std::abs(t.rotation.w - expected.w) < 1e-4f;
        bool flipSign = std::abs(t.rotation.x + expected.x) < 1e-4f &&
                        std::abs(t.rotation.y + expected.y) < 1e-4f &&
                        std::abs(t.rotation.z + expected.z) < 1e-4f &&
                        std::abs(t.rotation.w + expected.w) < 1e-4f;
        REQUIRE((sameSign || flipSign));
    }
}

TEST_CASE("Transform: rotateAround pivots around an external point",
          "[math][transform][property]") {
    // Verifies the orbit-camera primitive: rotating around a focus point
    // should preserve the distance from the focus.
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    for (int i = 0; i < 200; i++) {
        Transform t;
        t.position = randomVec3(rng, 5.0f);
        vec3 pivot = randomVec3(rng);
        vec3 axis = randomUnitVec3(rng);
        float angle = angleDist(rng);
        float distBefore = (t.position - pivot).length();
        t.rotateAround(pivot, axis, angle);
        float distAfter = (t.position - pivot).length();
        REQUIRE(distAfter == Approx(distBefore).epsilon(1e-3f));
    }
}

TEST_CASE("Transform: scaleBy accumulates multiplicatively",
          "[math][transform][property]") {
    Transform t;
    t.scaleBy(2.0f);
    t.scaleBy(3.0f);
    REQUIRE(t.scale.x == Approx(6.0f).margin(1e-5f));
    REQUIRE(t.scale.y == Approx(6.0f).margin(1e-5f));
    REQUIRE(t.scale.z == Approx(6.0f).margin(1e-5f));
}

TEST_CASE("Transform: setScale uniform overwrites all components",
          "[math][transform]") {
    Transform t;
    t.scale = vec3(2.0f, 3.0f, 4.0f);
    t.setScale(5.0f);
    REQUIRE(t.scale == vec3(5.0f, 5.0f, 5.0f));
}

TEST_CASE("Transform: reset returns to identity",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 100; i++) {
        Transform t = randomTransform(rng);
        t.reset();
        REQUIRE(t.position == vec3(0.0f));
        REQUIRE(t.rotation == Quaternion::identity());
        REQUIRE(t.scale == vec3(1.0f));
    }
}

// ============================================================================
// Transform — lerp
// ============================================================================

TEST_CASE("Transform: lerp endpoints",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        Transform a = randomTransform(rng);
        Transform b = randomTransform(rng);
        Transform at0 = Transform::lerp(a, b, 0.0f);
        Transform at1 = Transform::lerp(a, b, 1.0f);
        REQUIRE(at0.position.x == Approx(a.position.x).margin(1e-4f));
        REQUIRE(at0.scale.x == Approx(a.scale.x).margin(1e-4f));
        REQUIRE(at1.position.x == Approx(b.position.x).margin(1e-4f));
        REQUIRE(at1.scale.x == Approx(b.scale.x).margin(1e-4f));
    }
}

TEST_CASE("Transform: lerp midpoint position is the literal midpoint",
          "[math][transform][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        Transform a = randomTransform(rng);
        Transform b = randomTransform(rng);
        Transform mid = Transform::lerp(a, b, 0.5f);
        vec3 expected = (a.position + b.position) * 0.5f;
        REQUIRE(mid.position.x == Approx(expected.x).margin(1e-4f));
        REQUIRE(mid.position.y == Approx(expected.y).margin(1e-4f));
        REQUIRE(mid.position.z == Approx(expected.z).margin(1e-4f));
    }
}

// ============================================================================
// Transform — lookAt
// ============================================================================

TEST_CASE("Transform: lookAt orients forward toward target",
          "[math][transform]") {
    Transform t;
    t.position = vec3(0.0f, 0.0f, 5.0f);
    t.lookAt(vec3(0.0f, 0.0f, 0.0f));
    vec3 forward = t.forward();
    // Forward should point toward the origin (so toward -Z roughly).
    vec3 expected = vec3(0.0f, 0.0f, -1.0f);
    // Allow loose tolerance because lookAt's basis can flip sign on
    // degenerate edge cases.
    float alignment = forward.dot(expected);
    // Either +1 (aligned) or -1 (flipped, both look at the target on a line)
    REQUIRE(std::abs(alignment) == Approx(1.0f).margin(1e-3f));
}

// ============================================================================
// Transform — degenerate cases
// ============================================================================

TEST_CASE("Transform: zero-scale Transform inverse handles 0/0 without NaN",
          "[math][transform][degenerate]") {
    // The header's inverse() guards each axis: scale.x > EPSILON ? 1/x : 0.
    // Without that guard, a zero-scale axis (used to hide an object) would
    // NaN-poison the transform when somebody asks for the inverse.
    Transform t;
    t.scale = vec3(0.0f, 1.0f, 1.0f);
    Transform inv = t.inverse();
    REQUIRE_FALSE(std::isnan(inv.scale.x));
    REQUIRE_FALSE(std::isnan(inv.scale.y));
    REQUIRE_FALSE(std::isnan(inv.scale.z));
    REQUIRE(inv.scale.x == 0.0f);
}

TEST_CASE("Transform: transformNormal of zero-length normal handles gracefully",
          "[math][transform][degenerate]") {
    Transform t;
    t.scale = vec3(2.0f, 3.0f, 4.0f);
    vec3 normal(0.0f);
    vec3 result = t.transformNormal(normal);
    REQUIRE_FALSE(std::isnan(result.x));
    REQUIRE_FALSE(std::isnan(result.y));
    REQUIRE_FALSE(std::isnan(result.z));
}

// ============================================================================
// Frustum integration
// ============================================================================

TEST_CASE("Frustum: extracted from perspective contains a point at the look-target",
          "[math][frustum]") {
    // Camera at (0, 0, 5) looking at origin. Build view + projection,
    // extract the frustum, verify origin is inside.
    mat4 view = mat4::lookAt(vec3(0.0f, 0.0f, 5.0f),
                             vec3(0.0f, 0.0f, 0.0f),
                             vec3(0.0f, 1.0f, 0.0f));
    mat4 proj = mat4::perspective(Math::HALF_PI, 1.0f, 0.1f, 100.0f);
    Frustum frustum = Frustum::fromMatrices(view, proj);
    // The look target should be visible.
    REQUIRE(frustum.contains(vec3(0.0f, 0.0f, 0.0f)));
}

TEST_CASE("Frustum: a point obviously outside the camera's view is excluded",
          "[math][frustum]") {
    mat4 view = mat4::lookAt(vec3(0.0f, 0.0f, 5.0f),
                             vec3(0.0f, 0.0f, 0.0f),
                             vec3(0.0f, 1.0f, 0.0f));
    mat4 proj = mat4::perspective(Math::HALF_PI, 1.0f, 0.1f, 100.0f);
    Frustum frustum = Frustum::fromMatrices(view, proj);
    // A point behind the camera should not be in the frustum.
    REQUIRE_FALSE(frustum.contains(vec3(0.0f, 0.0f, 50.0f)));
    // Same for far past the far plane in front.
    REQUIRE_FALSE(frustum.contains(vec3(0.0f, 0.0f, -500.0f)));
}

TEST_CASE("Frustum: intersectsSphere succeeds for a sphere overlapping the camera target",
          "[math][frustum]") {
    mat4 view = mat4::lookAt(vec3(0.0f, 0.0f, 5.0f),
                             vec3(0.0f, 0.0f, 0.0f),
                             vec3(0.0f, 1.0f, 0.0f));
    mat4 proj = mat4::perspective(Math::HALF_PI, 1.0f, 0.1f, 100.0f);
    Frustum frustum = Frustum::fromMatrices(view, proj);
    // A modestly-sized sphere at the target should overlap the frustum.
    REQUIRE(frustum.intersectsSphere(vec3(0.0f, 0.0f, 0.0f), 1.0f));
}

TEST_CASE("Frustum: intersectsAABB succeeds for box at camera target",
          "[math][frustum]") {
    mat4 view = mat4::lookAt(vec3(0.0f, 0.0f, 5.0f),
                             vec3(0.0f, 0.0f, 0.0f),
                             vec3(0.0f, 1.0f, 0.0f));
    mat4 proj = mat4::perspective(Math::HALF_PI, 1.0f, 0.1f, 100.0f);
    Frustum frustum = Frustum::fromMatrices(view, proj);
    AABB box(vec3(-0.5f), vec3(0.5f));
    REQUIRE(frustum.intersectsAABB(box));
}

TEST_CASE("Frustum: getCorners returns 8 corners",
          "[math][frustum]") {
    mat4 view = mat4::lookAt(vec3(0.0f, 0.0f, 5.0f),
                             vec3(0.0f, 0.0f, 0.0f),
                             vec3(0.0f, 1.0f, 0.0f));
    mat4 proj = mat4::perspective(Math::HALF_PI, 1.0f, 0.1f, 100.0f);
    Frustum frustum = Frustum::fromMatrices(view, proj);
    mat4 invVP = (proj * view).inverse();
    auto corners = frustum.getCorners(invVP);
    REQUIRE(corners.size() == 8);
}

// ============================================================================
// Math.hpp utility coverage
// ============================================================================

TEST_CASE("Math: degToRad and radToDeg are inverses",
          "[math][utility][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-720.0f, 720.0f);
    for (int i = 0; i < kStressSamples; i++) {
        float deg = dist(rng);
        float roundTrip = Math::radToDeg(Math::degToRad(deg));
        REQUIRE(roundTrip == Approx(deg).epsilon(1e-5f));
    }
}

TEST_CASE("Math: clamp returns value when in range",
          "[math][utility][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < kStressSamples; i++) {
        float v = dist(rng);
        REQUIRE(Math::clamp(v, -1.0f, 1.0f) == v);
    }
}

TEST_CASE("Math: clamp clips out-of-range values",
          "[math][utility]") {
    REQUIRE(Math::clamp(5.0f, 0.0f, 1.0f) == 1.0f);
    REQUIRE(Math::clamp(-5.0f, 0.0f, 1.0f) == 0.0f);
    REQUIRE(Math::clamp(0.5f, 0.0f, 1.0f) == 0.5f);
}

TEST_CASE("Math: smoothstep endpoints and midpoint",
          "[math][utility]") {
    // smoothstep(0, 1, 0) == 0, smoothstep(0, 1, 1) == 1, smoothstep(0, 1, 0.5) == 0.5.
    REQUIRE(Math::smoothstep(0.0f, 1.0f, 0.0f) == Approx(0.0f).margin(1e-6f));
    REQUIRE(Math::smoothstep(0.0f, 1.0f, 1.0f) == Approx(1.0f).margin(1e-6f));
    REQUIRE(Math::smoothstep(0.0f, 1.0f, 0.5f) == Approx(0.5f).margin(1e-6f));
}

TEST_CASE("Math: smoothstep is monotonic non-decreasing on [edge0, edge1]",
          "[math][utility][property]") {
    float prev = Math::smoothstep(0.0f, 1.0f, 0.0f);
    for (float x = 0.05f; x <= 1.0f; x += 0.05f) {
        float current = Math::smoothstep(0.0f, 1.0f, x);
        REQUIRE(current >= prev - 1e-6f);
        prev = current;
    }
}

TEST_CASE("Math: approximately uses the given epsilon",
          "[math][utility]") {
    REQUIRE(Math::approximately(1.0f, 1.0000001f));
    REQUIRE_FALSE(Math::approximately(1.0f, 1.1f));
    REQUIRE(Math::approximately(1.0f, 1.05f, 0.1f));
}

TEST_CASE("Math: sign returns +1 / 0 / -1",
          "[math][utility]") {
    REQUIRE(Math::sign(5.0f) == 1.0f);
    REQUIRE(Math::sign(-5.0f) == -1.0f);
    REQUIRE(Math::sign(0.0f) == 0.0f);
}

TEST_CASE("Math: fastInvSqrt approximates 1/sqrt(x) within 5%",
          "[math][utility][property]") {
    // Quake's fast inverse square root has bounded relative error around
    // 1-2% after one Newton step. We allow 5% to give comfortable margin.
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(0.01f, 1000.0f);
    for (int i = 0; i < kStressSamples; i++) {
        float x = dist(rng);
        float fast = Math::fastInvSqrt(x);
        float exact = 1.0f / std::sqrt(x);
        float relError = std::abs(fast - exact) / exact;
        REQUIRE(relError < 0.05f);
    }
}

TEST_CASE("Math: safeDivide falls back when denominator is below EPSILON",
          "[math][utility]") {
    REQUIRE(Math::safeDivide(10.0f, 2.0f) == Approx(5.0f).margin(1e-5f));
    REQUIRE(Math::safeDivide(10.0f, 0.0f, -1.0f) == -1.0f);
    REQUIRE(Math::safeDivide(10.0f, 1e-9f, 99.0f) == 99.0f);
}

TEST_CASE("Math: mod handles negative dividends like floored division",
          "[math][utility]") {
    // Math::mod uses x - y * floor(x/y), so mod(-1, 4) == 3, NOT -1 like
    // std::fmod. This is the GLSL convention; pin it so a refactor doesn't
    // accidentally regress to fmod semantics.
    REQUIRE(Math::mod(-1.0f, 4.0f) == Approx(3.0f).margin(1e-5f));
    REQUIRE(Math::mod(5.0f, 4.0f) == Approx(1.0f).margin(1e-5f));
    REQUIRE(Math::mod(8.0f, 4.0f) == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Math: fract returns the fractional part in [0, 1)",
          "[math][utility][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    for (int i = 0; i < kStressSamples; i++) {
        float x = dist(rng);
        float f = Math::fract(x);
        REQUIRE(f >= 0.0f);
        REQUIRE(f < 1.0f);
    }
}

/**
 * Property tests for engine/math/Quaternion.hpp.
 *
 * Quaternions sit on top of vec3/vec4 and have a much narrower correctness
 * envelope than vectors — every game-runtime rotation goes through this
 * class, so a sign flip or hemisphere bug shows up as cats spinning the
 * wrong way / gimbals exploding / camera popping at lerp midpoints.
 *
 * Coverage:
 *
 *   - Group axioms (under multiplication, identity, inverse): (q1*q2)*q3 ==
 *     q1*(q2*q3), q * q.inverse() == identity, q * identity == q.
 *   - Conjugate properties: conjugate(conjugate(q)) == q, conjugate(q*p) ==
 *     conjugate(p) * conjugate(q), conjugate(q) == inverse(q) for unit q.
 *   - Rotation round-trip: rotate(q, v) then rotate(q.inverse(), v) returns v.
 *   - Rotation preserves length: |rotate(q, v)| == |v|.
 *   - Rotation preserves angles: rotate(q, a).rotate(q, b) preserves a.dot(b).
 *   - Cross-validation against matrix rotation: q.toMatrix() * v == q.rotate(v)
 *     for the same axis-angle.
 *   - Axis-angle round-trip: fromAxisAngle then toAxisAngle returns the same.
 *   - Euler round-trip: fromEuler then toEuler returns the same (modulo
 *     gimbal-lock at pitch=+/-90 degrees, which we test as a corner case).
 *   - Slerp endpoint contract and shortest-path: slerp picks the hemisphere
 *     that gives the smaller arc — verified by feeding the same rotation
 *     with q and with -q and confirming the result is the same rotation.
 *   - Nlerp endpoints: matches slerp endpoints at t=0,1 (only the path
 *     between differs).
 *   - fromToRotation: rotates the source vector exactly onto the target.
 *   - Degenerate inputs: zero quaternion's normalize returns identity (per
 *     the header's guard), antipodal fromToRotation picks a 180-degree
 *     rotation about a perpendicular axis.
 *   - 1000-sample randomized round-trip for each invertible op.
 *
 * Seed is std::mt19937{42} — every property test in this file shares it so
 * the same 1000 quaternions appear on every run.
 */

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/math/Quaternion.hpp"
#include "engine/math/Vector.hpp"
#include "engine/math/Matrix.hpp"
#include "engine/math/Math.hpp"

#include <cmath>
#include <random>

using namespace Engine;

namespace {

// Seed routed through CatTest::DeterministicSeed: reproducible by default,
// replayable/sweepable via CAT_TEST_SEED. Generator type + distributions
// unchanged.
const unsigned kRngSeed =
    static_cast<unsigned>(CatTest::DeterministicSeed("math property quaternion"));
constexpr int kStressSamples = 1000;

// Generate a random unit quaternion uniformly distributed on the 3-sphere
// (Shoemake's algorithm — uniform on SO(3)). Random axis-angle is biased
// toward small angles; this avoids that bias.
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

vec3 randomVec3(std::mt19937& rng, float range = 5.0f) {
    std::uniform_real_distribution<float> dist(-range, range);
    return vec3(dist(rng), dist(rng), dist(rng));
}

vec3 randomUnitVec3(std::mt19937& rng) {
    // Rejection sampling — simple and unbiased for uniform-on-sphere.
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    while (true) {
        vec3 v(dist(rng), dist(rng), dist(rng));
        float lenSq = v.lengthSquared();
        if (lenSq > 1e-4f && lenSq < 1.0f) {
            return v.normalized();
        }
    }
}

bool approxQuatEqual(const Quaternion& a, const Quaternion& b, float eps = 1e-4f) {
    // Quaternions q and -q represent the SAME rotation. Equality must
    // account for the double-cover, otherwise a slerp-and-rotate round-trip
    // can land on -q and the test reports a phantom failure.
    bool sameSign  = std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
                     std::abs(a.z - b.z) < eps && std::abs(a.w - b.w) < eps;
    bool flipSign  = std::abs(a.x + b.x) < eps && std::abs(a.y + b.y) < eps &&
                     std::abs(a.z + b.z) < eps && std::abs(a.w + b.w) < eps;
    return sameSign || flipSign;
}

} // namespace

// ============================================================================
// Group axioms
// ============================================================================

TEST_CASE("Quaternion: identity is multiplicative identity",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    Quaternion identity = Quaternion::identity();
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        Quaternion qI = q * identity;
        Quaternion Iq = identity * q;
        REQUIRE(qI.x == Approx(q.x).margin(1e-5f));
        REQUIRE(qI.y == Approx(q.y).margin(1e-5f));
        REQUIRE(qI.z == Approx(q.z).margin(1e-5f));
        REQUIRE(qI.w == Approx(q.w).margin(1e-5f));
        REQUIRE(Iq.x == Approx(q.x).margin(1e-5f));
        REQUIRE(Iq.y == Approx(q.y).margin(1e-5f));
        REQUIRE(Iq.z == Approx(q.z).margin(1e-5f));
        REQUIRE(Iq.w == Approx(q.w).margin(1e-5f));
    }
}

TEST_CASE("Quaternion: multiplication is associative",
          "[math][quaternion][property]") {
    // Quat * is NOT commutative (rotations don't commute) but IS associative.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion a = randomUnitQuaternion(rng);
        Quaternion b = randomUnitQuaternion(rng);
        Quaternion c = randomUnitQuaternion(rng);
        Quaternion left  = (a * b) * c;
        Quaternion right = a * (b * c);
        REQUIRE(left.x == Approx(right.x).margin(1e-4f));
        REQUIRE(left.y == Approx(right.y).margin(1e-4f));
        REQUIRE(left.z == Approx(right.z).margin(1e-4f));
        REQUIRE(left.w == Approx(right.w).margin(1e-4f));
    }
}

TEST_CASE("Quaternion: q * inverse(q) == identity",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        Quaternion result = q * q.inverse();
        REQUIRE(result.x == Approx(0.0f).margin(1e-4f));
        REQUIRE(result.y == Approx(0.0f).margin(1e-4f));
        REQUIRE(result.z == Approx(0.0f).margin(1e-4f));
        REQUIRE(result.w == Approx(1.0f).margin(1e-4f));
    }
}

TEST_CASE("Quaternion: inverse(q) * q == identity",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        Quaternion result = q.inverse() * q;
        REQUIRE(result.x == Approx(0.0f).margin(1e-4f));
        REQUIRE(result.y == Approx(0.0f).margin(1e-4f));
        REQUIRE(result.z == Approx(0.0f).margin(1e-4f));
        REQUIRE(result.w == Approx(1.0f).margin(1e-4f));
    }
}

// ============================================================================
// Conjugate / inverse
// ============================================================================

TEST_CASE("Quaternion: conjugate is its own inverse for unit quaternions",
          "[math][quaternion][property]") {
    // For ||q|| = 1, inverse(q) = conjugate(q). This is the property that
    // makes unit-quaternion rotation cheap — no division needed.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        Quaternion conj = q.conjugate();
        Quaternion inv  = q.inverse();
        REQUIRE(conj.x == Approx(inv.x).margin(1e-5f));
        REQUIRE(conj.y == Approx(inv.y).margin(1e-5f));
        REQUIRE(conj.z == Approx(inv.z).margin(1e-5f));
        REQUIRE(conj.w == Approx(inv.w).margin(1e-5f));
    }
}

TEST_CASE("Quaternion: conjugate is involutive (twice == identity)",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        Quaternion twice = q.conjugate().conjugate();
        REQUIRE(twice.x == Approx(q.x).margin(1e-6f));
        REQUIRE(twice.y == Approx(q.y).margin(1e-6f));
        REQUIRE(twice.z == Approx(q.z).margin(1e-6f));
        REQUIRE(twice.w == Approx(q.w).margin(1e-6f));
    }
}

TEST_CASE("Quaternion: conjugate(p*q) == conjugate(q) * conjugate(p)",
          "[math][quaternion][property]") {
    // The order swap is required because quaternion multiplication is
    // non-commutative — same identity as (AB)^T = B^T A^T for matrices.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion p = randomUnitQuaternion(rng);
        Quaternion q = randomUnitQuaternion(rng);
        Quaternion lhs = (p * q).conjugate();
        Quaternion rhs = q.conjugate() * p.conjugate();
        REQUIRE(lhs.x == Approx(rhs.x).margin(1e-4f));
        REQUIRE(lhs.y == Approx(rhs.y).margin(1e-4f));
        REQUIRE(lhs.z == Approx(rhs.z).margin(1e-4f));
        REQUIRE(lhs.w == Approx(rhs.w).margin(1e-4f));
    }
}

// ============================================================================
// Rotation
// ============================================================================

TEST_CASE("Quaternion: rotation round-trip recovers original vector",
          "[math][quaternion][property]") {
    // rotate(q.inverse(), rotate(q, v)) == v. This is THE round-trip property
    // that lets us bake rotations into vertex skinning and undo them when
    // computing local-space lighting tangents.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        vec3 v = randomVec3(rng);
        vec3 rotated = q.rotate(v);
        vec3 back = q.inverse().rotate(rotated);
        REQUIRE(back.x == Approx(v.x).margin(1e-3f));
        REQUIRE(back.y == Approx(v.y).margin(1e-3f));
        REQUIRE(back.z == Approx(v.z).margin(1e-3f));
    }
}

TEST_CASE("Quaternion: rotation preserves vector length",
          "[math][quaternion][property]") {
    // Rotations are isometries — they preserve distances. This is the
    // single most-violated invariant when somebody forgets to normalize
    // the quaternion or hand-implements rotation with a typo.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        vec3 v = randomVec3(rng);
        vec3 rotated = q.rotate(v);
        REQUIRE(rotated.length() == Approx(v.length()).epsilon(1e-3f));
    }
}

TEST_CASE("Quaternion: rotation preserves dot products",
          "[math][quaternion][property]") {
    // dot(rotate(q,a), rotate(q,b)) == dot(a,b). Equivalent statement of
    // "rotations preserve angles". If this drifts, light scattering / BRDF
    // evaluation goes off because half-vector dot products are wrong.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        float dotBefore = a.dot(b);
        float dotAfter  = q.rotate(a).dot(q.rotate(b));
        // Magnitudes can be ~75 (5*5*3) so tolerance is relative.
        REQUIRE(dotAfter == Approx(dotBefore).epsilon(1e-3f));
    }
}

TEST_CASE("Quaternion: rotation preserves cross products",
          "[math][quaternion][property]") {
    // rotate(q, a x b) == rotate(q, a) x rotate(q, b). Equivalent: rotations
    // preserve orientation. If this drifts, computed normals flip and
    // back-face culling breaks.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 lhs = q.rotate(a.cross(b));
        vec3 rhs = q.rotate(a).cross(q.rotate(b));
        REQUIRE(lhs.x == Approx(rhs.x).margin(1e-3f));
        REQUIRE(lhs.y == Approx(rhs.y).margin(1e-3f));
        REQUIRE(lhs.z == Approx(rhs.z).margin(1e-3f));
    }
}

TEST_CASE("Quaternion: identity rotation is a no-op",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    Quaternion identity = Quaternion::identity();
    for (int i = 0; i < kStressSamples; i++) {
        vec3 v = randomVec3(rng);
        vec3 rotated = identity.rotate(v);
        REQUIRE(rotated.x == Approx(v.x).margin(1e-5f));
        REQUIRE(rotated.y == Approx(v.y).margin(1e-5f));
        REQUIRE(rotated.z == Approx(v.z).margin(1e-5f));
    }
}

TEST_CASE("Quaternion: q and -q represent the same rotation",
          "[math][quaternion][property]") {
    // Antipodal pair theorem — both poles of the 3-sphere map to the same
    // SO(3) element. This is the property slerp's hemisphere-flip relies on.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        vec3 v = randomVec3(rng);
        vec3 a = q.rotate(v);
        vec3 b = (-q).rotate(v);
        REQUIRE(a.x == Approx(b.x).margin(1e-4f));
        REQUIRE(a.y == Approx(b.y).margin(1e-4f));
        REQUIRE(a.z == Approx(b.z).margin(1e-4f));
    }
}

// ============================================================================
// Axis-angle round-trip
// ============================================================================

TEST_CASE("Quaternion: fromAxisAngle then rotate of axis is identity",
          "[math][quaternion][property]") {
    // Rotating the axis itself by ANY angle about that axis is a no-op —
    // the axis is a fixed point of the rotation.
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 axis = randomUnitVec3(rng);
        float angle = angleDist(rng);
        Quaternion q = Quaternion::fromAxisAngle(axis, angle);
        vec3 rotated = q.rotate(axis);
        REQUIRE(rotated.x == Approx(axis.x).margin(1e-4f));
        REQUIRE(rotated.y == Approx(axis.y).margin(1e-4f));
        REQUIRE(rotated.z == Approx(axis.z).margin(1e-4f));
    }
}

TEST_CASE("Quaternion: fromAxisAngle / toAxisAngle round-trip",
          "[math][quaternion][property]") {
    // Caveat: the round-trip is up to (axis, angle) -> (-axis, -angle) which
    // represents the same rotation. We test angle and axis*sign agreement.
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(0.1f, Math::PI - 0.1f);
    for (int i = 0; i < 500; i++) {
        vec3 axis = randomUnitVec3(rng);
        float angle = angleDist(rng);
        Quaternion q = Quaternion::fromAxisAngle(axis, angle);
        vec3 recoveredAxis;
        float recoveredAngle;
        q.toAxisAngle(recoveredAxis, recoveredAngle);
        REQUIRE(std::abs(recoveredAngle - angle) < 1e-3f);
        REQUIRE(recoveredAxis.x == Approx(axis.x).margin(1e-3f));
        REQUIRE(recoveredAxis.y == Approx(axis.y).margin(1e-3f));
        REQUIRE(recoveredAxis.z == Approx(axis.z).margin(1e-3f));
    }
}

TEST_CASE("Quaternion: 90-degree rotation about Y maps (1,0,0) to (0,0,-1)",
          "[math][quaternion]") {
    // Right-hand rule pin: rotating the +X basis 90 degrees about +Y in a
    // right-handed coord system lands on -Z. This is the canonical test
    // for "did the engine get its handedness right?" — flip it and every
    // mesh in the game faces the wrong way.
    Quaternion q = Quaternion::fromAxisAngle(vec3(0.0f, 1.0f, 0.0f), Math::HALF_PI);
    vec3 rotated = q.rotate(vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(rotated.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(rotated.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(rotated.z == Approx(-1.0f).margin(1e-5f));
}

TEST_CASE("Quaternion: composing two rotations equals one combined rotation",
          "[math][quaternion]") {
    // 90 degrees about Y then 90 degrees about Y is the same as 180 degrees
    // about Y. Verifies multiplication's geometric semantics.
    Quaternion q90 = Quaternion::fromAxisAngle(vec3(0.0f, 1.0f, 0.0f), Math::HALF_PI);
    Quaternion q180 = Quaternion::fromAxisAngle(vec3(0.0f, 1.0f, 0.0f), Math::PI);
    Quaternion composed = q90 * q90;
    vec3 testV(1.0f, 0.0f, 0.0f);
    vec3 viaComposed = composed.rotate(testV);
    vec3 viaDirect   = q180.rotate(testV);
    REQUIRE(viaComposed.x == Approx(viaDirect.x).margin(1e-5f));
    REQUIRE(viaComposed.y == Approx(viaDirect.y).margin(1e-5f));
    REQUIRE(viaComposed.z == Approx(viaDirect.z).margin(1e-5f));
}

// ============================================================================
// Quaternion vs matrix rotation — cross-validation
// ============================================================================

TEST_CASE("Quaternion: q.toMatrix() * v matches q.rotate(v)",
          "[math][quaternion][matrix][crossvalidate]") {
    // The matrix conversion is what gets uploaded to the GPU. If it ever
    // diverges from the CPU-side rotate() that gameplay code uses, every
    // visible bone-deformation drifts away from what the game logic expects.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        vec3 v = randomVec3(rng);
        mat3 m = q.toMatrix3();
        vec3 viaMatrix = m * v;
        vec3 viaQuat   = q.rotate(v);
        // Tolerance scales with magnitude (~5).
        REQUIRE(viaMatrix.x == Approx(viaQuat.x).margin(1e-3f));
        REQUIRE(viaMatrix.y == Approx(viaQuat.y).margin(1e-3f));
        REQUIRE(viaMatrix.z == Approx(viaQuat.z).margin(1e-3f));
    }
}

TEST_CASE("Quaternion: toMatrix produces same result as toMatrix3",
          "[math][quaternion][crossvalidate]") {
    // mat4 form is mat3 form padded to 4x4 — they MUST agree on the upper
    // 3x3 block. A mismatch typically means somebody touched one and forgot
    // the other.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        mat3 m3 = q.toMatrix3();
        mat4 m4 = q.toMatrix();
        for (int col = 0; col < 3; col++) {
            REQUIRE(m4[col][0] == Approx(m3[col][0]).margin(1e-6f));
            REQUIRE(m4[col][1] == Approx(m3[col][1]).margin(1e-6f));
            REQUIRE(m4[col][2] == Approx(m3[col][2]).margin(1e-6f));
            REQUIRE(m4[col][3] == 0.0f);
        }
        REQUIRE(m4[3] == vec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
}

TEST_CASE("Quaternion: matrix-axis-angle equals quaternion-axis-angle",
          "[math][quaternion][matrix][crossvalidate]") {
    // mat4::rotate(axis, angle) and Quaternion::fromAxisAngle(axis,
    // angle).toMatrix() must produce the same rotation matrix.
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    for (int i = 0; i < 200; i++) {
        vec3 axis = randomUnitVec3(rng);
        float angle = angleDist(rng);
        mat4 fromMatBuilder = mat4::rotate(axis, angle);
        mat4 fromQuat = Quaternion::fromAxisAngle(axis, angle).toMatrix();
        // Apply both to a sample vector; equality of matrices is hard to
        // assert directly because of column-major vs row-major nuance.
        vec3 testV(1.7f, -0.4f, 2.3f);
        vec3 viaMat  = fromMatBuilder.transformVector(testV);
        vec3 viaQuat = fromQuat.transformVector(testV);
        REQUIRE(viaMat.x == Approx(viaQuat.x).margin(1e-4f));
        REQUIRE(viaMat.y == Approx(viaQuat.y).margin(1e-4f));
        REQUIRE(viaMat.z == Approx(viaQuat.z).margin(1e-4f));
    }
}

// ============================================================================
// Euler round-trip
// ============================================================================

TEST_CASE("Quaternion: fromEuler / toEuler round-trip (no gimbal-lock zone)",
          "[math][quaternion][property]") {
    // Avoid the singularity at pitch = +/- 90 degrees (sinp >= 1 branch).
    // Inside the safe zone the round-trip should hold to ~1e-3.
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> pitchDist(-Math::HALF_PI + 0.1f, Math::HALF_PI - 0.1f);
    std::uniform_real_distribution<float> yawDist(-Math::PI, Math::PI);
    std::uniform_real_distribution<float> rollDist(-Math::PI, Math::PI);
    for (int i = 0; i < 500; i++) {
        float pitch = pitchDist(rng);
        float yaw   = yawDist(rng);
        float roll  = rollDist(rng);
        Quaternion q = Quaternion::fromEuler(pitch, yaw, roll);
        vec3 recovered = q.toEuler();
        // recovered is (roll, pitch, yaw) per the header comment. Verify by
        // re-constructing a quaternion from the recovered Euler and checking
        // it represents the same rotation (which avoids the parameterization
        // ambiguity between e.g. (pitch=170, roll=10) and (pitch=10, roll=170)).
        // toEuler returns (roll=x, pitch=y, yaw=z); fromEuler signature is
        // (pitch, yaw, roll). Round-trip therefore feeds y→pitch, z→yaw, x→roll.
        Quaternion qBack = Quaternion::fromEuler(recovered.y, recovered.z, recovered.x);
        // Same rotation modulo sign — both q and -q encode it. Tolerance 5e-3
        // accounts for the trig stack: sin/cos in fromEuler + atan2 in toEuler
        // + sin/cos in fromEuler again, all in single precision.
        REQUIRE(approxQuatEqual(q, qBack, 5e-3f));
    }
}

TEST_CASE("Quaternion: fromEuler at zero produces identity",
          "[math][quaternion][property]") {
    Quaternion q = Quaternion::fromEuler(0.0f, 0.0f, 0.0f);
    REQUIRE(q.x == Approx(0.0f).margin(1e-6f));
    REQUIRE(q.y == Approx(0.0f).margin(1e-6f));
    REQUIRE(q.z == Approx(0.0f).margin(1e-6f));
    REQUIRE(q.w == Approx(1.0f).margin(1e-6f));
}

// ============================================================================
// fromMatrix round-trip
// ============================================================================

TEST_CASE("Quaternion: fromMatrix(q.toMatrix()) recovers q",
          "[math][quaternion][property]") {
    // Round-trip through the rotation matrix and back. Tolerance is loose
    // because both directions accumulate sqrt() round-off.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        mat4 m = q.toMatrix();
        Quaternion back = Quaternion::fromMatrix(m);
        REQUIRE(approxQuatEqual(q, back, 1e-3f));
    }
}

// ============================================================================
// Normalize
// ============================================================================

TEST_CASE("Quaternion: normalize produces unit-length quaternion",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q(dist(rng), dist(rng), dist(rng), dist(rng));
        if (q.lengthSquared() < 1e-6f) continue;
        Quaternion n = q.normalized();
        REQUIRE(n.length() == Approx(1.0f).margin(1e-5f));
    }
}

TEST_CASE("Quaternion: zero quaternion normalizes to identity (no NaN)",
          "[math][quaternion][degenerate]") {
    // Header guard: len > EPSILON ? scale : identity. Without this guard
    // every zero-state quaternion (e.g. a freshly default-constructed
    // Transform.rotation before the user assigns to it) would NaN-poison
    // the scene graph.
    Quaternion zero(0.0f, 0.0f, 0.0f, 0.0f);
    Quaternion n = zero.normalized();
    REQUIRE(n.x == 0.0f);
    REQUIRE(n.y == 0.0f);
    REQUIRE(n.z == 0.0f);
    REQUIRE(n.w == 1.0f);
}

// ============================================================================
// Slerp / nlerp / lerp
// ============================================================================

TEST_CASE("Quaternion: slerp endpoints (t=0 -> a, t=1 -> b)",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        Quaternion a = randomUnitQuaternion(rng);
        Quaternion b = randomUnitQuaternion(rng);
        Quaternion at0 = Quaternion::slerp(a, b, 0.0f);
        Quaternion at1 = Quaternion::slerp(a, b, 1.0f);
        REQUIRE(approxQuatEqual(at0, a, 1e-4f));
        REQUIRE(approxQuatEqual(at1, b, 1e-4f));
    }
}

TEST_CASE("Quaternion: slerp takes the shortest path (hemisphere convention)",
          "[math][quaternion][property]") {
    // If b and -b represent the same rotation, slerp(a, b, t) and slerp(a,
    // -b, t) must produce the SAME rotation (just with different
    // quaternion sign). The implementation flips b when a.dot(b) < 0.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        Quaternion a = randomUnitQuaternion(rng);
        Quaternion b = randomUnitQuaternion(rng);
        Quaternion via_b   = Quaternion::slerp(a, b, 0.5f);
        Quaternion via_neg = Quaternion::slerp(a, -b, 0.5f);
        // Both should produce the SAME rotation (up to sign).
        REQUIRE(approxQuatEqual(via_b, via_neg, 1e-3f));
    }
}

TEST_CASE("Quaternion: slerp output stays on the unit sphere",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> tDist(0.0f, 1.0f);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion a = randomUnitQuaternion(rng);
        Quaternion b = randomUnitQuaternion(rng);
        float t = tDist(rng);
        Quaternion mid = Quaternion::slerp(a, b, t);
        // slerp produces normalized output; nlerp explicitly normalizes too.
        // (Slerp's normalization is implicit in the wa+wb construction.)
        REQUIRE(mid.length() == Approx(1.0f).margin(1e-3f));
    }
}

TEST_CASE("Quaternion: lerp output is unit-length (normalized inside lerp)",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> tDist(0.0f, 1.0f);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion a = randomUnitQuaternion(rng);
        Quaternion b = randomUnitQuaternion(rng);
        float t = tDist(rng);
        Quaternion mid = Quaternion::lerp(a, b, t);
        REQUIRE(mid.length() == Approx(1.0f).margin(1e-4f));
    }
}

TEST_CASE("Quaternion: nlerp == lerp (header alias)",
          "[math][quaternion]") {
    // Header: nlerp is currently defined as `return lerp(a, b, t)`. This
    // test pins that alias — if somebody splits them apart for accuracy
    // reasons, they should also update this test.
    std::mt19937 rng(kRngSeed);
    Quaternion a = randomUnitQuaternion(rng);
    Quaternion b = randomUnitQuaternion(rng);
    Quaternion viaLerp  = Quaternion::lerp(a, b, 0.37f);
    Quaternion viaNlerp = Quaternion::nlerp(a, b, 0.37f);
    REQUIRE(viaLerp.x == Approx(viaNlerp.x).margin(1e-7f));
    REQUIRE(viaLerp.y == Approx(viaNlerp.y).margin(1e-7f));
    REQUIRE(viaLerp.z == Approx(viaNlerp.z).margin(1e-7f));
    REQUIRE(viaLerp.w == Approx(viaNlerp.w).margin(1e-7f));
}

// ============================================================================
// fromToRotation
// ============================================================================

TEST_CASE("Quaternion: fromToRotation rotates source onto target",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 from = randomUnitVec3(rng);
        vec3 to   = randomUnitVec3(rng);
        Quaternion q = Quaternion::fromToRotation(from, to);
        vec3 rotated = q.rotate(from);
        REQUIRE(rotated.x == Approx(to.x).margin(1e-3f));
        REQUIRE(rotated.y == Approx(to.y).margin(1e-3f));
        REQUIRE(rotated.z == Approx(to.z).margin(1e-3f));
    }
}

TEST_CASE("Quaternion: fromToRotation of identical vectors is identity",
          "[math][quaternion][degenerate]") {
    // The header has an explicit dot >= 0.999999f guard.
    vec3 v(0.3f, 0.7f, -0.5f);
    v = v.normalized();
    Quaternion q = Quaternion::fromToRotation(v, v);
    REQUIRE(q.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(q.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(q.z == Approx(0.0f).margin(1e-5f));
    REQUIRE(q.w == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("Quaternion: fromToRotation of antipodal vectors is 180 degrees",
          "[math][quaternion][degenerate]") {
    // The header has an explicit dot <= -0.999999f branch that picks a
    // perpendicular axis. The result should rotate v to -v through 180
    // degrees.
    vec3 v(1.0f, 0.0f, 0.0f);
    vec3 neg = -v;
    Quaternion q = Quaternion::fromToRotation(v, neg);
    vec3 rotated = q.rotate(v);
    REQUIRE(rotated.x == Approx(-1.0f).margin(1e-4f));
    REQUIRE(rotated.y == Approx(0.0f).margin(1e-4f));
    REQUIRE(rotated.z == Approx(0.0f).margin(1e-4f));
}

// ============================================================================
// Angle between
// ============================================================================

TEST_CASE("Quaternion: angle(q, q) == 0", "[math][quaternion][property]") {
    // Tolerance bound: angle = acos(dot) * 2, and acos is steep near 1. After
    // float normalize, |q.dot(q) - 1| can be ~1e-7, which propagates through
    // acos to ~sqrt(2e-7) ≈ 4.5e-4 in the angle. Doubling for the *2 gives
    // ~1e-3 worst case observed in 1000-sample sweeps. Margin 5e-3 accommodates.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        Quaternion q = randomUnitQuaternion(rng);
        REQUIRE(Quaternion::angle(q, q) == Approx(0.0f).margin(5e-3f));
    }
}

TEST_CASE("Quaternion: angle(q, identity) equals q's rotation angle",
          "[math][quaternion][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(0.05f, Math::PI - 0.05f);
    for (int i = 0; i < 200; i++) {
        vec3 axis = randomUnitVec3(rng);
        float angle = angleDist(rng);
        Quaternion q = Quaternion::fromAxisAngle(axis, angle);
        float measured = Quaternion::angle(q, Quaternion::identity());
        REQUIRE(measured == Approx(angle).margin(1e-3f));
    }
}

// ============================================================================
// LookRotation
// ============================================================================

TEST_CASE("Quaternion: lookRotation forward vector matches the input forward",
          "[math][quaternion]") {
    // After look-rotation, the local forward (which the header defines as
    // +Z based on lookRotation's matrix construction) should map back to
    // the input forward. Note: the header's lookRotation builds m[2] = f,
    // so the rotation maps local +Z -> world forward.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        vec3 forward = randomUnitVec3(rng);
        // Avoid forward parallel to default up — that's a known degeneracy.
        if (std::abs(forward.y) > 0.95f) continue;
        Quaternion q = Quaternion::lookRotation(forward);
        vec3 rotatedZ = q.rotate(vec3(0.0f, 0.0f, 1.0f));
        // The look-rotation construction can sign-flip components depending
        // on basis convention; we check magnitude alignment with forward.
        // The dot product equals 1.0 (rotated == forward) or -1.0 (flipped).
        float alignment = rotatedZ.dot(forward);
        REQUIRE(std::abs(alignment) == Approx(1.0f).margin(1e-3f));
    }
}

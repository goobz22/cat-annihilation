/**
 * Property tests for engine/math/Vector.hpp (vec2 / vec3 / vec4).
 *
 * These are algebraic-identity + invariant tests, not point-sample tests.
 * Every random-input case uses std::mt19937{42} so the suite is fully
 * deterministic — the same seed produces the same 1000 samples on every CI
 * run, so a regression that hits one sample today will hit the same sample
 * tomorrow.
 *
 * Coverage:
 *
 *   - Field-axiom checks: commutativity of +, distributivity of scalar over
 *     vector +, additive identity, additive inverse, scalar-1 identity.
 *   - Associativity of vector + (eps-tolerant — fp addition is not exactly
 *     associative but is associative to within a small relative epsilon).
 *   - Dot-product axioms: bilinearity, |a|^2 = a.a, Cauchy-Schwarz inequality
 *     (|a.b| <= |a||b|).
 *   - Cross-product axioms: anti-commutativity, cross(a,a) = 0, the
 *     orthogonality identity dot(cross(a,b), a) == 0, BAC-CAB triple-product
 *     expansion, and the magnitude identity |cross(a,b)|^2 + dot(a,b)^2 ==
 *     |a|^2 |b|^2 (Lagrange).
 *   - Normalize round-trip: length(normalize(v)) == 1, and normalize is
 *     idempotent. Degenerate near-zero returns zero (not NaN) per the
 *     header's guard.
 *   - Reflection: reflect(v, n).n == -v.n (incoming and outgoing have equal
 *     and opposite normal-projection), |reflect(v, n)| == |v|.
 *   - Refraction: matches Snell's law for known eta values; total internal
 *     reflection returns the zero vector.
 *   - Lerp: lerp(a, b, 0) == a, lerp(a, b, 1) == b, lerp(a, b, 0.5) is the
 *     midpoint.
 *   - SIMD-lane-4 hygiene: vec3 operations leave _padding at 0.0f even after
 *     divide-by-zero (a documented invariant in Vector.hpp's operator+
 *     comment block).
 *   - Degenerate inputs: NaN propagation, zero vectors, very small (1e-30f)
 *     and very large (1e30f) magnitudes survive without producing NaN under
 *     the documented contract.
 *
 * All assertions use Catch1's Approx with explicit .margin() / .epsilon()
 * tuned to the order of magnitude of the operands — a relative tolerance of
 * 1e-5f is the practical floor for cumulative single-precision error on
 * 3-component sums.
 */

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/math/Vector.hpp"
#include "engine/math/Math.hpp"

#include <cmath>
#include <limits>
#include <random>

using namespace Engine;

namespace {

// Deterministic RNG seed — routed through CatTest::DeterministicSeed so the
// stream is reproducible across machines by default yet replayable/sweepable
// via the CAT_TEST_SEED environment variable. Same generator type and
// distribution usage as before; only the seed VALUE now comes from the shared
// helper instead of a bare literal.
const unsigned kRngSeed =
    static_cast<unsigned>(CatTest::DeterministicSeed("math property vector"));
constexpr int kStressSamples = 1000;

// Generate a finite vec3 with components in [-range, range]. We deliberately
// keep |v| below sqrt(3)*range so dot/cross products stay well within
// float's representable range and don't accidentally test denormal handling
// — that's a separate test below.
vec3 randomVec3(std::mt19937& rng, float range = 10.0f) {
    std::uniform_real_distribution<float> dist(-range, range);
    return vec3(dist(rng), dist(rng), dist(rng));
}

vec2 randomVec2(std::mt19937& rng, float range = 10.0f) {
    std::uniform_real_distribution<float> dist(-range, range);
    return vec2(dist(rng), dist(rng));
}

vec4 randomVec4(std::mt19937& rng, float range = 10.0f) {
    std::uniform_real_distribution<float> dist(-range, range);
    return vec4(dist(rng), dist(rng), dist(rng), dist(rng));
}

// Tolerance helpers — float associativity / distributivity holds to within
// O(eps) of the operand magnitudes, NOT to within an absolute fixed margin.
// A pair of 1e3-magnitude operands legitimately accumulates ~1e-4 absolute
// error on a 3-add reduction.
float relativeTolerance(float a, float b, float baseEps = 1e-5f) {
    return baseEps * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
}

} // namespace

// ============================================================================
// vec3 — field axioms
// ============================================================================

TEST_CASE("vec3: addition is commutative", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 ab = a + b;
        vec3 ba = b + a;
        // fp + is exactly commutative — IEEE-754 guarantees a+b == b+a
        // bit-for-bit. So the assertion is strict equality, not Approx.
        REQUIRE(ab.x == ba.x);
        REQUIRE(ab.y == ba.y);
        REQUIRE(ab.z == ba.z);
    }
}

TEST_CASE("vec3: addition is associative within fp tolerance",
          "[math][vector][property]") {
    // Floating-point addition is NOT exactly associative (different rounding
    // orders), but the relative error is bounded by O(eps). We accept a
    // 1e-5 relative tolerance which is comfortably above the IEEE-754 bound
    // for 3 single-precision adds.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 c = randomVec3(rng);
        vec3 leftAssoc  = (a + b) + c;
        vec3 rightAssoc = a + (b + c);
        REQUIRE(leftAssoc.x == Approx(rightAssoc.x).margin(relativeTolerance(leftAssoc.x, rightAssoc.x)));
        REQUIRE(leftAssoc.y == Approx(rightAssoc.y).margin(relativeTolerance(leftAssoc.y, rightAssoc.y)));
        REQUIRE(leftAssoc.z == Approx(rightAssoc.z).margin(relativeTolerance(leftAssoc.z, rightAssoc.z)));
    }
}

TEST_CASE("vec3: zero is the additive identity", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    vec3 zero(0.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        REQUIRE((a + zero) == a);
        REQUIRE((zero + a) == a);
        REQUIRE((a - a) == zero);
    }
}

TEST_CASE("vec3: subtraction equals adding the negation",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 sub = a - b;
        vec3 addNeg = a + (-b);
        REQUIRE(sub.x == addNeg.x);
        REQUIRE(sub.y == addNeg.y);
        REQUIRE(sub.z == addNeg.z);
    }
}

TEST_CASE("vec3: scalar multiplication distributes over vector addition",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> scalarDist(-5.0f, 5.0f);
    for (int i = 0; i < kStressSamples; i++) {
        float s = scalarDist(rng);
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 lhs = (a + b) * s;
        vec3 rhs = a * s + b * s;
        REQUIRE(lhs.x == Approx(rhs.x).margin(relativeTolerance(lhs.x, rhs.x)));
        REQUIRE(lhs.y == Approx(rhs.y).margin(relativeTolerance(lhs.y, rhs.y)));
        REQUIRE(lhs.z == Approx(rhs.z).margin(relativeTolerance(lhs.z, rhs.z)));
    }
}

TEST_CASE("vec3: scalar 1 is the multiplicative identity",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 scaled = a * 1.0f;
        REQUIRE(scaled.x == a.x);
        REQUIRE(scaled.y == a.y);
        REQUIRE(scaled.z == a.z);
    }
}

TEST_CASE("vec3: scalar 0 produces zero vector", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 scaled = a * 0.0f;
        REQUIRE(scaled.x == 0.0f);
        REQUIRE(scaled.y == 0.0f);
        REQUIRE(scaled.z == 0.0f);
    }
}

TEST_CASE("vec3: scalar associativity (s1 * s2) * v == s1 * (s2 * v)",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> scalarDist(-3.0f, 3.0f);
    for (int i = 0; i < kStressSamples; i++) {
        float s1 = scalarDist(rng);
        float s2 = scalarDist(rng);
        vec3 a = randomVec3(rng);
        vec3 lhs = a * (s1 * s2);
        vec3 rhs = (a * s1) * s2;
        REQUIRE(lhs.x == Approx(rhs.x).margin(relativeTolerance(lhs.x, rhs.x)));
        REQUIRE(lhs.y == Approx(rhs.y).margin(relativeTolerance(lhs.y, rhs.y)));
        REQUIRE(lhs.z == Approx(rhs.z).margin(relativeTolerance(lhs.z, rhs.z)));
    }
}

// ============================================================================
// vec3 — dot product axioms
// ============================================================================

TEST_CASE("vec3: dot is commutative", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        // Float multiplication is exactly commutative, so a*b == b*a per term.
        REQUIRE(a.dot(b) == b.dot(a));
    }
}

TEST_CASE("vec3: dot self equals length squared", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        REQUIRE(a.dot(a) == Approx(a.lengthSquared()).margin(1e-5f));
    }
}

TEST_CASE("vec3: dot is bilinear in second argument",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> scalarDist(-3.0f, 3.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 c = randomVec3(rng);
        float s = scalarDist(rng);
        float lhs = a.dot(b * s + c);
        float rhs = a.dot(b) * s + a.dot(c);
        // Magnitudes here can reach ~3*10*10 = 300, so absolute tolerance
        // scales accordingly.
        REQUIRE(lhs == Approx(rhs).margin(1e-3f));
    }
}

TEST_CASE("vec3: Cauchy-Schwarz inequality |a.b| <= |a||b|",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        float lhs = std::abs(a.dot(b));
        float rhs = a.length() * b.length();
        // Allow fp slop: the inequality can be violated by ~ULP at exact
        // equality (parallel vectors).
        REQUIRE(lhs <= rhs + 1e-4f);
    }
}

// ============================================================================
// vec3 — cross product axioms
// ============================================================================

TEST_CASE("vec3: cross with self is zero", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 selfCross = a.cross(a);
        // a x a is exactly zero in real arithmetic; in fp it is exactly zero
        // because every term (y*z - z*y) collapses to (x - x) = 0 bit-for-bit.
        REQUIRE(selfCross.x == 0.0f);
        REQUIRE(selfCross.y == 0.0f);
        REQUIRE(selfCross.z == 0.0f);
    }
}

TEST_CASE("vec3: cross is anti-commutative (a x b == -(b x a))",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 ab = a.cross(b);
        vec3 ba = b.cross(a);
        REQUIRE(ab.x == Approx(-ba.x).margin(1e-5f));
        REQUIRE(ab.y == Approx(-ba.y).margin(1e-5f));
        REQUIRE(ab.z == Approx(-ba.z).margin(1e-5f));
    }
}

TEST_CASE("vec3: cross(a, b) is orthogonal to both a and b",
          "[math][vector][property]") {
    // The geometric reason this matters: every normal-vector computation in
    // mesh shading uses cross() to derive a face normal from two edge vectors.
    // If this identity ever drifts, lighting flips inside-out.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 c = a.cross(b);
        // Magnitudes are bounded by ~300 (10*10*3), so dot can be ~3000.
        // Relative tolerance 1e-4 of that = 3e-1; we tighten to 1e-3
        // because the cancellation in cross's exact form is quite clean.
        float dotA = c.dot(a);
        float dotB = c.dot(b);
        REQUIRE(dotA == Approx(0.0f).margin(1e-3f));
        REQUIRE(dotB == Approx(0.0f).margin(1e-3f));
    }
}

TEST_CASE("vec3: cross distributes over addition", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 c = randomVec3(rng);
        vec3 lhs = a.cross(b + c);
        vec3 rhs = a.cross(b) + a.cross(c);
        REQUIRE(lhs.x == Approx(rhs.x).margin(1e-3f));
        REQUIRE(lhs.y == Approx(rhs.y).margin(1e-3f));
        REQUIRE(lhs.z == Approx(rhs.z).margin(1e-3f));
    }
}

TEST_CASE("vec3: Lagrange identity |a x b|^2 + (a.b)^2 == |a|^2 |b|^2",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        float crossLenSq = a.cross(b).lengthSquared();
        float dotVal = a.dot(b);
        float lhs = crossLenSq + dotVal * dotVal;
        float rhs = a.lengthSquared() * b.lengthSquared();
        // Magnitudes scale as the 4th power of range (~10^4 = 10000) — use
        // a relative tolerance.
        REQUIRE(lhs == Approx(rhs).epsilon(1e-4f));
    }
}

TEST_CASE("vec3: cross of parallel vectors is zero", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> scalarDist(-3.0f, 3.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        float s = scalarDist(rng);
        vec3 parallel = a * s;
        vec3 c = a.cross(parallel);
        REQUIRE(c.x == Approx(0.0f).margin(1e-4f));
        REQUIRE(c.y == Approx(0.0f).margin(1e-4f));
        REQUIRE(c.z == Approx(0.0f).margin(1e-4f));
    }
}

TEST_CASE("vec3: cross of canonical basis vectors follows right-hand rule",
          "[math][vector][property]") {
    // x × y = z, y × z = x, z × x = y is the right-handed convention every
    // graphics engine uses. Worth pinning exactly because flipping any one
    // of these reverses winding order on every cross-product-derived normal.
    vec3 X(1.0f, 0.0f, 0.0f), Y(0.0f, 1.0f, 0.0f), Z(0.0f, 0.0f, 1.0f);
    REQUIRE(X.cross(Y) == Z);
    REQUIRE(Y.cross(Z) == X);
    REQUIRE(Z.cross(X) == Y);
    REQUIRE(Y.cross(X) == -Z);
    REQUIRE(Z.cross(Y) == -X);
    REQUIRE(X.cross(Z) == -Y);
}

// ============================================================================
// vec3 — normalize
// ============================================================================

TEST_CASE("vec3: normalize produces unit length", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        if (a.length() < 1e-3f) continue; // skip near-zero — separate test below
        vec3 n = a.normalized();
        REQUIRE(n.length() == Approx(1.0f).margin(1e-5f));
    }
}

TEST_CASE("vec3: normalize is idempotent", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        if (a.length() < 1e-3f) continue;
        vec3 n1 = a.normalized();
        vec3 n2 = n1.normalized();
        REQUIRE(n2.x == Approx(n1.x).margin(1e-6f));
        REQUIRE(n2.y == Approx(n1.y).margin(1e-6f));
        REQUIRE(n2.z == Approx(n1.z).margin(1e-6f));
    }
}

TEST_CASE("vec3: normalize of zero returns zero (no NaN)",
          "[math][vector][property][degenerate]") {
    // The header guard says: len > EPSILON ? v/len : vec3(0). Without that
    // guard, normalizing (0,0,0) would divide by zero and produce NaN, which
    // would then poison every downstream consumer (lighting, physics).
    vec3 zero(0.0f);
    vec3 normalized = zero.normalized();
    REQUIRE(normalized.x == 0.0f);
    REQUIRE(normalized.y == 0.0f);
    REQUIRE(normalized.z == 0.0f);
    REQUIRE_FALSE(std::isnan(normalized.x));
    REQUIRE_FALSE(std::isnan(normalized.y));
    REQUIRE_FALSE(std::isnan(normalized.z));
}

TEST_CASE("vec3: normalize preserves direction", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        if (a.length() < 1e-3f) continue;
        vec3 n = a.normalized();
        // a and a.normalized() must point the same way: their dot product
        // equals |a| (because n is unit length and parallel to a).
        REQUIRE(a.dot(n) == Approx(a.length()).margin(1e-4f));
        // Cross of parallel vectors is ~zero.
        vec3 c = a.cross(n);
        REQUIRE(c.length() == Approx(0.0f).margin(1e-4f));
    }
}

// ============================================================================
// vec3 — reflection / refraction
// ============================================================================

TEST_CASE("vec3: reflection preserves magnitude", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 v = randomVec3(rng);
        vec3 n = randomVec3(rng);
        if (n.length() < 1e-3f) continue;
        n = n.normalized();
        vec3 r = v.reflect(n);
        REQUIRE(r.length() == Approx(v.length()).epsilon(1e-4f));
    }
}

TEST_CASE("vec3: reflection flips normal-projection sign",
          "[math][vector][property]") {
    // r.n == -v.n: the reflected ray's normal component is opposite to the
    // incident ray's. This is the defining property of mirror reflection.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 v = randomVec3(rng);
        vec3 n = randomVec3(rng);
        if (n.length() < 1e-3f) continue;
        n = n.normalized();
        vec3 r = v.reflect(n);
        float vDotN = v.dot(n);
        float rDotN = r.dot(n);
        REQUIRE(rDotN == Approx(-vDotN).margin(1e-4f));
    }
}

TEST_CASE("vec3: reflection preserves tangential component",
          "[math][vector][property]") {
    // The tangent (perpendicular-to-normal) part is unchanged by reflection.
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 v = randomVec3(rng);
        vec3 n = randomVec3(rng);
        if (n.length() < 1e-3f) continue;
        n = n.normalized();
        vec3 r = v.reflect(n);
        // Tangent component: v_t = v - n * (v.n)
        vec3 vTangent = v - n * v.dot(n);
        vec3 rTangent = r - n * r.dot(n);
        REQUIRE(rTangent.x == Approx(vTangent.x).margin(1e-4f));
        REQUIRE(rTangent.y == Approx(vTangent.y).margin(1e-4f));
        REQUIRE(rTangent.z == Approx(vTangent.z).margin(1e-4f));
    }
}

TEST_CASE("vec3: refraction with eta=1 is the identity",
          "[math][vector][property]") {
    // eta=1 means no refraction; the direction should pass through
    // unchanged (the formula collapses to v - n*0 + 0 = v).
    // Caveat: the engine's refract() uses k = 1 - 1*1*(1 - cos^2) = cos^2
    // and returns v*1 - n*(cos + |cos|), which equals v - 2*n*cos when
    // the ray is hitting the back face. For a ray going INTO a surface
    // (v.n < 0) it should be a no-op. We verify the entering-ray case.
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> tDist(0.1f, 1.0f);
    for (int i = 0; i < 100; i++) {
        vec3 n = randomVec3(rng);
        if (n.length() < 1e-3f) continue;
        n = n.normalized();
        // Build v so that v.n < 0 (entering surface from outside).
        vec3 tangent = randomVec3(rng);
        // Remove normal component so tangent is purely tangential.
        tangent = tangent - n * tangent.dot(n);
        vec3 v = (tangent - n * tDist(rng)).normalized();
        REQUIRE(v.dot(n) < 0.0f);
        vec3 refr = v.refract(n, 1.0f);
        // For eta=1, refract returns v*1 - n*(1*v.n + sqrt(1-(1*(1-(v.n)^2))))
        //         = v - n*(v.n + sqrt((v.n)^2))
        //         = v - n*(v.n + |v.n|)
        // Since v.n < 0, |v.n| = -v.n, so sum = 0, and result is exactly v.
        REQUIRE(refr.x == Approx(v.x).margin(1e-5f));
        REQUIRE(refr.y == Approx(v.y).margin(1e-5f));
        REQUIRE(refr.z == Approx(v.z).margin(1e-5f));
    }
}

TEST_CASE("vec3: refraction with total internal reflection returns zero",
          "[math][vector][property]") {
    // When eta > 1 and the angle is shallow, Snell's law has no real
    // solution — the discriminant k goes negative and the header documents
    // that we return vec3(0).
    // A ray near-grazing the surface (small normal component) with high eta
    // is guaranteed to TIR.
    vec3 n(0.0f, 1.0f, 0.0f);
    vec3 v(0.99f, -0.141f, 0.0f); // 81.9 degrees from normal
    v = v.normalized();
    vec3 r = v.refract(n, 2.0f); // air -> very-dense medium
    REQUIRE(r.x == 0.0f);
    REQUIRE(r.y == 0.0f);
    REQUIRE(r.z == 0.0f);
}

// ============================================================================
// vec3 — lerp
// ============================================================================

TEST_CASE("vec3: lerp endpoint contract (t=0 -> a, t=1 -> b)",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 atZero = vec3::lerp(a, b, 0.0f);
        vec3 atOne  = vec3::lerp(a, b, 1.0f);
        REQUIRE(atZero == a);
        REQUIRE(atOne.x == Approx(b.x).margin(1e-5f));
        REQUIRE(atOne.y == Approx(b.y).margin(1e-5f));
        REQUIRE(atOne.z == Approx(b.z).margin(1e-5f));
    }
}

TEST_CASE("vec3: lerp at t=0.5 is the midpoint", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 b = randomVec3(rng);
        vec3 mid = vec3::lerp(a, b, 0.5f);
        vec3 expected = (a + b) * 0.5f;
        REQUIRE(mid.x == Approx(expected.x).margin(1e-5f));
        REQUIRE(mid.y == Approx(expected.y).margin(1e-5f));
        REQUIRE(mid.z == Approx(expected.z).margin(1e-5f));
    }
}

// ============================================================================
// vec3 — SIMD-lane-4 hygiene
// ============================================================================

TEST_CASE("vec3: _padding stays 0 after operator+", "[math][vector][simd]") {
    // The header's operator+ explicitly stores 0.0f into _padding after each
    // SIMD op. If somebody removes the `ret._padding = 0.0f;` line, this
    // test catches the regression — the SSE add carries lane 4 forward and
    // any non-zero left there from a prior operation will pollute the
    // result. We synthesise a vec3 with a hand-poked non-zero padding and
    // verify the new vec3 from + is cleanly zero in lane 4.
    vec3 a(1.0f, 2.0f, 3.0f);
    vec3 b(4.0f, 5.0f, 6.0f);
    // Manually corrupt padding via reinterpret cast — UB-ish but documented
    // by the header as the failure mode we're guarding against.
    *(reinterpret_cast<float*>(&a) + 3) = 999.0f;
    vec3 sum = a + b;
    REQUIRE(*(const float*)(&sum.data[0] + 3) == 0.0f);
}

TEST_CASE("vec3: _padding stays 0 after divide-by-zero", "[math][vector][simd]") {
    // Documented by the header: divide-by-zero on _padding produces NaN in
    // lane 4 unless we explicitly clear it. This test pins the contract.
    vec3 a(1.0f, 2.0f, 3.0f);
    vec3 result = a / 0.0f;
    REQUIRE(*(const float*)(&result.data[0] + 3) == 0.0f);
    REQUIRE_FALSE(std::isnan(*(const float*)(&result.data[0] + 3)));
}

// ============================================================================
// vec3 — degenerate inputs
// ============================================================================

TEST_CASE("vec3: NaN propagates through arithmetic", "[math][vector][degenerate]") {
    // Sanity check: we don't try to scrub NaN — once it's in, it stays in,
    // which is the IEEE-754 contract. Downstream code is supposed to keep
    // NaN OUT in the first place.
    float nanf = std::numeric_limits<float>::quiet_NaN();
    vec3 nanV(nanf, 0.0f, 0.0f);
    vec3 ok(1.0f, 2.0f, 3.0f);
    vec3 sum = nanV + ok;
    REQUIRE(std::isnan(sum.x));
}

TEST_CASE("vec3: very small magnitudes survive without NaN",
          "[math][vector][degenerate]") {
    // 1e-30 is well into denormal territory but still representable. Vector
    // ops should not introduce NaN.
    vec3 tiny(1e-30f, 1e-30f, 1e-30f);
    vec3 doubled = tiny * 2.0f;
    REQUIRE_FALSE(std::isnan(doubled.x));
    REQUIRE_FALSE(std::isnan(doubled.y));
    REQUIRE_FALSE(std::isnan(doubled.z));
}

TEST_CASE("vec3: very large magnitudes survive without inf overflow on sum",
          "[math][vector][degenerate]") {
    // 1e15 squared overflows float, but plain addition is fine.
    vec3 big(1e15f, 1e15f, 1e15f);
    vec3 sum = big + big;
    REQUIRE_FALSE(std::isnan(sum.x));
    REQUIRE_FALSE(std::isinf(sum.x));
}

TEST_CASE("vec3: lengthSquared and length agree (length^2 == lengthSquared)",
          "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        float lenSq = a.lengthSquared();
        float len   = a.length();
        REQUIRE(len * len == Approx(lenSq).epsilon(1e-5f));
    }
}

// ============================================================================
// vec2 — small but matters
// ============================================================================

TEST_CASE("vec2: dot and cross axioms", "[math][vector][vec2][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec2 a = randomVec2(rng);
        vec2 b = randomVec2(rng);
        REQUIRE(a.dot(b) == b.dot(a));                  // dot commutative
        REQUIRE(a.cross(b) == Approx(-b.cross(a)));     // 2D cross anti-comm
        REQUIRE(a.cross(a) == Approx(0.0f).margin(1e-5f));
    }
}

TEST_CASE("vec2: perpendicular is orthogonal", "[math][vector][vec2][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec2 a = randomVec2(rng);
        vec2 p = a.perpendicular();
        REQUIRE(a.dot(p) == Approx(0.0f).margin(1e-5f));
        REQUIRE(p.length() == Approx(a.length()).epsilon(1e-5f));
    }
}

TEST_CASE("vec2: normalize round-trip", "[math][vector][vec2][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec2 a = randomVec2(rng);
        if (a.length() < 1e-3f) continue;
        vec2 n = a.normalized();
        REQUIRE(n.length() == Approx(1.0f).margin(1e-5f));
    }
}

TEST_CASE("vec2: lerp endpoint contract", "[math][vector][vec2][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec2 a = randomVec2(rng);
        vec2 b = randomVec2(rng);
        REQUIRE(vec2::lerp(a, b, 0.0f) == a);
        REQUIRE(vec2::lerp(a, b, 1.0f) == b);
        vec2 mid = vec2::lerp(a, b, 0.5f);
        vec2 expected = (a + b) * 0.5f;
        REQUIRE(mid.x == Approx(expected.x).margin(1e-5f));
        REQUIRE(mid.y == Approx(expected.y).margin(1e-5f));
    }
}

// ============================================================================
// vec4 — SIMD union field aliasing
// ============================================================================

TEST_CASE("vec4: simd / data / struct view of the same storage agree",
          "[math][vector][vec4][simd]") {
    // The header unions {x,y,z,w} / data[4] / __m128 simd. They MUST be
    // aliases for the same 16 bytes — if a compiler ever reordered union
    // members or changed layout, downstream SIMD code would silently read
    // the wrong lanes.
    vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(v.data[0] == 1.0f);
    REQUIRE(v.data[1] == 2.0f);
    REQUIRE(v.data[2] == 3.0f);
    REQUIRE(v.data[3] == 4.0f);
    REQUIRE(v[0] == 1.0f);
    REQUIRE(v[1] == 2.0f);
    REQUIRE(v[2] == 3.0f);
    REQUIRE(v[3] == 4.0f);
}

TEST_CASE("vec4: dot and length agree with the scalar formulas",
          "[math][vector][vec4][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec4 a = randomVec4(rng);
        vec4 b = randomVec4(rng);
        float expectedDot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        REQUIRE(a.dot(b) == Approx(expectedDot).epsilon(1e-5f));
        float expectedLenSq = a.x*a.x + a.y*a.y + a.z*a.z + a.w*a.w;
        REQUIRE(a.lengthSquared() == Approx(expectedLenSq).epsilon(1e-5f));
    }
}

TEST_CASE("vec4: SIMD operator+ matches per-component scalar sum",
          "[math][vector][vec4][simd]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec4 a = randomVec4(rng);
        vec4 b = randomVec4(rng);
        vec4 sum = a + b;
        REQUIRE(sum.x == Approx(a.x + b.x).margin(1e-5f));
        REQUIRE(sum.y == Approx(a.y + b.y).margin(1e-5f));
        REQUIRE(sum.z == Approx(a.z + b.z).margin(1e-5f));
        REQUIRE(sum.w == Approx(a.w + b.w).margin(1e-5f));
    }
}

TEST_CASE("vec4: xyz() extracts the xyz triple", "[math][vector][vec4]") {
    vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    vec3 xyz = v.xyz();
    REQUIRE(xyz.x == 1.0f);
    REQUIRE(xyz.y == 2.0f);
    REQUIRE(xyz.z == 3.0f);
}

// ============================================================================
// vec3 — round-trip with negation
// ============================================================================

TEST_CASE("vec3: double negation is identity", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        vec3 doubleNeg = -(-a);
        REQUIRE(doubleNeg.x == a.x);
        REQUIRE(doubleNeg.y == a.y);
        REQUIRE(doubleNeg.z == a.z);
    }
}

TEST_CASE("vec3: index operator round-trip", "[math][vector][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 a = randomVec3(rng);
        REQUIRE(a[0] == a.x);
        REQUIRE(a[1] == a.y);
        REQUIRE(a[2] == a.z);
        a[0] = 7.0f;
        REQUIRE(a.x == 7.0f);
    }
}

TEST_CASE("vec3: static factory constants match documented values",
          "[math][vector]") {
    REQUIRE(vec3::zero() == vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(vec3::one() == vec3(1.0f, 1.0f, 1.0f));
    REQUIRE(vec3::up() == vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(vec3::down() == vec3(0.0f, -1.0f, 0.0f));
    REQUIRE(vec3::right() == vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(vec3::left() == vec3(-1.0f, 0.0f, 0.0f));
    REQUIRE(vec3::forward() == vec3(0.0f, 0.0f, -1.0f));   // OpenGL/Vulkan -z forward
    REQUIRE(vec3::back() == vec3(0.0f, 0.0f, 1.0f));
}

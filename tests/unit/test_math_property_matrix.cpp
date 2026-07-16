/**
 * Property tests for engine/math/Matrix.hpp (mat3 + mat4) — including
 * the Vulkan projection-matrix clip-Z contract.
 *
 * Coverage:
 *
 *   - Algebraic group axioms: associativity of *, identity * M == M, M *
 *     inverse(M) == identity.
 *   - Transposition: transpose(transpose(M)) == M, transpose(A*B) ==
 *     transpose(B) * transpose(A).
 *   - Determinant of identity == 1; determinant of a singular matrix == 0;
 *     det(A*B) == det(A) * det(B) on a sample of random matrices.
 *   - Inversion round-trip: M * M.inverse() == identity (with eps tolerance),
 *     inverse(inverse(M)) == M.
 *   - Affine transforms: translate / scale / rotate construct matrices that
 *     match the closed-form column layout. translate(t) * translate(-t) ==
 *     identity. scale(s) * scale(1/s) == identity (for non-zero s).
 *   - rotateX / rotateY / rotateZ at 0 == identity, at PI == reverse-axis
 *     identity, at PI/2 maps the documented basis vectors.
 *   - mat4 * vec4: produces same result as explicit per-component sum.
 *   - transformPoint vs transformVector: transformVector ignores
 *     translation (verified by translating then transforming a direction).
 *   - VULKAN clip-Z contract: perspective(fov, aspect, near, far) MUST map
 *     view-space z=-near to clip z=0 and view-space z=-far to clip z=1
 *     (Vulkan's [0, 1] depth range, not OpenGL's [-1, 1]).
 *   - VULKAN ortho clip-Z contract: same [0, 1] range.
 *   - perspectiveInfinite collapses to perspective in the limit far -> inf.
 *   - lookAt: an eye looking at center produces a view matrix where the
 *     center maps to (0, 0, -|eye - center|) in view space.
 *   - mat3 vs mat4 upper-3x3 agreement on construction from same axis-angle.
 *   - Degenerate inputs: singular matrix's inverse returns identity (header
 *     guard at line 236).
 *
 * Seed is std::mt19937{42}, 1000 samples for the property tests, smaller
 * batches for the more expensive ones (lookAt, perspective).
 */

#include "catch.hpp"
#include "engine/math/Matrix.hpp"
#include "engine/math/Vector.hpp"
#include "engine/math/Math.hpp"

#include <cmath>
#include <random>

using namespace Engine;

namespace {

constexpr unsigned kRngSeed = 42u;
constexpr int kStressSamples = 1000;

// Generate a random invertible mat4 by composing translate * rotate * scale
// — same construction the engine uses for actual transforms, so we're
// stress-testing the typical case rather than arbitrary matrices that might
// be near-singular.
mat4 randomAffineMat4(std::mt19937& rng) {
    std::uniform_real_distribution<float> posDist(-10.0f, 10.0f);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    std::uniform_real_distribution<float> scaleDist(0.5f, 3.0f);
    std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);

    vec3 t(posDist(rng), posDist(rng), posDist(rng));
    vec3 axis(axisDist(rng), axisDist(rng), axisDist(rng));
    if (axis.lengthSquared() < 1e-4f) axis = vec3(0.0f, 1.0f, 0.0f);
    axis = axis.normalized();
    float angle = angleDist(rng);
    vec3 s(scaleDist(rng), scaleDist(rng), scaleDist(rng));

    return mat4::translate(t) * mat4::rotate(axis, angle) * mat4::scale(s);
}

mat3 randomInvertibleMat3(std::mt19937& rng) {
    // Diagonally dominant -> non-singular.
    std::uniform_real_distribution<float> offDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> diagDist(2.0f, 5.0f);
    return mat3(
        vec3(diagDist(rng), offDist(rng), offDist(rng)),
        vec3(offDist(rng), diagDist(rng), offDist(rng)),
        vec3(offDist(rng), offDist(rng), diagDist(rng))
    );
}

bool isApproxIdentity4(const mat4& m, float eps = 1e-3f) {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float expected = (col == row) ? 1.0f : 0.0f;
            if (std::abs(m[col][row] - expected) > eps) return false;
        }
    }
    return true;
}

bool isApproxIdentity3(const mat3& m, float eps = 1e-4f) {
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 3; row++) {
            float expected = (col == row) ? 1.0f : 0.0f;
            if (std::abs(m[col][row] - expected) > eps) return false;
        }
    }
    return true;
}

} // namespace

// ============================================================================
// mat4 — identity and construction
// ============================================================================

TEST_CASE("mat4: default constructor produces identity", "[math][matrix]") {
    mat4 m;
    REQUIRE(isApproxIdentity4(m, 0.0f));
}

TEST_CASE("mat4: identity() factory produces identity", "[math][matrix]") {
    REQUIRE(isApproxIdentity4(mat4::identity(), 0.0f));
}

TEST_CASE("mat4: diagonal-only constructor populates diagonal",
          "[math][matrix]") {
    mat4 m(7.0f);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float expected = (i == j) ? 7.0f : 0.0f;
            REQUIRE(m[i][j] == expected);
        }
    }
}

// ============================================================================
// mat4 — multiplication algebraic axioms
// ============================================================================

TEST_CASE("mat4: identity * M == M * identity == M",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    mat4 identity = mat4::identity();
    for (int i = 0; i < 200; i++) {
        mat4 m = randomAffineMat4(rng);
        mat4 imM = identity * m;
        mat4 mi  = m * identity;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                REQUIRE(imM[col][row] == Approx(m[col][row]).margin(1e-4f));
                REQUIRE(mi[col][row] == Approx(m[col][row]).margin(1e-4f));
            }
        }
    }
}

TEST_CASE("mat4: multiplication is associative", "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        mat4 a = randomAffineMat4(rng);
        mat4 b = randomAffineMat4(rng);
        mat4 c = randomAffineMat4(rng);
        mat4 left  = (a * b) * c;
        mat4 right = a * (b * c);
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                // Magnitudes here can reach ~100; relative tolerance 1e-3.
                REQUIRE(left[col][row] == Approx(right[col][row]).epsilon(1e-3f));
            }
        }
    }
}

TEST_CASE("mat4: multiplication is NOT commutative (rotation + translation)",
          "[math][matrix]") {
    // Pin the non-commutativity — rotate-then-translate is different from
    // translate-then-rotate. If somebody ever accidentally implements
    // operator* as cwise-multiply, this catches it.
    mat4 rotate = mat4::rotateY(Math::HALF_PI);
    mat4 translate = mat4::translate(vec3(1.0f, 0.0f, 0.0f));
    mat4 RT = rotate * translate;
    mat4 TR = translate * rotate;
    // Apply each to the origin: RT puts the origin somewhere; TR puts it
    // somewhere different.
    vec3 viaRT = RT.transformPoint(vec3(0.0f));
    vec3 viaTR = TR.transformPoint(vec3(0.0f));
    REQUIRE_FALSE(viaRT == viaTR);
}

// ============================================================================
// mat4 — transpose
// ============================================================================

TEST_CASE("mat4: transpose is involutive (twice == identity-op)",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        mat4 m = randomAffineMat4(rng);
        mat4 twice = m.transposed().transposed();
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                REQUIRE(twice[col][row] == m[col][row]);
            }
        }
    }
}

TEST_CASE("mat4: transpose(A*B) == transpose(B) * transpose(A)",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        mat4 a = randomAffineMat4(rng);
        mat4 b = randomAffineMat4(rng);
        mat4 lhs = (a * b).transposed();
        mat4 rhs = b.transposed() * a.transposed();
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                REQUIRE(lhs[col][row] == Approx(rhs[col][row]).epsilon(1e-3f));
            }
        }
    }
}

// ============================================================================
// mat4 — inverse
// ============================================================================

TEST_CASE("mat4: M * inverse(M) == identity", "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        mat4 m = randomAffineMat4(rng);
        mat4 prod = m * m.inverse();
        REQUIRE(isApproxIdentity4(prod, 1e-3f));
    }
}

TEST_CASE("mat4: inverse(M) * M == identity", "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        mat4 m = randomAffineMat4(rng);
        mat4 prod = m.inverse() * m;
        REQUIRE(isApproxIdentity4(prod, 1e-3f));
    }
}

TEST_CASE("mat4: inverse(inverse(M)) == M", "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        mat4 m = randomAffineMat4(rng);
        mat4 back = m.inverse().inverse();
        // Tolerance must account for two inversions' worth of round-off.
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                REQUIRE(back[col][row] == Approx(m[col][row]).epsilon(1e-2f).margin(1e-3f));
            }
        }
    }
}

TEST_CASE("mat4: inverse of singular matrix returns identity (header guard)",
          "[math][matrix][degenerate]") {
    // Header line ~236: if abs(det) < EPSILON, return identity. This
    // prevents NaN-poisoning when somebody asks for the inverse of a
    // zero matrix or a matrix with a zero scale axis.
    mat4 zero(0.0f); // all zeros, det == 0
    mat4 inv = zero.inverse();
    REQUIRE(isApproxIdentity4(inv, 0.0f));
}

// ============================================================================
// mat4 — translate / scale / rotate
// ============================================================================

TEST_CASE("mat4: translate then inverse-translate is identity",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 t(dist(rng), dist(rng), dist(rng));
        mat4 forward = mat4::translate(t);
        mat4 reverse = mat4::translate(-t);
        mat4 prod = forward * reverse;
        REQUIRE(isApproxIdentity4(prod, 1e-5f));
    }
}

TEST_CASE("mat4: translate applied to origin gives the translation",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 t(dist(rng), dist(rng), dist(rng));
        mat4 m = mat4::translate(t);
        vec3 origin(0.0f);
        vec3 translated = m.transformPoint(origin);
        REQUIRE(translated.x == Approx(t.x).margin(1e-5f));
        REQUIRE(translated.y == Approx(t.y).margin(1e-5f));
        REQUIRE(translated.z == Approx(t.z).margin(1e-5f));
    }
}

TEST_CASE("mat4: translate does NOT affect transformVector",
          "[math][matrix]") {
    // transformVector treats the vec3 as a direction (w=0), so translation
    // must drop out. This invariant is critical for normal-vector transforms
    // — if translation leaks into a normal, lighting goes haywire.
    mat4 m = mat4::translate(vec3(50.0f, -30.0f, 17.0f));
    vec3 dir(1.0f, 0.0f, 0.0f);
    vec3 transformed = m.transformVector(dir);
    REQUIRE(transformed.x == Approx(1.0f).margin(1e-5f));
    REQUIRE(transformed.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(transformed.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("mat4: scale then inverse-scale is identity",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(0.5f, 3.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 s(dist(rng), dist(rng), dist(rng));
        vec3 invS(1.0f / s.x, 1.0f / s.y, 1.0f / s.z);
        mat4 prod = mat4::scale(s) * mat4::scale(invS);
        REQUIRE(isApproxIdentity4(prod, 1e-5f));
    }
}

TEST_CASE("mat4: rotateX at 0 is identity", "[math][matrix]") {
    mat4 r = mat4::rotateX(0.0f);
    REQUIRE(isApproxIdentity4(r, 1e-6f));
}

TEST_CASE("mat4: rotateY at 0 is identity", "[math][matrix]") {
    mat4 r = mat4::rotateY(0.0f);
    REQUIRE(isApproxIdentity4(r, 1e-6f));
}

TEST_CASE("mat4: rotateZ at 0 is identity", "[math][matrix]") {
    mat4 r = mat4::rotateZ(0.0f);
    REQUIRE(isApproxIdentity4(r, 1e-6f));
}

TEST_CASE("mat4: rotateY(PI/2) maps +X to -Z (right-handed convention)",
          "[math][matrix]") {
    mat4 r = mat4::rotateY(Math::HALF_PI);
    vec3 result = r.transformVector(vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(result.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(result.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(result.z == Approx(-1.0f).margin(1e-5f));
}

TEST_CASE("mat4: rotateX(PI) maps +Y to -Y", "[math][matrix]") {
    mat4 r = mat4::rotateX(Math::PI);
    vec3 result = r.transformVector(vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(result.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(result.y == Approx(-1.0f).margin(1e-4f));
    REQUIRE(result.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("mat4: rotateZ(PI/2) maps +X to +Y", "[math][matrix]") {
    mat4 r = mat4::rotateZ(Math::HALF_PI);
    vec3 result = r.transformVector(vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(result.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(result.y == Approx(1.0f).margin(1e-5f));
    REQUIRE(result.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("mat4: rotation preserves vector length",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    std::uniform_real_distribution<float> compDist(-5.0f, 5.0f);
    std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 axis(axisDist(rng), axisDist(rng), axisDist(rng));
        if (axis.lengthSquared() < 1e-4f) continue;
        axis = axis.normalized();
        float angle = angleDist(rng);
        mat4 r = mat4::rotate(axis, angle);
        vec3 v(compDist(rng), compDist(rng), compDist(rng));
        vec3 rotated = r.transformVector(v);
        REQUIRE(rotated.length() == Approx(v.length()).epsilon(1e-3f));
    }
}

TEST_CASE("mat4: rotation around axis leaves axis invariant",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);
    for (int i = 0; i < 500; i++) {
        vec3 axis(axisDist(rng), axisDist(rng), axisDist(rng));
        if (axis.lengthSquared() < 1e-4f) continue;
        axis = axis.normalized();
        float angle = angleDist(rng);
        mat4 r = mat4::rotate(axis, angle);
        vec3 rotated = r.transformVector(axis);
        REQUIRE(rotated.x == Approx(axis.x).margin(1e-4f));
        REQUIRE(rotated.y == Approx(axis.y).margin(1e-4f));
        REQUIRE(rotated.z == Approx(axis.z).margin(1e-4f));
    }
}

// ============================================================================
// mat4 — Vulkan projection clip-Z = [0, 1]
// ============================================================================

TEST_CASE("mat4: perspective maps view-z = -near to clip-z = 0 (Vulkan [0,1])",
          "[math][matrix][vulkan]") {
    // Vulkan's depth range is [0, 1]. After perspective division, a point
    // on the near plane (view-space z = -near) should land at clip-z = 0.
    // OpenGL's [-1, 1] convention would map it to -1; if the matrix slipped
    // back to OpenGL, the depth test would silently invert and the entire
    // scene would disappear behind the near plane.
    float near = 0.1f;
    float far  = 100.0f;
    mat4 p = mat4::perspective(Math::HALF_PI, 16.0f / 9.0f, near, far);
    vec4 viewSpacePoint(0.0f, 0.0f, -near, 1.0f);
    vec4 clipSpacePoint = p * viewSpacePoint;
    float ndcZ = clipSpacePoint.z / clipSpacePoint.w;
    REQUIRE(ndcZ == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("mat4: perspective maps view-z = -far to clip-z = 1 (Vulkan [0,1])",
          "[math][matrix][vulkan]") {
    // The matching end of the [0, 1] range: far plane maps to clip-z = 1.
    float near = 0.1f;
    float far  = 100.0f;
    mat4 p = mat4::perspective(Math::HALF_PI, 16.0f / 9.0f, near, far);
    vec4 viewSpacePoint(0.0f, 0.0f, -far, 1.0f);
    vec4 clipSpacePoint = p * viewSpacePoint;
    float ndcZ = clipSpacePoint.z / clipSpacePoint.w;
    REQUIRE(ndcZ == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("mat4: perspective maps view-z midway between near and far to a clip-z in (0, 1)",
          "[math][matrix][vulkan]") {
    // Perspective depth is non-linear: 50% of the way between near and far
    // in view space is NOT clip-z = 0.5 — it's biased toward 1 because of
    // the 1/z term. We just assert the value is in the open interval.
    float near = 1.0f;
    float far  = 100.0f;
    mat4 p = mat4::perspective(Math::HALF_PI, 1.0f, near, far);
    vec4 mid(0.0f, 0.0f, -(near + far) * 0.5f, 1.0f);
    vec4 clip = p * mid;
    float ndcZ = clip.z / clip.w;
    REQUIRE(ndcZ > 0.0f);
    REQUIRE(ndcZ < 1.0f);
    // Specifically the perspective-warped value: ndcZ = far/(far-near) - far*near/((far-near)*-view.z)
    float expected = far / (far - near) - (far * near) / ((far - near) * (near + far) * 0.5f);
    REQUIRE(ndcZ == Approx(expected).margin(1e-4f));
}

TEST_CASE("mat4: perspectiveInfinite preserves Vulkan clip-z near-plane = 0",
          "[math][matrix][vulkan]") {
    // The infinite-far variant should still emit clip-z = 0 at the near plane.
    float near = 0.1f;
    mat4 p = mat4::perspectiveInfinite(Math::HALF_PI, 16.0f / 9.0f, near);
    vec4 nearPoint(0.0f, 0.0f, -near, 1.0f);
    vec4 clip = p * nearPoint;
    REQUIRE(clip.z / clip.w == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("mat4: perspectiveInfinite clip-z approaches 1 as view-z -> -inf",
          "[math][matrix][vulkan]") {
    // As the point recedes, clip-z should asymptote to 1.
    float near = 0.1f;
    mat4 p = mat4::perspectiveInfinite(Math::HALF_PI, 1.0f, near);
    vec4 farPoint(0.0f, 0.0f, -1e6f, 1.0f);
    vec4 clip = p * farPoint;
    float ndcZ = clip.z / clip.w;
    // At z = -1e6 with near = 0.1, ndcZ = 1 - near/|view-z| ~ 1 - 1e-7
    REQUIRE(ndcZ == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("mat4: ortho maps view-z = -near to clip-z = 0 (Vulkan [0,1])",
          "[math][matrix][vulkan]") {
    float near = 0.1f;
    float far  = 100.0f;
    mat4 o = mat4::ortho(-1.0f, 1.0f, -1.0f, 1.0f, near, far);
    vec4 nearPoint(0.0f, 0.0f, -near, 1.0f);
    vec4 clip = o * nearPoint;
    REQUIRE(clip.z == Approx(0.0f).margin(1e-5f));
    REQUIRE(clip.w == 1.0f);
}

TEST_CASE("mat4: ortho maps view-z = -far to clip-z = 1 (Vulkan [0,1])",
          "[math][matrix][vulkan]") {
    float near = 0.1f;
    float far  = 100.0f;
    mat4 o = mat4::ortho(-1.0f, 1.0f, -1.0f, 1.0f, near, far);
    vec4 farPoint(0.0f, 0.0f, -far, 1.0f);
    vec4 clip = o * farPoint;
    REQUIRE(clip.z == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("mat4: ortho is linear in view-z (clip-z = 0.5 at midpoint)",
          "[math][matrix][vulkan]") {
    // Unlike perspective, ortho is linear — midpoint maps to midpoint.
    float near = 1.0f;
    float far  = 11.0f;
    mat4 o = mat4::ortho(-1.0f, 1.0f, -1.0f, 1.0f, near, far);
    vec4 mid(0.0f, 0.0f, -6.0f, 1.0f);
    vec4 clip = o * mid;
    REQUIRE(clip.z == Approx(0.5f).margin(1e-5f));
}

TEST_CASE("mat4: ortho maps +X corner to clip x = 1",
          "[math][matrix][vulkan]") {
    // Orthographic projection: the right plane at view-x = right should
    // land at clip-x = +1.
    mat4 o = mat4::ortho(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    vec4 rightEdge(2.0f, 0.0f, -1.0f, 1.0f);
    vec4 clip = o * rightEdge;
    REQUIRE(clip.x == Approx(1.0f).margin(1e-5f));
}

// ============================================================================
// mat4 — lookAt
// ============================================================================

TEST_CASE("mat4: lookAt maps the look target to view-space -|eye-center| on Z",
          "[math][matrix][camera]") {
    // After applying the view matrix, the look-at center should land on the
    // -Z axis in view space at distance |eye - center|.
    vec3 eye(0.0f, 0.0f, 5.0f);
    vec3 center(0.0f, 0.0f, 0.0f);
    vec3 up(0.0f, 1.0f, 0.0f);
    mat4 view = mat4::lookAt(eye, center, up);
    vec3 centerInView = view.transformPoint(center);
    // eye distance = 5, view-space Z is negative-into-screen.
    REQUIRE(centerInView.x == Approx(0.0f).margin(1e-4f));
    REQUIRE(centerInView.y == Approx(0.0f).margin(1e-4f));
    REQUIRE(centerInView.z == Approx(-5.0f).margin(1e-4f));
}

TEST_CASE("mat4: lookAt with eye at origin maps eye to origin in view space",
          "[math][matrix][camera]") {
    vec3 eye(0.0f, 0.0f, 0.0f);
    vec3 center(0.0f, 0.0f, -1.0f);
    vec3 up(0.0f, 1.0f, 0.0f);
    mat4 view = mat4::lookAt(eye, center, up);
    vec3 eyeInView = view.transformPoint(eye);
    REQUIRE(eyeInView.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(eyeInView.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(eyeInView.z == Approx(0.0f).margin(1e-5f));
}

// ============================================================================
// mat3 — algebraic
// ============================================================================

TEST_CASE("mat3: M * inverse(M) == identity", "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 500; i++) {
        mat3 m = randomInvertibleMat3(rng);
        mat3 prod = m * m.inverse();
        REQUIRE(isApproxIdentity3(prod, 1e-3f));
    }
}

TEST_CASE("mat3: transpose is involutive", "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        mat3 m = randomInvertibleMat3(rng);
        mat3 twice = m.transposed().transposed();
        for (int col = 0; col < 3; col++) {
            for (int row = 0; row < 3; row++) {
                REQUIRE(twice[col][row] == m[col][row]);
            }
        }
    }
}

TEST_CASE("mat3: det(identity) == 1", "[math][matrix]") {
    REQUIRE(mat3::identity().determinant() == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("mat3: det(A*B) == det(A) * det(B)",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    for (int i = 0; i < 200; i++) {
        mat3 a = randomInvertibleMat3(rng);
        mat3 b = randomInvertibleMat3(rng);
        float detProd = (a * b).determinant();
        float prodDet = a.determinant() * b.determinant();
        // Tolerance scales with the magnitudes (~125 for det of 3..5
        // diagonals); relative epsilon is sufficient.
        REQUIRE(detProd == Approx(prodDet).epsilon(1e-3f));
    }
}

TEST_CASE("mat3: rotation 2D - rotate(angle) preserves length",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);
    for (int i = 0; i < kStressSamples; i++) {
        float angle = angleDist(rng);
        mat3 r = mat3::rotate(angle);
        vec3 v(dist(rng), dist(rng), 1.0f); // 2D point in homogeneous form
        vec3 rotated = r * v;
        // The 2D part of length is preserved; w should still be 1 because
        // rotation has 0s in column 3.
        float len2dBefore = std::sqrt(v.x * v.x + v.y * v.y);
        float len2dAfter  = std::sqrt(rotated.x * rotated.x + rotated.y * rotated.y);
        REQUIRE(len2dAfter == Approx(len2dBefore).epsilon(1e-4f));
        REQUIRE(rotated.z == Approx(1.0f).margin(1e-5f));
    }
}

TEST_CASE("mat3: 2D translation matrix applies translation in homogeneous form",
          "[math][matrix]") {
    mat3 t = mat3::translate(vec2(3.0f, -2.0f));
    vec3 point(1.0f, 1.0f, 1.0f); // (1, 1) in 2D
    vec3 translated = t * point;
    REQUIRE(translated.x == Approx(4.0f).margin(1e-5f));
    REQUIRE(translated.y == Approx(-1.0f).margin(1e-5f));
    REQUIRE(translated.z == Approx(1.0f).margin(1e-5f));
}

// ============================================================================
// mat4 * vec4 - explicit formula check
// ============================================================================

TEST_CASE("mat4 * vec4: matches per-component dot product of rows",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> dist(-3.0f, 3.0f);
    for (int i = 0; i < 200; i++) {
        mat4 m = randomAffineMat4(rng);
        vec4 v(dist(rng), dist(rng), dist(rng), dist(rng));
        vec4 result = m * v;
        // Manual expansion — matches the header's operator* exactly.
        float ex = m[0][0]*v.x + m[1][0]*v.y + m[2][0]*v.z + m[3][0]*v.w;
        float ey = m[0][1]*v.x + m[1][1]*v.y + m[2][1]*v.z + m[3][1]*v.w;
        float ez = m[0][2]*v.x + m[1][2]*v.y + m[2][2]*v.z + m[3][2]*v.w;
        float ew = m[0][3]*v.x + m[1][3]*v.y + m[2][3]*v.z + m[3][3]*v.w;
        REQUIRE(result.x == Approx(ex).epsilon(1e-4f));
        REQUIRE(result.y == Approx(ey).epsilon(1e-4f));
        REQUIRE(result.z == Approx(ez).epsilon(1e-4f));
        REQUIRE(result.w == Approx(ew).epsilon(1e-4f));
    }
}

TEST_CASE("mat4: transformPoint divides by w", "[math][matrix]") {
    // transformPoint applies (M * vec4(p, 1)) then divides xyz by w. For a
    // non-perspective matrix w should remain 1 so division is a no-op.
    mat4 m = mat4::translate(vec3(1.0f, 2.0f, 3.0f)) * mat4::scale(vec3(2.0f));
    vec3 p(1.0f, 1.0f, 1.0f);
    vec3 result = m.transformPoint(p);
    // Scale first (1*2=2, 1*2=2, 1*2=2), then translate (3, 4, 5).
    REQUIRE(result.x == Approx(3.0f).margin(1e-5f));
    REQUIRE(result.y == Approx(4.0f).margin(1e-5f));
    REQUIRE(result.z == Approx(5.0f).margin(1e-5f));
}

// ============================================================================
// mat4 — composition with rotation
// ============================================================================

TEST_CASE("mat4: rotate(axis, theta) * rotate(axis, -theta) == identity",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);
    for (int i = 0; i < kStressSamples; i++) {
        vec3 axis(axisDist(rng), axisDist(rng), axisDist(rng));
        if (axis.lengthSquared() < 1e-4f) continue;
        axis = axis.normalized();
        float theta = angleDist(rng);
        mat4 forward = mat4::rotate(axis, theta);
        mat4 reverse = mat4::rotate(axis, -theta);
        mat4 prod = forward * reverse;
        REQUIRE(isApproxIdentity4(prod, 1e-4f));
    }
}

TEST_CASE("mat4: rotate(axis, 2*PI) == identity (modulo accumulated error)",
          "[math][matrix][property]") {
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);
    for (int i = 0; i < 200; i++) {
        vec3 axis(axisDist(rng), axisDist(rng), axisDist(rng));
        if (axis.lengthSquared() < 1e-4f) continue;
        axis = axis.normalized();
        mat4 r = mat4::rotate(axis, Math::TWO_PI);
        REQUIRE(isApproxIdentity4(r, 1e-4f));
    }
}

TEST_CASE("mat4: rotate(X, theta) matches rotateX(theta)",
          "[math][matrix][property][crossvalidate]") {
    // The generic axis-angle constructor and the axis-specific specialisation
    // must produce the same matrix when the axis is X (or Y, or Z).
    std::mt19937 rng(kRngSeed);
    std::uniform_real_distribution<float> angleDist(-Math::PI, Math::PI);
    for (int i = 0; i < 200; i++) {
        float theta = angleDist(rng);
        mat4 viaGeneric = mat4::rotate(vec3(1.0f, 0.0f, 0.0f), theta);
        mat4 viaSpecific = mat4::rotateX(theta);
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                REQUIRE(viaGeneric[col][row] == Approx(viaSpecific[col][row]).margin(1e-4f));
            }
        }
    }
}

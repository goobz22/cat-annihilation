/**
 * Unit tests for Matrix.hpp camera-projection math.
 *
 * Locks down the Vulkan clip-space depth-range contract: every projection
 * matrix MUST map view-space depth into [0, 1], not OpenGL's [-1, 1].
 * The engine targets Vulkan natively (no OpenGL backend) — emitting the
 * wrong range silently inverts the depth test and the entire scene
 * disappears behind the near plane on the first draw call.
 */

#include "catch.hpp"
#include "engine/math/Matrix.hpp"
#include "engine/math/Vector.hpp"
#include <cmath>

using Engine::mat4;
using Engine::vec4;
using Engine::vec3;

namespace {

constexpr float kProjectionEpsilon = 1e-4f;

// Project a view-space point through a projection matrix and return the
// resulting clip-space Z divided by clip-space W — the value the depth
// test actually uses, in [0, 1] for Vulkan.
float clipDepth(const mat4& projection, const vec3& viewSpacePoint) {
    const vec4 clip = projection * vec4(viewSpacePoint, 1.0f);
    return clip.z / clip.w;
}

} // namespace

TEST_CASE("Matrix::perspective maps near plane to z=0 (Vulkan)", "[matrix][projection]") {
    // Use nearPlane/farPlane (not near/far) because on Windows the legacy
    // 16-bit pointer attribute macros `near` and `far` get defined by
    // windows.h to empty token sequences, and any test source that
    // transitively pulls in windows.h (via <filesystem> on MSVC) would
    // see `mat4::perspective(fovy, aspect, , )` and refuse to compile.
    const float fovy = 1.0f;       // ~57 deg
    const float aspect = 16.0f / 9.0f;
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;

    const mat4 projection = mat4::perspective(fovy, aspect, nearPlane, farPlane);

    // A point on the near plane (view-space z = -nearPlane in the
    // right-handed system produced by Quaternion::lookRotation /
    // mat4::lookAt) must project to clip-space z/w = 0.
    const float zNear = clipDepth(projection, vec3(0.0f, 0.0f, -nearPlane));
    REQUIRE(zNear == Approx(0.0f).margin(kProjectionEpsilon));
}

TEST_CASE("Matrix::perspective maps far plane to z=1 (Vulkan)", "[matrix][projection]") {
    const float fovy = 1.0f;
    const float aspect = 16.0f / 9.0f;
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;

    const mat4 projection = mat4::perspective(fovy, aspect, nearPlane, farPlane);

    // The far plane MUST map to z/w = 1. Pre-fix the matrix was emitting
    // OpenGL's z/w = -1 here; the depth buffer would read that as 0
    // because viewport-state clamps z, and everything would test as the
    // nearest possible fragment — visually identical to "no depth test".
    const float zFar = clipDepth(projection, vec3(0.0f, 0.0f, -farPlane));
    REQUIRE(zFar == Approx(1.0f).margin(kProjectionEpsilon));
}

TEST_CASE("Matrix::perspective midpoint depth is monotonic", "[matrix][projection]") {
    const mat4 projection = mat4::perspective(1.0f, 1.0f, 0.5f, 50.0f);

    const float zMid = clipDepth(projection, vec3(0.0f, 0.0f, -25.0f));

    // Mid-distance must land strictly inside (0, 1), and any closer point
    // must produce a SMALLER clip-space z than any farther point. Both
    // invariants are needed for the GPU's depth test to behave as a real
    // depth comparator instead of a coin flip.
    REQUIRE(zMid > 0.0f);
    REQUIRE(zMid < 1.0f);

    const float zClose = clipDepth(projection, vec3(0.0f, 0.0f, -1.0f));
    const float zMidPoint = clipDepth(projection, vec3(0.0f, 0.0f, -10.0f));
    const float zFarPoint = clipDepth(projection, vec3(0.0f, 0.0f, -40.0f));

    REQUIRE(zClose < zMidPoint);
    REQUIRE(zMidPoint < zFarPoint);
}

TEST_CASE("Matrix::ortho maps near plane to z=0 and far plane to z=1 (Vulkan)",
          "[matrix][projection]") {
    const mat4 projection = mat4::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.5f, 10.0f);

    // Ortho writes z directly into clip-space, divided by w=1, so the
    // clip-depth check is identical to perspective but without the
    // perspective divide. Same Vulkan [0, 1] contract.
    const float zNear = clipDepth(projection, vec3(0.0f, 0.0f, -0.5f));
    const float zFar  = clipDepth(projection, vec3(0.0f, 0.0f, -10.0f));

    REQUIRE(zNear == Approx(0.0f).margin(kProjectionEpsilon));
    REQUIRE(zFar  == Approx(1.0f).margin(kProjectionEpsilon));
}

TEST_CASE("Matrix::perspectiveInfinite preserves Vulkan depth contract", "[matrix][projection]") {
    const mat4 projection = mat4::perspectiveInfinite(1.0f, 1.0f, 0.1f);

    // Near plane still anchors at z=0 even when the far plane is at
    // infinity — that's the whole point of infinite-far projection.
    const float zNear = clipDepth(projection, vec3(0.0f, 0.0f, -0.1f));
    REQUIRE(zNear == Approx(0.0f).margin(kProjectionEpsilon));

    // As view-space z marches toward -inf, the projected depth must
    // approach 1 (never exceed it; the infinite-far convention is the
    // open interval [0, 1)). Picking a large but finite distance here
    // verifies the limit behaviour without involving FLT_MAX edge cases.
    const float zFarish = clipDepth(projection, vec3(0.0f, 0.0f, -1e6f));
    REQUIRE(zFarish > 0.999f);
    REQUIRE(zFarish <= 1.0f);
}

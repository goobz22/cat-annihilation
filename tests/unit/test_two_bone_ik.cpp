// ============================================================================
// Unit tests for the analytic two-bone IK solver.
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Two-bone IK for foot placement".
// Kernel under test: engine/animation/TwoBoneIK.hpp.
//
// These tests cover the invariants a foot / hand IK pass would rely on in
// a real frame: limb-length preservation, exact reach, out-of-reach
// clamping, pole-direction selection, and the ambiguity-resolving
// fallbacks for degenerate inputs (collinear pole, anti-parallel
// rotate-from-to, zero-length chain). The kernel is header-only and has no
// GPU dependency, so it compiles cleanly into the USE_MOCK_GPU=1 test
// executable — same pattern as test_mesh_optimizer.cpp and
// test_shadow_atlas_packer.cpp.
// ============================================================================

#include "catch.hpp"
#include "engine/animation/TwoBoneIK.hpp"

#include <cmath>

using Engine::vec3;
using Engine::Quaternion;
using Engine::TwoBoneIK::Chain;
using Engine::TwoBoneIK::Solution;
using Engine::TwoBoneIK::Solve;
using Engine::TwoBoneIK::ComputeRotationDeltas;
using Engine::TwoBoneIK::detail::RotationFromTo;

namespace {

// Local vec3 equality with a loose tolerance. The IK math accumulates
// several float ops (cross, dot, sqrt, acos) that each drop a ULP or two;
// 1e-4 is the tolerance the MeshOptimizer tests use and it holds here too.
constexpr float IK_EPS = 1e-4f;

bool vec3_approx(const vec3& a, const vec3& b, float eps = IK_EPS) {
    return std::abs(a.x - b.x) < eps &&
           std::abs(a.y - b.y) < eps &&
           std::abs(a.z - b.z) < eps;
}

} // anon

TEST_CASE("TwoBoneIK: exact reach preserves limb lengths",
          "[two_bone_ik]") {
    // Classic rest pose: upper along +X (length 1), lower along +X (length 1),
    // end-effector at (2,0,0). Pole above (0, 1, 0) says "bend upward if
    // the target forces a bend".
    Chain chain{};
    chain.a = vec3(0.0f, 0.0f, 0.0f);
    chain.b = vec3(1.0f, 0.0f, 0.0f);
    chain.c = vec3(2.0f, 0.0f, 0.0f);
    chain.pole = vec3(0.0f, 1.0f, 0.0f);

    // Target within reach: needs the knee to bend upward to hit it.
    vec3 target(1.5f, 0.5f, 0.0f);
    Solution sol = Solve(chain, target);

    REQUIRE(sol.reached);
    REQUIRE(vec3_approx(sol.newC, target));

    // Limb lengths are the IK's hard contract — if these drift, the mesh
    // skin would stretch / compress, which is precisely the bug IK is
    // supposed to prevent. Same rest length 1 for both upper and lower.
    float upperLen = (sol.newB - chain.a).length();
    float lowerLen = (sol.newC - sol.newB).length();
    REQUIRE(std::abs(upperLen - 1.0f) < IK_EPS);
    REQUIRE(std::abs(lowerLen - 1.0f) < IK_EPS);
}

TEST_CASE("TwoBoneIK: out-of-reach target clamps to fully extended limb",
          "[two_bone_ik]") {
    // Rest pose identical to the first test. Target is far beyond maxReach
    // (2.0f) — the solver should straighten the chain along the
    // root-to-target direction and flag reached=false so the caller can
    // render a "can't reach" effect instead of trusting newC == target.
    Chain chain{};
    chain.a = vec3(0.0f, 0.0f, 0.0f);
    chain.b = vec3(1.0f, 0.0f, 0.0f);
    chain.c = vec3(2.0f, 0.0f, 0.0f);
    chain.pole = vec3(0.0f, 1.0f, 0.0f);

    vec3 target(10.0f, 0.0f, 0.0f);  // way out of reach
    Solution sol = Solve(chain, target);

    REQUIRE(!sol.reached);

    // Chain should be nearly fully extended: |newC - a| ≈ 2.0 (maxReach).
    float fullReach = (sol.newC - chain.a).length();
    REQUIRE(std::abs(fullReach - 2.0f) < 1e-3f);

    // Limb lengths must still be preserved even when clamped.
    float upperLen = (sol.newB - chain.a).length();
    float lowerLen = (sol.newC - sol.newB).length();
    REQUIRE(std::abs(upperLen - 1.0f) < 1e-3f);
    REQUIRE(std::abs(lowerLen - 1.0f) < 1e-3f);
}

TEST_CASE("TwoBoneIK: pole selects bend side",
          "[two_bone_ik]") {
    // Same rest pose. Target sits ON the shoulder-to-end line, but closer
    // than the max reach — so the chain must bend. The pole decides which
    // way. Two calls with opposite poles should produce mid joints on
    // opposite sides of the X axis.
    Chain chain{};
    chain.a = vec3(0.0f, 0.0f, 0.0f);
    chain.b = vec3(1.0f, 0.0f, 0.0f);
    chain.c = vec3(2.0f, 0.0f, 0.0f);

    vec3 target(1.5f, 0.0f, 0.0f);  // on the X axis, inside reach

    chain.pole = vec3(0.0f, 1.0f, 0.0f);
    Solution solUp = Solve(chain, target);

    chain.pole = vec3(0.0f, -1.0f, 0.0f);
    Solution solDown = Solve(chain, target);

    REQUIRE(solUp.reached);
    REQUIRE(solDown.reached);

    // Mid joint sits on the pole side of the shoulder-target line.
    REQUIRE(solUp.newB.y > 0.1f);
    REQUIRE(solDown.newB.y < -0.1f);

    // End effectors land on the target regardless of bend direction.
    REQUIRE(vec3_approx(solUp.newC, target));
    REQUIRE(vec3_approx(solDown.newC, target));
}

TEST_CASE("TwoBoneIK: collinear pole degeneracy yields a stable fallback",
          "[two_bone_ik]") {
    // Pole on the same line as shoulder-to-target. The projection onto the
    // perpendicular plane has length 0 — a naive implementation would
    // compute 0/0 and produce NaN. The contract is: fall back to an
    // arbitrary-but-deterministic perpendicular so the chain still bends
    // in a consistent direction.
    Chain chain{};
    chain.a = vec3(0.0f, 0.0f, 0.0f);
    chain.b = vec3(1.0f, 0.0f, 0.0f);
    chain.c = vec3(2.0f, 0.0f, 0.0f);
    chain.pole = vec3(5.0f, 0.0f, 0.0f);  // along +X, same as dirAT

    vec3 target(1.5f, 0.0f, 0.0f);
    Solution sol = Solve(chain, target);

    REQUIRE(sol.reached);

    // NaN check: each component must be finite.
    REQUIRE(std::isfinite(sol.newB.x));
    REQUIRE(std::isfinite(sol.newB.y));
    REQUIRE(std::isfinite(sol.newB.z));

    // Limb lengths preserved even under degeneracy.
    float upperLen = (sol.newB - chain.a).length();
    float lowerLen = (sol.newC - sol.newB).length();
    REQUIRE(std::abs(upperLen - 1.0f) < IK_EPS);
    REQUIRE(std::abs(lowerLen - 1.0f) < IK_EPS);
}

TEST_CASE("TwoBoneIK: zero-length limb returns rest pose unchanged",
          "[two_bone_ik]") {
    // Shoulder and knee coincide → upper limb has zero length. The solver
    // cannot synthesize a valid chain from nothing; it must leave the
    // input alone rather than produce NaN joint positions.
    Chain chain{};
    chain.a = vec3(0.0f, 0.0f, 0.0f);
    chain.b = vec3(0.0f, 0.0f, 0.0f);  // coincident with a
    chain.c = vec3(1.0f, 0.0f, 0.0f);
    chain.pole = vec3(0.0f, 1.0f, 0.0f);

    vec3 target(0.5f, 0.5f, 0.0f);
    Solution sol = Solve(chain, target);

    REQUIRE(!sol.reached);
    REQUIRE(vec3_approx(sol.newB, chain.b));
    REQUIRE(vec3_approx(sol.newC, chain.c));
}

TEST_CASE("TwoBoneIK: target at root returns rest pose unchanged",
          "[two_bone_ik]") {
    // Pathological IK target: exactly at the shoulder. No direction to
    // solve toward. Solver must refuse gracefully (reached=false) rather
    // than normalize a zero vector.
    Chain chain{};
    chain.a = vec3(0.0f, 0.0f, 0.0f);
    chain.b = vec3(1.0f, 0.0f, 0.0f);
    chain.c = vec3(2.0f, 0.0f, 0.0f);
    chain.pole = vec3(0.0f, 1.0f, 0.0f);

    Solution sol = Solve(chain, chain.a);

    REQUIRE(!sol.reached);
    REQUIRE(vec3_approx(sol.newB, chain.b));
    REQUIRE(vec3_approx(sol.newC, chain.c));
}

TEST_CASE("TwoBoneIK: rotation-from-to handles aligned and anti-parallel",
          "[two_bone_ik]") {
    // Aligned (identity). from == to → identity quaternion.
    vec3 x(1.0f, 0.0f, 0.0f);
    Quaternion id = RotationFromTo(x, x);
    REQUIRE(std::abs(id.w - 1.0f) < IK_EPS);
    REQUIRE(std::abs(id.x) < IK_EPS);
    REQUIRE(std::abs(id.y) < IK_EPS);
    REQUIRE(std::abs(id.z) < IK_EPS);

    // Anti-parallel. Rotation must be 180°; the specific axis is
    // implementation-defined but the post-rotation direction must flip.
    vec3 negX(-1.0f, 0.0f, 0.0f);
    Quaternion flip = RotationFromTo(x, negX);
    vec3 rotated = flip.rotate(x);
    REQUIRE(vec3_approx(rotated, negX, 1e-3f));

    // Generic 90° case: +X → +Y should produce a quaternion that, when
    // applied to +X, yields +Y.
    vec3 y(0.0f, 1.0f, 0.0f);
    Quaternion ninety = RotationFromTo(x, y);
    vec3 rotatedY = ninety.rotate(x);
    REQUIRE(vec3_approx(rotatedY, y, 1e-4f));

    // Zero-length input: identity. Documents the defensive behaviour in
    // detail::RotationFromTo — zero-length bones shouldn't crash the IK
    // pass, they should no-op.
    vec3 zero(0.0f, 0.0f, 0.0f);
    Quaternion noop = RotationFromTo(zero, y);
    REQUIRE(std::abs(noop.w - 1.0f) < IK_EPS);
}

TEST_CASE("TwoBoneIK: rotation deltas reconstruct solved positions",
          "[two_bone_ik]") {
    // Round-trip check: applying the returned upperDelta to the rest upper
    // bone vector should land on the solved upper bone vector, and the
    // same must hold for the lower bone (with the upper rotation composed
    // in first, per the ComputeRotationDeltas contract). If this
    // round-trip ever fails, the caller that uses these deltas to write
    // local-rotation updates would be producing a chain that doesn't
    // agree with the position solve.
    Chain chain{};
    chain.a = vec3(0.0f, 0.0f, 0.0f);
    chain.b = vec3(1.0f, 0.0f, 0.0f);
    chain.c = vec3(2.0f, 0.0f, 0.0f);
    chain.pole = vec3(0.0f, 1.0f, 0.0f);

    vec3 target(1.2f, 0.8f, 0.0f);
    Solution sol = Solve(chain, target);
    REQUIRE(sol.reached);

    auto deltas = ComputeRotationDeltas(chain, sol);

    // Upper delta rotates rest upper → solved upper.
    vec3 restUpper = chain.b - chain.a;
    vec3 solvedUpper = sol.newB - chain.a;
    vec3 rotatedUpper = deltas.upperDelta.rotate(restUpper);
    REQUIRE(vec3_approx(rotatedUpper, solvedUpper, 1e-3f));

    // Lower delta, composed after the upper delta, rotates rest lower →
    // solved lower. The composition order matches how a skinning pipeline
    // would apply the deltas onto bone-local rotations that accumulate
    // along the parent chain.
    vec3 restLower = chain.c - chain.b;
    vec3 solvedLower = sol.newC - sol.newB;
    vec3 upperRotatedLower = deltas.upperDelta.rotate(restLower);
    vec3 fullyRotatedLower = deltas.lowerDelta.rotate(upperRotatedLower);
    REQUIRE(vec3_approx(fullyRotatedLower, solvedLower, 1e-3f));
}

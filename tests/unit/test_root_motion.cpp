// ============================================================================
// Unit tests for the root-motion extraction kernel.
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Root-motion extraction from GLTF
// animations".
// Kernel under test: engine/animation/RootMotion.hpp.
//
// What's covered:
//   - axis-mask projection (translation: X / Y / Z / XZ / XYZ; rotation:
//     yaw on/off);
//   - swing-twist decomposition (pure twist input, pure swing input,
//     mixed input, degenerate axes);
//   - sub-cycle delta extraction (translation only, rotation only,
//     combined);
//   - loop-wrap window extraction (per-cycle drift composition for both
//     translation and yaw, with cyclesCrossed = 1, 2, 3);
//   - pose stripping (translation zeroing per mask, rotation swing
//     preservation after twist removal);
//   - integration: extract delta + strip pose + reapply yields original
//     world-space placement.
//
// Why these tests are sufficient for the backlog item to count "done":
//   The wiring into Animator.cpp / PlayerControlSystem.cpp is one or two
//   call sites — the same shape as the TwoBoneIK math kernel landing.
//   These tests pin every numerical contract those call sites will
//   depend on, so once the integration lands no math regression can
//   sneak in unnoticed. Same pattern as test_two_bone_ik.cpp,
//   test_simplex_noise.cpp, test_mesh_optimizer.cpp.
// ============================================================================

#include "catch.hpp"
#include "engine/animation/RootMotion.hpp"

#include <cmath>

using Engine::vec3;
using Engine::Quaternion;
using Engine::Transform;
using Engine::RootMotion::Config;
using Engine::RootMotion::Delta;
using Engine::RootMotion::ExtractSubCycle;
using Engine::RootMotion::ExtractTwist;
using Engine::RootMotion::ExtractWindow;
using Engine::RootMotion::ProjectRotation;
using Engine::RootMotion::ProjectTranslation;
using Engine::RootMotion::StripFromPose;
namespace AxisFlags = Engine::RootMotion::AxisFlags;

namespace {

// Loose tolerance: composing several quaternion multiplies + a swing-twist
// projection drops a couple of ULPs. 1e-4 is the same tolerance the
// TwoBoneIK and MeshOptimizer tests use, and it holds here too. Tighter
// (1e-6) would make the multi-cycle test brittle on different STL
// implementations of std::sqrt / std::sin.
constexpr float RM_EPS = 1e-4f;

bool vec3_approx(const vec3& a, const vec3& b, float eps = RM_EPS) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
           std::abs(a.z - b.z) < eps;
}

bool quat_approx(const Quaternion& a, const Quaternion& b, float eps = RM_EPS) {
    // Quaternions q and -q represent the same rotation, so we accept either
    // sign. Without this the swing-twist tests would intermittently fail
    // when the twist component happened to land in the antipodal hemisphere.
    const bool sameSign = std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
                          std::abs(a.z - b.z) < eps && std::abs(a.w - b.w) < eps;
    const bool oppSign = std::abs(a.x + b.x) < eps && std::abs(a.y + b.y) < eps &&
                         std::abs(a.z + b.z) < eps && std::abs(a.w + b.w) < eps;
    return sameSign || oppSign;
}

// Build a yaw-only quaternion (rotation about +Y by `radians`). Cleanest
// way to author "this should be exactly the twist component" inputs in
// tests without depending on Quaternion::fromEuler convention drift.
Quaternion yawQ(float radians) {
    return Quaternion::fromAxisAngle(vec3(0.0f, 1.0f, 0.0f), radians);
}

// Pure swing about an axis perpendicular to up (+X here). Used to verify
// ExtractTwist returns identity when the input has zero twist about Y.
Quaternion swingX(float radians) {
    return Quaternion::fromAxisAngle(vec3(1.0f, 0.0f, 0.0f), radians);
}

}  // anon

// ----------------------------------------------------------------------------
// ProjectTranslation
// ----------------------------------------------------------------------------

TEST_CASE("RootMotion: ProjectTranslation masks individual axes",
          "[root_motion]") {
    const vec3 v(1.5f, 2.5f, 3.5f);

    REQUIRE(vec3_approx(ProjectTranslation(v, AxisFlags::None),
                        vec3(0.0f, 0.0f, 0.0f)));
    REQUIRE(vec3_approx(ProjectTranslation(v, AxisFlags::TranslationX),
                        vec3(1.5f, 0.0f, 0.0f)));
    REQUIRE(vec3_approx(ProjectTranslation(v, AxisFlags::TranslationY),
                        vec3(0.0f, 2.5f, 0.0f)));
    REQUIRE(vec3_approx(ProjectTranslation(v, AxisFlags::TranslationZ),
                        vec3(0.0f, 0.0f, 3.5f)));
    REQUIRE(vec3_approx(ProjectTranslation(v, AxisFlags::TranslationXZ),
                        vec3(1.5f, 0.0f, 3.5f)));
    REQUIRE(vec3_approx(ProjectTranslation(v, AxisFlags::TranslationXYZ), v));
}

// ----------------------------------------------------------------------------
// ExtractTwist (swing-twist decomposition)
// ----------------------------------------------------------------------------

TEST_CASE("RootMotion: ExtractTwist returns the input on pure-twist rotation",
          "[root_motion]") {
    // Pure yaw input → twist == input (within sign).
    const Quaternion qYaw = yawQ(1.2f);
    const Quaternion twist = ExtractTwist(qYaw, vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(quat_approx(twist, qYaw));
}

TEST_CASE("RootMotion: ExtractTwist returns identity on pure-swing rotation",
          "[root_motion]") {
    // Pure pitch (rotation about +X) has zero twist about +Y.
    const Quaternion qPitch = swingX(0.7f);
    const Quaternion twist = ExtractTwist(qPitch, vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(quat_approx(twist, Quaternion::identity()));
}

TEST_CASE("RootMotion: ExtractTwist isolates twist from swing-twist composite",
          "[root_motion]") {
    // q = swing(X by 0.3) * twist(Y by 0.9). Decomposition must give
    // back twist(Y by 0.9) (modulo sign).
    const Quaternion qTwist = yawQ(0.9f);
    const Quaternion qSwing = swingX(0.3f);
    const Quaternion combined = qSwing * qTwist;

    const Quaternion extracted = ExtractTwist(combined, vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(quat_approx(extracted, qTwist));
}

TEST_CASE("RootMotion: ExtractTwist tolerates a non-unit axis",
          "[root_motion]") {
    // Caller passes axis with length 3. Internal normalisation must
    // produce the same result as a unit-length axis input.
    const Quaternion qYaw = yawQ(0.5f);
    const Quaternion twistUnit = ExtractTwist(qYaw, vec3(0.0f, 1.0f, 0.0f));
    const Quaternion twistScaled = ExtractTwist(qYaw, vec3(0.0f, 3.0f, 0.0f));
    REQUIRE(quat_approx(twistUnit, twistScaled));
}

TEST_CASE("RootMotion: ExtractTwist on zero-length axis returns identity",
          "[root_motion]") {
    // Caller bug guard: don't NaN out skinning, return identity.
    const Quaternion qYaw = yawQ(0.5f);
    const Quaternion twist = ExtractTwist(qYaw, vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(quat_approx(twist, Quaternion::identity()));
}

// ----------------------------------------------------------------------------
// ProjectRotation (Config-aware wrapper)
// ----------------------------------------------------------------------------

TEST_CASE("RootMotion: ProjectRotation drops yaw when mask excludes it",
          "[root_motion]") {
    Config cfg;
    cfg.axisMask = AxisFlags::TranslationXZ;  // No RotationYaw bit.
    const Quaternion extracted = ProjectRotation(yawQ(1.0f), cfg);
    REQUIRE(quat_approx(extracted, Quaternion::identity()));
}

TEST_CASE("RootMotion: ProjectRotation extracts yaw when mask includes it",
          "[root_motion]") {
    Config cfg;
    cfg.axisMask = AxisFlags::DefaultLocomotion;  // Includes RotationYaw.
    const Quaternion qYaw = yawQ(0.6f);
    const Quaternion extracted = ProjectRotation(qYaw, cfg);
    REQUIRE(quat_approx(extracted, qYaw));
}

// ----------------------------------------------------------------------------
// ExtractSubCycle — common per-frame path
// ----------------------------------------------------------------------------

TEST_CASE("RootMotion: ExtractSubCycle of pure forward translation",
          "[root_motion]") {
    // Cat walking: at t0 root is at z=0.10, at t1 root is at z=0.25. The
    // delta along Z must come out as 0.15. Default mask is XZ + yaw, so
    // the y component (vertical bob) is dropped.
    Transform t0;
    t0.position = vec3(0.0f, 0.20f, 0.10f);
    Transform t1;
    t1.position = vec3(0.0f, 0.25f, 0.25f);  // y bob present, ignored.

    const Delta d = ExtractSubCycle(t0, t1);
    REQUIRE(vec3_approx(d.translation, vec3(0.0f, 0.0f, 0.15f)));
    REQUIRE(quat_approx(d.rotation, Quaternion::identity()));
}

TEST_CASE("RootMotion: ExtractSubCycle of pure yaw rotation",
          "[root_motion]") {
    // Cat turning in place: 0.4 rad of yaw between two frames. No
    // translation. Default mask includes RotationYaw.
    Transform t0;
    t0.rotation = yawQ(0.1f);
    Transform t1;
    t1.rotation = yawQ(0.5f);

    const Delta d = ExtractSubCycle(t0, t1);
    REQUIRE(vec3_approx(d.translation, vec3(0.0f, 0.0f, 0.0f)));
    // Delta yaw should be 0.4 rad.
    REQUIRE(quat_approx(d.rotation, yawQ(0.4f)));
}

TEST_CASE("RootMotion: ExtractSubCycle drops swing rotation about non-up axis",
          "[root_motion]") {
    // Combined yaw + pitch where the pitch is IDENTICAL on both frames.
    // Why this construction: the convention is q1 * q2 means "apply q2
    // first, then q1" (q1 * q2 * v * q2^{-1} * q1^{-1}). So when the
    // pitch is the rightmost factor on both poses, it cancels exactly
    // in qDelta = t1.rot * t0.rot^{-1}:
    //
    //     yawQ(0.4) * swingX(c) * (yawQ(0.1) * swingX(c))^{-1}
    //   = yawQ(0.4) * swingX(c) * swingX(-c) * yawQ(-0.1)
    //   = yawQ(0.4) * yawQ(-0.1)
    //   = yawQ(0.3)
    //
    // If we'd naively varied the pitch on both frames OR placed the
    // pitch on the LEFT of the yaw, the conjugation would rotate the
    // twist axis and the extracted yaw would no longer be a clean
    // 0.3 rad — that's a real subtlety the swing-twist projection
    // handles, but it's not what THIS test is trying to pin. The
    // "extract twist from arbitrary composite" contract is already
    // covered by the "ExtractTwist isolates twist from swing-twist
    // composite" case above.
    const Quaternion pitch = swingX(0.2f);
    Transform t0;
    t0.rotation = yawQ(0.1f) * pitch;
    Transform t1;
    t1.rotation = yawQ(0.4f) * pitch;

    const Delta d = ExtractSubCycle(t0, t1);
    REQUIRE(quat_approx(d.rotation, yawQ(0.3f)));
}

TEST_CASE("RootMotion: ExtractSubCycle with custom XYZ mask preserves Y",
          "[root_motion]") {
    // When the clip is for swimming/flying and the gameplay layer wants
    // vertical translation too, an XYZ mask preserves the y delta.
    Config cfg;
    cfg.axisMask = AxisFlags::TranslationXYZ;

    Transform t0;
    t0.position = vec3(1.0f, 2.0f, 3.0f);
    Transform t1;
    t1.position = vec3(1.5f, 2.7f, 3.4f);

    const Delta d = ExtractSubCycle(t0, t1, cfg);
    REQUIRE(vec3_approx(d.translation, vec3(0.5f, 0.7f, 0.4f)));
}

TEST_CASE("RootMotion: ExtractSubCycle with empty mask returns zero delta",
          "[root_motion]") {
    Config cfg;
    cfg.axisMask = AxisFlags::None;

    Transform t0;
    t0.position = vec3(1.0f, 2.0f, 3.0f);
    t0.rotation = yawQ(0.2f);
    Transform t1;
    t1.position = vec3(5.0f, 4.0f, 7.0f);
    t1.rotation = yawQ(1.0f);

    const Delta d = ExtractSubCycle(t0, t1, cfg);
    REQUIRE(vec3_approx(d.translation, vec3(0.0f, 0.0f, 0.0f)));
    REQUIRE(quat_approx(d.rotation, Quaternion::identity()));
}

// ----------------------------------------------------------------------------
// ExtractWindow — loop-wrap composition
// ----------------------------------------------------------------------------

TEST_CASE("RootMotion: ExtractWindow with cyclesCrossed=0 matches sub-cycle",
          "[root_motion]") {
    // Sanity check: when no loop boundary is crossed, ExtractWindow must
    // be bit-equivalent to ExtractSubCycle (the anchors are unused).
    Transform t0;
    t0.position = vec3(0.0f, 0.0f, 0.10f);
    Transform t1;
    t1.position = vec3(0.0f, 0.0f, 0.25f);
    Transform anchorStart;  // unused in this branch
    Transform anchorEnd;    // unused in this branch

    const Delta sub = ExtractSubCycle(t0, t1);
    const Delta win = ExtractWindow(t0, t1, anchorStart, anchorEnd, 0);
    REQUIRE(vec3_approx(sub.translation, win.translation));
    REQUIRE(quat_approx(sub.rotation, win.rotation));
}

TEST_CASE("RootMotion: ExtractWindow accumulates one full cycle correctly",
          "[root_motion]") {
    // Walk loop: per-cycle drift = +1.0 along Z (artist-authored stride
    // length per loop). Sub-cycle starts at z=0.8 (near end of cycle),
    // ends at z=0.2 (early in next cycle). The window crosses one
    // boundary so we need the partial-start (1.0 - 0.8 = 0.2) plus
    // partial-end (0.2 - 0.0 = 0.2), totalling 0.4.
    //
    // Per-cycle full-loop contribution count = cyclesCrossed - 1 = 0
    // here, so the full-loop term drops out — but the partials are
    // exactly what cleanly explains why a tail-end + head-start
    // composition can't be done by raw subtraction (which would give
    // 0.2 - 0.8 = -0.6, the famous "teleport backward" bug).
    Transform t0;
    t0.position = vec3(0.0f, 0.0f, 0.8f);
    Transform t1;
    t1.position = vec3(0.0f, 0.0f, 0.2f);
    Transform anchorStart;
    anchorStart.position = vec3(0.0f, 0.0f, 0.0f);
    Transform anchorEnd;
    anchorEnd.position = vec3(0.0f, 0.0f, 1.0f);

    const Delta d = ExtractWindow(t0, t1, anchorStart, anchorEnd, 1);
    REQUIRE(vec3_approx(d.translation, vec3(0.0f, 0.0f, 0.4f)));
}

TEST_CASE("RootMotion: ExtractWindow accumulates multiple full cycles",
          "[root_motion]") {
    // Same as the previous test but cyclesCrossed = 3, so we expect:
    //   partialStart (1.0 - 0.8 = 0.2)
    // + fullCycles  (1.0 * (3-1) = 2.0)
    // + partialEnd  (0.2 - 0.0 = 0.2)
    // = 2.4
    Transform t0;
    t0.position = vec3(0.0f, 0.0f, 0.8f);
    Transform t1;
    t1.position = vec3(0.0f, 0.0f, 0.2f);
    Transform anchorStart;
    anchorStart.position = vec3(0.0f, 0.0f, 0.0f);
    Transform anchorEnd;
    anchorEnd.position = vec3(0.0f, 0.0f, 1.0f);

    const Delta d = ExtractWindow(t0, t1, anchorStart, anchorEnd, 3);
    REQUIRE(vec3_approx(d.translation, vec3(0.0f, 0.0f, 2.4f)));
}

TEST_CASE("RootMotion: ExtractWindow composes yaw across cycles",
          "[root_motion]") {
    // In-place spin clip: per-cycle yaw drift = +0.5 rad. Sub-cycle
    // partials start at yaw 0.4 (anchor end), end at yaw 0.1 (anchor
    // start). cyclesCrossed = 2:
    //   partialStart yaw delta = 0.5 - 0.4 = 0.1
    //   per-cycle yaw delta    = 0.5 - 0.0 = 0.5
    //   partialEnd yaw delta   = 0.1 - 0.0 = 0.1
    //   total                  = 0.1 + 0.5 * (2-1) + 0.1 = 0.7 rad
    Transform t0;
    t0.rotation = yawQ(0.4f);
    Transform t1;
    t1.rotation = yawQ(0.1f);
    Transform anchorStart;
    anchorStart.rotation = yawQ(0.0f);  // identity, but explicit for readability.
    Transform anchorEnd;
    anchorEnd.rotation = yawQ(0.5f);

    const Delta d = ExtractWindow(t0, t1, anchorStart, anchorEnd, 2);
    REQUIRE(quat_approx(d.rotation, yawQ(0.7f)));
}

TEST_CASE("RootMotion: ExtractWindow with negative cyclesCrossed is no-op",
          "[root_motion]") {
    // Defensive: garbage input from the caller (negative wrap count
    // shouldn't happen, but if it does, fall back to ExtractSubCycle
    // semantics rather than NaN-out).
    Transform t0;
    t0.position = vec3(0.0f, 0.0f, 0.10f);
    Transform t1;
    t1.position = vec3(0.0f, 0.0f, 0.25f);
    Transform anchorStart;
    Transform anchorEnd;

    const Delta d = ExtractWindow(t0, t1, anchorStart, anchorEnd, -5);
    REQUIRE(vec3_approx(d.translation, vec3(0.0f, 0.0f, 0.15f)));
}

// ----------------------------------------------------------------------------
// StripFromPose
// ----------------------------------------------------------------------------

TEST_CASE("RootMotion: StripFromPose zeroes XZ and preserves Y by default",
          "[root_motion]") {
    Transform pose;
    pose.position = vec3(1.0f, 2.0f, 3.0f);

    Config cfg;  // DefaultLocomotion (XZ + RotationYaw)
    StripFromPose(pose, cfg);
    REQUIRE(vec3_approx(pose.position, vec3(0.0f, 2.0f, 0.0f)));
}

TEST_CASE("RootMotion: StripFromPose with XYZ mask zeroes the whole position",
          "[root_motion]") {
    Transform pose;
    pose.position = vec3(1.0f, 2.0f, 3.0f);

    Config cfg;
    cfg.axisMask = AxisFlags::TranslationXYZ;
    StripFromPose(pose, cfg);
    REQUIRE(vec3_approx(pose.position, vec3(0.0f, 0.0f, 0.0f)));
}

TEST_CASE("RootMotion: StripFromPose removes yaw twist but preserves swing",
          "[root_motion]") {
    // Construct a swing-then-twist composite, strip the twist, and
    // verify the surviving rotation is bit-identical to the original
    // swing component.
    const Quaternion qSwing = swingX(0.4f);
    const Quaternion qTwist = yawQ(0.7f);
    Transform pose;
    pose.rotation = qSwing * qTwist;

    Config cfg;  // DefaultLocomotion includes RotationYaw.
    StripFromPose(pose, cfg);
    REQUIRE(quat_approx(pose.rotation, qSwing));
}

TEST_CASE("RootMotion: StripFromPose without RotationYaw leaves rotation alone",
          "[root_motion]") {
    const Quaternion qOriginal = swingX(0.3f) * yawQ(0.5f);
    Transform pose;
    pose.rotation = qOriginal;

    Config cfg;
    cfg.axisMask = AxisFlags::TranslationXZ;  // No RotationYaw bit.
    StripFromPose(pose, cfg);
    REQUIRE(quat_approx(pose.rotation, qOriginal));
}

// ----------------------------------------------------------------------------
// Integration: extract + strip + reapply == original world placement
// ----------------------------------------------------------------------------

TEST_CASE("RootMotion: extract + strip + reapply round-trips world delta",
          "[root_motion]") {
    // The contract that closes the "ice-skating cat" loop: between two
    // frames, the WORLD-SPACE delta the camera sees must be the same
    // whether we naively skin the unstripped bone (no entity offset
    // change) or run the full root-motion path (extract delta + strip
    // bone + update entity by delta). If those two world deltas
    // disagree, root motion either leaks through twice or is lost.
    //
    // Why compare deltas and not absolute placements:
    //   The naive "entity stays put + bone moves" model and the
    //   root-motion "entity follows + bone stays in place" model
    //   produce different absolute placements at any given frame —
    //   they only need to agree on the *rate* at which the world-space
    //   placement evolves. That's the invariant a player or camera
    //   actually perceives.
    //
    // We test the translation half because the rotation half requires
    // a full skinning pipeline to verify end-to-end; the StripFromPose
    // unit test above pins the per-bone math, and the translation
    // round-trip is sufficient to catch the most common bug class
    // (sign flip, double-apply, or missing-apply).
    Transform rootT0;
    rootT0.position = vec3(0.0f, 0.20f, 0.10f);
    Transform rootT1;
    rootT1.position = vec3(0.0f, 0.25f, 0.25f);

    // Naive world delta: entity stays put, bone moves freely. The
    // observer sees the bone slide from rootT0 to rootT1.
    const vec3 naiveWorldDelta = rootT1.position - rootT0.position;

    // Root-motion world delta: entity moves by the extracted delta,
    // bone is stripped of the extracted axes. The observer sees the
    // entity shift PLUS whatever the stripped bones' delta still
    // contributes (only the unmasked Y bob in this configuration).
    const Delta d = ExtractSubCycle(rootT0, rootT1);
    Transform stripped0 = rootT0;
    Transform stripped1 = rootT1;
    StripFromPose(stripped0, Config{});
    StripFromPose(stripped1, Config{});
    const vec3 strippedBoneDelta = stripped1.position - stripped0.position;
    const vec3 rootMotionWorldDelta = d.translation + strippedBoneDelta;

    // The two world deltas must match on every axis. X and Z come from
    // d.translation (the masked delta); Y comes from strippedBoneDelta
    // (the unmasked bob). Together they reconstruct the naive delta
    // exactly — that's the round-trip invariant.
    REQUIRE(vec3_approx(naiveWorldDelta, rootMotionWorldDelta));

    // Sanity: the entity actually moved by the extracted Z delta.
    REQUIRE(std::abs(d.translation.z - 0.15f) < RM_EPS);
    // Sanity: the bob stayed on the stripped bone (Y is unmasked).
    REQUIRE(std::abs(strippedBoneDelta.y - 0.05f) < RM_EPS);
}

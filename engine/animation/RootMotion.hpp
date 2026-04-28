#ifndef ENGINE_ROOT_MOTION_HPP
#define ENGINE_ROOT_MOTION_HPP

// ============================================================================
// Root-motion extraction.
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Root-motion extraction from GLTF
// animations".
//
// What is "root motion"?
//
//   Most authored skeletal animations (walk, run, sprint, dodge-roll, sword
//   swing follow-through) bake a per-loop forward translation and yaw onto
//   the root bone of the rig. If the runtime samples that root channel
//   verbatim and skins it visually, the character's mesh slides forward
//   while the entity's gameplay transform stays planted at origin — the
//   classic "ice-skating cat" bug. Conversely, if the runtime ignores the
//   root channel and drives the entity transform from raw input/physics,
//   the mesh's feet will skate against the ground because the foot-plant
//   timing the artist authored is no longer aligned with the actual world
//   displacement.
//
//   The fix is "root motion": extract the per-frame delta translation
//   (and optionally the delta yaw) authored on the root bone, hand it to
//   the entity transform / character controller / physics body so the
//   gameplay moves with the animation, AND strip those components from
//   the bone pose so the visual skeleton stays at origin in skeleton
//   space (i.e. the renderer skins the character at its rest origin and
//   the entity transform plus the extracted delta provide the world
//   placement). Both halves matter — without the strip, the motion is
//   applied twice; without the extract, gameplay never moves.
//
// Why pure-math header-only (no Animation/Skeleton coupling)?
//
//   Same rationale as TwoBoneIK.hpp and SimplexNoise.hpp: the math is
//   short, has no GPU/RHI dependency, can be exercised against synthetic
//   data with zero asset-pipeline overhead, and the integration into the
//   real Animator state machine becomes a couple of call sites once the
//   numerics are pinned. The Animator (engine/animation/Animator.cpp)
//   will, in a follow-up, sample the root channel at its previous and
//   current playback times and call ExtractSubCycle(); the controller
//   layer (game/systems/PlayerControlSystem.cpp) will read the returned
//   Delta and offset the entity transform / kinematic-body target.
//
// Coordinate-system contract:
//
//   The input transforms (rootAtT0, rootAtT1, anchors) are in the bone's
//   parent-local frame, which for a glTF root bone is the entity's
//   model space (the renderer's "object" space). The returned Delta is
//   therefore also in model space — the caller transforms it into world
//   space by the entity's CURRENT world rotation (typically yaw-only).
//   If the entity orientation is updated this same frame from the
//   extracted yaw, the standard order is: apply rotation first, then
//   translate by `entityRotation * delta.translation`.
//
//   `upAxis` defaults to (0, 1, 0) matching engine/math/Vector.hpp's
//   `vec3::up()` convention. Override only if a particular skeleton was
//   exported with a non-Y-up convention (Z-up Blender glTF exports do
//   exist but are corrected at import time in the asset loader, so in
//   practice this default is what the engine uses).
//
// Loop-wrap correctness:
//
//   For a cyclic locomotion clip the artist authors a per-loop forward
//   step (e.g. walk loop = 1.2 s, root z advances by 0.6 m per loop). The
//   per-cycle delta is therefore `cycleAnchorEnd - cycleAnchorStart`
//   where both anchors are sampled from the SAME clip at t=0 and
//   t=duration. Sub-frame updates that don't cross a loop boundary use
//   ExtractSubCycle (cyclesCrossed = 0); updates that DO cross n loop
//   boundaries call ExtractWindow with `cyclesCrossed = n` and the
//   anchors so the partial-start, n full cycles, and partial-end
//   contributions sum cleanly. This matters whenever the dt for a frame
//   exceeds the remaining clip time — common when the loop is short
//   (idle cycle) or the dt is large (single-step debug, framerate
//   hitch). Without the wrap math the character would teleport
//   backward by one cycle's worth of drift.
//
//   Non-cyclic clips (one-shot attacks, the wind-up of a magic spell)
//   pass identity for both anchors and `cyclesCrossed = 0`; in that
//   regime ExtractWindow degenerates to the same `t1 - t0` subtraction
//   as ExtractSubCycle.
//
// References:
//   - Unity Animation Programming Guide ("Root Motion")
//   - Unreal Engine Animation System Overview ("Root Motion Sources")
//   - Granny / Havok / Wwise game-AI papers on swing-twist decomposition
//     for yaw-only character rotation extraction.
// ============================================================================

#include "../math/Math.hpp"
#include "../math/Quaternion.hpp"
#include "../math/Transform.hpp"
#include "../math/Vector.hpp"
#include <cmath>
#include <cstdint>

namespace Engine {
namespace RootMotion {

// ----------------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------------

// Bit flags selecting which transform components are treated as root motion.
//
// Why a bitmask and not separate booleans?
//   Several common gameplay configurations need the same set of axes (XZ-only
//   for ground locomotion, XYZ for swimming/flying clips, RotationYaw for
//   in-place spins). A bitmask lets a config table or a designer-facing
//   ImGui panel encode the choice in one int and lets the Project* helpers
//   below dispatch in O(1) with bitwise ANDs instead of branching ladders.
namespace AxisFlags {
constexpr uint32_t None             = 0u;
constexpr uint32_t TranslationX     = 1u << 0;
constexpr uint32_t TranslationY     = 1u << 1;
constexpr uint32_t TranslationZ     = 1u << 2;
constexpr uint32_t TranslationXZ    = TranslationX | TranslationZ;
constexpr uint32_t TranslationXYZ   = TranslationX | TranslationY | TranslationZ;
constexpr uint32_t RotationYaw      = 1u << 3;
// Sensible default for ground-walking quadrupeds (cat, dog) — horizontal
// translation goes to the entity, vertical bob stays on the bone for visual
// gait, and in-place yaw becomes character turning.
constexpr uint32_t DefaultLocomotion = TranslationXZ | RotationYaw;
}  // namespace AxisFlags

struct Config {
    uint32_t axisMask = AxisFlags::DefaultLocomotion;
    // Up axis the swing-twist decomposition projects onto when extracting
    // RotationYaw. Engine convention is +Y up (matches vec3::up()).
    vec3 upAxis = vec3(0.0f, 1.0f, 0.0f);
};

// ----------------------------------------------------------------------------
// Output
// ----------------------------------------------------------------------------

struct Delta {
    // Translation in the bone's parent-local frame (== entity model space
    // for a glTF root bone). Caller rotates this by the entity's current
    // world orientation to get the world-space step.
    vec3 translation = vec3(0.0f, 0.0f, 0.0f);
    // Yaw delta (twist about cfg.upAxis). Identity if RotationYaw is not in
    // the mask. Caller post-multiplies onto the entity's world rotation.
    Quaternion rotation = Quaternion::identity();
};

// ----------------------------------------------------------------------------
// Pure-math helpers (exposed for testing the degenerate cases directly)
// ----------------------------------------------------------------------------

// Zero out vector components not selected by axisMask. Inverse of "keep only
// the selected axes".
inline vec3 ProjectTranslation(const vec3& v, uint32_t axisMask) {
    return vec3(
        (axisMask & AxisFlags::TranslationX) ? v.x : 0.0f,
        (axisMask & AxisFlags::TranslationY) ? v.y : 0.0f,
        (axisMask & AxisFlags::TranslationZ) ? v.z : 0.0f);
}

// Swing-twist decomposition: extract the rotation about `axis` from `q`.
//
// Algorithm (Diebel 2006 §B, popularised by Erin Catto's GDC slides):
//   For unit axis a, project the imaginary part (q.x, q.y, q.z) onto a:
//       p = ((qxyz · a)) * a
//   The twist is then twist = normalize(Quaternion(p, q.w)). This works
//   because rotations about `a` lie on a great circle in quaternion space
//   whose imaginary part is parallel to a; projecting and renormalising
//   recovers the twist component without trigonometry.
//
// Degenerate fallbacks:
//   - Zero-length axis → identity (caller bug, but don't NaN-out skinning).
//   - Zero-length projected quaternion (== rotation perpendicular to axis)
//     → identity (the input has no twist component about this axis).
inline Quaternion ExtractTwist(const Quaternion& q, const vec3& axis) {
    const float axisLenSq = axis.lengthSquared();
    if (axisLenSq < Math::EPSILON) {
        return Quaternion::identity();
    }
    // Normalise the axis up-front so callers can pass approximately-unit
    // input (e.g. a cfg.upAxis the user mutated) without poisoning the
    // projection magnitude downstream.
    const float invAxisLen = 1.0f / std::sqrt(axisLenSq);
    const vec3 a(axis.x * invAxisLen, axis.y * invAxisLen, axis.z * invAxisLen);

    // Imaginary part of q dotted with the axis: scalar projection.
    const float d = q.x * a.x + q.y * a.y + q.z * a.z;
    Quaternion twist(d * a.x, d * a.y, d * a.z, q.w);
    const float twistLenSq = twist.lengthSquared();
    if (twistLenSq < Math::EPSILON) {
        // The input rotation is purely a swing about `a` — no twist.
        return Quaternion::identity();
    }
    const float invLen = 1.0f / std::sqrt(twistLenSq);
    twist.x *= invLen;
    twist.y *= invLen;
    twist.z *= invLen;
    twist.w *= invLen;
    return twist;
}

// Project a delta rotation onto the configured rotation axes. Today only
// RotationYaw is supported (single up axis); future extensions for pitch /
// roll can add new flags + new ExtractTwist calls without breaking the
// signature.
inline Quaternion ProjectRotation(const Quaternion& q, const Config& cfg) {
    if ((cfg.axisMask & AxisFlags::RotationYaw) == 0u) {
        return Quaternion::identity();
    }
    return ExtractTwist(q, cfg.upAxis);
}

// ----------------------------------------------------------------------------
// Delta extraction
// ----------------------------------------------------------------------------

// Sub-cycle window: caller guarantees t0 and t1 are inside the same clip
// cycle (no loop boundary crossed). Common path for normal frame updates
// where dt is sub-cycle.
inline Delta ExtractSubCycle(const Transform& rootAtT0,
                             const Transform& rootAtT1,
                             const Config& cfg = Config{}) {
    Delta out;
    // Translation: raw difference in bone-local space, masked.
    const vec3 rawTrans = rootAtT1.position - rootAtT0.position;
    out.translation = ProjectTranslation(rawTrans, cfg.axisMask);
    // Rotation: q1 = qDelta * q0, so qDelta = q1 * q0^{-1}. We then project
    // qDelta onto the configured axes (yaw only, today).
    const Quaternion qDelta = rootAtT1.rotation * rootAtT0.rotation.inverse();
    out.rotation = ProjectRotation(qDelta, cfg);
    return out;
}

// Window crossing zero or more loop boundaries. See the loop-wrap section
// of the file header for the math derivation. `cyclesCrossed` is the
// number of complete clip-loops elapsed (n = floor(t1/dur) - floor(t0/dur));
// the caller computes this from the playback timestamps it already has.
//
// Why exposed as a separate function from ExtractSubCycle?
//   The cycle-crossing case is rarer and needs two extra inputs (the
//   anchors). Forcing every per-frame call to construct anchors it never
//   uses would be wasteful, and an `if (cyclesCrossed == 0)` branch
//   inside ExtractSubCycle would obscure the common path. Two named
//   functions make the intent explicit at every call site.
inline Delta ExtractWindow(const Transform& rootAtT0,
                           const Transform& rootAtT1,
                           const Transform& cycleAnchorStart,
                           const Transform& cycleAnchorEnd,
                           int cyclesCrossed,
                           const Config& cfg = Config{}) {
    if (cyclesCrossed <= 0) {
        // No loop boundary crossed — same as the simple subtraction.
        return ExtractSubCycle(rootAtT0, rootAtT1, cfg);
    }

    // Translation: partial-start + (n-1) full cycles + partial-end.
    const vec3 partialStart = cycleAnchorEnd.position - rootAtT0.position;
    const vec3 perCycle     = cycleAnchorEnd.position - cycleAnchorStart.position;
    const vec3 partialEnd   = rootAtT1.position       - cycleAnchorStart.position;
    const vec3 rawTrans = partialStart
                        + perCycle * static_cast<float>(cyclesCrossed - 1)
                        + partialEnd;

    Delta out;
    out.translation = ProjectTranslation(rawTrans, cfg.axisMask);

    // Rotation: same composition but multiplicative. Quaternion deltas
    // chain right-to-left in our convention: qTotal = qPartialEnd *
    // (qPerCycle ^ (n-1)) * qPartialStart, where each segment is the
    // delta from start of segment to end of segment.
    //
    // For yaw-only extraction the multiplications commute (same axis), so
    // ordering is academic — but we keep the explicit composition to
    // mirror the translation arithmetic and to make a future "extract
    // pitch + roll too" generalisation a single-line change.
    const Quaternion qPartialStart = cycleAnchorEnd.rotation * rootAtT0.rotation.inverse();
    const Quaternion qPerCycle     = cycleAnchorEnd.rotation * cycleAnchorStart.rotation.inverse();
    const Quaternion qPartialEnd   = rootAtT1.rotation       * cycleAnchorStart.rotation.inverse();

    Quaternion qFullCycles = Quaternion::identity();
    for (int i = 0; i < cyclesCrossed - 1; ++i) {
        qFullCycles = qPerCycle * qFullCycles;
    }
    const Quaternion qDelta = qPartialEnd * qFullCycles * qPartialStart;
    out.rotation = ProjectRotation(qDelta, cfg);
    return out;
}

// ----------------------------------------------------------------------------
// Pose stripping
// ----------------------------------------------------------------------------

// Remove the extracted components from the root bone's pose. Call this
// AFTER you've extracted the delta and BEFORE the pose is fed to the
// skinning pipeline; otherwise the renderer applies the extracted motion
// twice (once via the entity transform, once via the bone position).
//
// Translation:
//   Zero the masked axes — what's left is the in-place vertical bob /
//   side-sway / whatever the artist intended to be visual-only.
// Rotation:
//   If RotationYaw is set, divide out the yaw twist so the bone keeps
//   only its swing component (forward lean, sideways tilt). If the mask
//   is empty, the rotation is left untouched.
inline void StripFromPose(Transform& rootBonePose, const Config& cfg) {
    // Translation: keep components NOT in the mask.
    if (cfg.axisMask & AxisFlags::TranslationX) rootBonePose.position.x = 0.0f;
    if (cfg.axisMask & AxisFlags::TranslationY) rootBonePose.position.y = 0.0f;
    if (cfg.axisMask & AxisFlags::TranslationZ) rootBonePose.position.z = 0.0f;

    if (cfg.axisMask & AxisFlags::RotationYaw) {
        const Quaternion twist = ExtractTwist(rootBonePose.rotation, cfg.upAxis);
        // qSwing = q * qTwist^{-1}. This decomposes the rotation into the
        // part orthogonal to the axis (swing — kept on the bone) and the
        // part parallel to the axis (twist — moved to the entity).
        rootBonePose.rotation = rootBonePose.rotation * twist.inverse();
        rootBonePose.rotation.normalize();
    }
}

}  // namespace RootMotion
}  // namespace Engine

#endif  // ENGINE_ROOT_MOTION_HPP

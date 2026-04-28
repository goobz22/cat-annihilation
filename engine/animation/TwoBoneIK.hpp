#ifndef ENGINE_TWO_BONE_IK_HPP
#define ENGINE_TWO_BONE_IK_HPP

// ============================================================================
// Two-bone inverse kinematics (analytic, law-of-cosines).
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Two-bone IK for foot placement".
//
// Why analytic and not CCD / FABRIK?
//
// For a three-joint chain (upper, lower, end — think hip→knee→ankle or
// shoulder→elbow→wrist), the geometry is fully constrained by the law of
// cosines plus a pole vector that resolves the single remaining degree of
// freedom (which side the elbow / knee bends toward). Iterative solvers are
// overkill here — they cost more CPU, can oscillate, and don't give you the
// "exact reach or clamp to max extension" semantics we actually want for
// foot placement on uneven terrain. CCD / FABRIK earn their keep on longer
// chains (fingers, tails, spines); the legs of the cat and the dogs are all
// two-bone chains.
//
// What this kernel solves:
//
//   Given rest-pose positions of three joints (a, b, c) and a desired
//   end-effector target t, plus a pole target p that tells the mid joint
//   which way to bend, compute new positions b' and c' such that:
//
//     1. The limb lengths are preserved: |b'-a| == |b-a|, |c'-b'| == |c-b|.
//     2. c' == t when t is inside the limb's reach envelope (|t-a| ≤ L1+L2).
//     3. When t is out of reach, c' lands on the line from a toward t at
//        distance (L1+L2), and the chain fully extends (reached=false).
//     4. b' lies on the side of line (a,t) that is closest to p, so the
//        knee points forward, the elbow points backward, etc.
//
// The kernel operates on bare vec3 positions — no Transform or Skeleton
// coupling — so it is straightforward to unit-test against known geometries
// and to integrate into any skinning pipeline (animation blender, physics
// ragdoll retargeter, foot-IK pass at the end of the animation stage).
//
// Coordinate system: the solver is coordinate-agnostic. Feed it positions in
// any single consistent frame (world space is typical for foot-placement; a
// chain-root-local space is typical for reach animations). The returned
// positions come out in the same frame you fed in.
//
// References: "Programming Game AI by Example" (Buckland 2005) §7 and the
// classical limb-IK derivation used in every major engine (Unreal's
// AnimNode_TwoBoneIK, Unity's Animation Rigging TwoBoneIKConstraint,
// Godot's SkeletonModification2DTwoBoneIK). The difference in this
// implementation is the explicit out-of-reach handling and the pole-
// degeneracy fallback — both are real issues that show up the first time a
// designer yanks the IK target past the limb's reach envelope.
// ============================================================================

#include "../math/Vector.hpp"
#include "../math/Quaternion.hpp"
#include "../math/Math.hpp"
#include <cmath>

namespace Engine {
namespace TwoBoneIK {

// ----------------------------------------------------------------------------
// Inputs & outputs
// ----------------------------------------------------------------------------

/**
 * Rest-pose description of the three-joint chain, in any consistent frame.
 *
 * a, b, c: positions of upper / mid / end-effector joints in rest pose.
 * pole:    position of the pole target — the bend-direction hint. The mid
 *          joint (b) will be pushed toward the side of line(a, target) that
 *          contains this pole. Typical placements: in front of the knee for
 *          legs, behind the elbow for arms. The pole only needs to be a
 *          *direction* hint; its magnitude/distance doesn't matter as long
 *          as it is not collinear with (target - a).
 */
struct Chain {
    vec3 a;     // upper joint (hip / shoulder)
    vec3 b;     // mid joint (knee / elbow)
    vec3 c;     // end-effector (ankle / wrist)
    vec3 pole;  // pole target position (bend-direction hint)
};

/**
 * Result of the solve.
 *
 * newB, newC: solved world positions of the mid joint and end-effector.
 * reached:    true if the target was inside the limb's reach envelope and
 *             newC == target within float tolerance. false if the chain
 *             had to be extended to its full length toward the target.
 * upperLen, lowerLen: limb lengths that were preserved. Exposed so callers
 *             can sanity-check or drive visualization / debug overlays.
 */
struct Solution {
    vec3 newB;
    vec3 newC;
    bool reached;
    float upperLen;
    float lowerLen;
};

// ----------------------------------------------------------------------------
// Internal helper: rotation-from-to (shortest-arc quaternion between two
// unit vectors). Exposed in the TwoBoneIK::detail namespace so unit tests
// can exercise its degenerate paths directly — this helper shows up again
// when callers want to lift solved positions back into bone-local rotations.
// ----------------------------------------------------------------------------

namespace detail {

// Shortest-arc rotation that takes unit vector 'from' to unit vector 'to'.
// Handles the two degenerate cases: aligned (returns identity) and
// anti-parallel (returns a 180° rotation around any axis perpendicular to
// 'from'). The anti-parallel fallback matters: a naive cross(from, to) is
// zero in that case, which would produce a NaN quaternion.
inline Quaternion RotationFromTo(const vec3& from, const vec3& to) {
    // Assume inputs are normalized; guard anyway with a length-squared test
    // so callers can pass zero-length vectors without crashing — a stiff
    // bone with length 0 should be a no-op, not a crash.
    float fromLenSq = from.lengthSquared();
    float toLenSq = to.lengthSquared();
    if (fromLenSq < Math::EPSILON || toLenSq < Math::EPSILON) {
        return Quaternion::identity();
    }

    vec3 f = from.normalized();
    vec3 t = to.normalized();
    float d = f.dot(t);

    // Aligned (or near enough): identity.
    if (d > 1.0f - Math::EPSILON) {
        return Quaternion::identity();
    }

    // Anti-parallel: 180° around any axis perpendicular to 'from'.
    // Pick the world axis least aligned with 'from' to build a stable
    // perpendicular (Hughes-Moller 1999 technique).
    if (d < -1.0f + Math::EPSILON) {
        vec3 axis;
        if (std::abs(f.x) < 0.9f) {
            axis = vec3(1.0f, 0.0f, 0.0f).cross(f).normalized();
        } else {
            axis = vec3(0.0f, 1.0f, 0.0f).cross(f).normalized();
        }
        return Quaternion::fromAxisAngle(axis, Math::PI);
    }

    // Generic case: rotation axis = f × t, angle = acos(d).
    vec3 axis = f.cross(t).normalized();
    float angle = std::acos(d);
    return Quaternion::fromAxisAngle(axis, angle);
}

} // namespace detail

// ----------------------------------------------------------------------------
// The solver
// ----------------------------------------------------------------------------

/**
 * Solve for the end-effector reaching 'target' under the pole constraint.
 *
 * Algorithm (law of cosines):
 *
 *   1. Measure the rest limb lengths L1=|b-a| and L2=|c-b|. These are
 *      preserved by the solve — the IK moves joints, it doesn't stretch.
 *
 *   2. Compute D = |target - a|. If D > L1+L2 the target is out of reach;
 *      clamp to maxExtension (L1+L2 - tiny) and set reached=false so the
 *      caller can decide what to do (e.g., let the chain hyper-extend
 *      visually, or ignore the IK this frame).
 *
 *   3. Build an orthonormal basis at 'a':
 *        dirAT = normalize(target - a)       — along the shoulder→target axis.
 *        poleProj = (pole - a) − ((pole - a)·dirAT) dirAT
 *                                             — pole projected onto the
 *                                                plane perpendicular to dirAT.
 *        bendDir = normalize(poleProj)        — the bend plane's "up" vector.
 *
 *      Degeneracy: if the pole is (nearly) collinear with dirAT the
 *      projection has zero length. We fall back to any stable perpendicular
 *      derived from dirAT so the solve still produces a valid (if
 *      arbitrary) bend direction rather than NaN.
 *
 *   4. Law of cosines at the shoulder joint:
 *        cos(alpha) = (L1² + D² − L2²) / (2 L1 D).
 *      alpha is the interior angle between dirAT and the upper bone.
 *
 *   5. Solved positions in the (dirAT, bendDir) plane:
 *        b' = a + dirAT · (L1 cos α) + bendDir · (L1 sin α)
 *        c' = target (or clamped extension point if unreachable).
 *
 * Complexity: O(1). No iteration, no allocation.
 */
inline Solution Solve(const Chain& chain, const vec3& target) {
    Solution out{};
    out.upperLen = (chain.b - chain.a).length();
    out.lowerLen = (chain.c - chain.b).length();

    // Degenerate chain: zero-length limb. Return the rest pose unchanged so
    // callers can't produce NaN joint positions just because someone forgot
    // to seed the skeleton. reached=false communicates "we couldn't solve".
    if (out.upperLen < Math::EPSILON || out.lowerLen < Math::EPSILON) {
        out.newB = chain.b;
        out.newC = chain.c;
        out.reached = false;
        return out;
    }

    const float maxReach = out.upperLen + out.lowerLen;

    vec3 toTarget = target - chain.a;
    float originalD = toTarget.length();

    // Degenerate target: target == a. Can't extract a direction. Leave the
    // rest pose alone and flag as unreached — the caller chose a nonsensical
    // IK target. This has to run before the reach-clamp so we don't later
    // divide by zero when computing dirAT.
    if (originalD < Math::EPSILON) {
        out.newB = chain.b;
        out.newC = chain.c;
        out.reached = false;
        return out;
    }

    // dirAT is computed from the ORIGINAL toTarget vector, not from the
    // clamped distance. If we divided by the clamped D instead, the
    // resulting "direction" would inherit the ratio (originalD / clampedD)
    // as a spurious magnitude — e.g. target at 10 units with maxReach=2
    // would produce a 5× vector, which breaks every downstream law-of-
    // cosines term. Normalize once, then work in the (unit dirAT, D) frame.
    vec3 dirAT = toTarget / originalD;

    // Out-of-reach handling. We cap at maxReach - EPSILON rather than exactly
    // maxReach so the law-of-cosines cos(alpha) doesn't bottom out at 1.0
    // (which would be a fully straight limb — fine geometrically, but
    // downstream `acos(1.0)` + `sin(alpha)=0` silently zero out the bendDir
    // contribution, masking the degeneracy). Keeping epsilon slack means the
    // chain is "almost" fully extended and the bend plane stays well-defined.
    float D = originalD;
    bool reached = true;
    if (D > maxReach - Math::EPSILON) {
        D = maxReach - Math::EPSILON;
        reached = false;
    }

    // Pole projection onto the plane perpendicular to dirAT.
    vec3 poleDir = chain.pole - chain.a;
    vec3 poleProj = poleDir - dirAT * poleDir.dot(dirAT);
    float poleProjLenSq = poleProj.lengthSquared();

    vec3 bendDir;
    if (poleProjLenSq < Math::EPSILON) {
        // Pole is on the shoulder-target line. Pick any stable perpendicular
        // to dirAT. Hughes-Moller: cross with whichever world axis is least
        // aligned with dirAT. This is deterministic, which matters for unit
        // tests — two runs with the same degenerate input produce the same
        // bend direction instead of drifting with floating-point chatter.
        vec3 axis = (std::abs(dirAT.x) < 0.9f) ? vec3(1.0f, 0.0f, 0.0f)
                                               : vec3(0.0f, 1.0f, 0.0f);
        bendDir = axis.cross(dirAT).normalized();
    } else {
        bendDir = poleProj / std::sqrt(poleProjLenSq);
    }

    // Law of cosines at the shoulder joint.
    // cos(alpha) = (L1² + D² − L2²) / (2 L1 D). Clamp explicitly — floating
    // point error can push the ratio a hair past ±1 when the target sits
    // exactly at the reach boundary, and acos(1.0000001) is NaN.
    const float L1 = out.upperLen;
    const float L2 = out.lowerLen;
    float cosAlphaNum = L1 * L1 + D * D - L2 * L2;
    float cosAlphaDen = 2.0f * L1 * D;
    float cosAlpha = cosAlphaNum / cosAlphaDen;
    if (cosAlpha > 1.0f) cosAlpha = 1.0f;
    if (cosAlpha < -1.0f) cosAlpha = -1.0f;
    float sinAlpha = std::sqrt(1.0f - cosAlpha * cosAlpha);

    out.newB = chain.a + dirAT * (L1 * cosAlpha) + bendDir * (L1 * sinAlpha);
    out.newC = reached ? target : (chain.a + dirAT * maxReach);
    out.reached = reached;
    return out;
}

// ----------------------------------------------------------------------------
// Lift solved positions back into world-space rotation deltas.
// ----------------------------------------------------------------------------

/**
 * World-space rotation deltas that take the rest-pose chain to the solved
 * chain, computed from position deltas. Useful when the caller keeps bones
 * in local rotation form and needs to post-multiply a world-space delta into
 * each bone's parent-relative rotation.
 *
 *   upperDelta: rotation that takes (b - a) → (newB - a).
 *               Applied (world-space pre-multiply) to the upper bone's
 *               world rotation gives the solved upper rotation.
 *   lowerDelta: rotation that takes
 *               upperDelta·(c - b)   →   (newC - newB).
 *               Note the upperDelta prefix: after the upper bone rotates,
 *               the lower bone's rest direction has already been swung;
 *               the lower delta only needs to cover the remaining swing.
 *
 * This function does NOT update local rotations directly — the caller owns
 * the skeleton frame mapping. It returns the two world-space rotations and
 * leaves parent-relative conversion to the site that actually knows the
 * parent world transforms.
 */
struct RotationDeltas {
    Quaternion upperDelta;
    Quaternion lowerDelta;
};

inline RotationDeltas ComputeRotationDeltas(const Chain& chain,
                                            const Solution& solution) {
    RotationDeltas out{};

    vec3 restUpper = chain.b - chain.a;
    vec3 solvedUpper = solution.newB - chain.a;
    out.upperDelta = detail::RotationFromTo(restUpper, solvedUpper);

    // Rotate the rest lower-bone direction by the upper delta first; the
    // lower delta only covers the leftover swing. This matches how the
    // world-space rotation composes when you apply upperDelta to the upper
    // bone and then ask "what rotation must the lower bone pick up, in its
    // new world frame, to reach newC?".
    vec3 restLower = chain.c - chain.b;
    vec3 rotatedLowerRest = out.upperDelta.rotate(restLower);
    vec3 solvedLower = solution.newC - solution.newB;
    out.lowerDelta = detail::RotationFromTo(rotatedLowerRest, solvedLower);

    return out;
}

} // namespace TwoBoneIK
} // namespace Engine

#endif // ENGINE_TWO_BONE_IK_HPP

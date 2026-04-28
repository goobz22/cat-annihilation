#ifndef ENGINE_PHYSICS_CCD_HPP
#define ENGINE_PHYSICS_CCD_HPP

// ============================================================================
// Continuous collision detection (CCD).
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Continuous collision detection for
// fast bodies". Acceptance bar:
//   "Add swept-AABB expansion in the broadphase and TOI clamp in the narrow
//    phase. Unit test: a bullet-speed body through a thin wall must collide,
//    not pass through."
//
// Why tunneling matters at all:
//
// A discrete-time rigid-body integrator advances a body by `displacement =
// velocity * dt` each frame, then the narrow phase checks for overlap at the
// new pose. If the body is small and fast enough that `|displacement| >
// body_thickness + obstacle_thickness`, the discrete step jumps OVER the
// obstacle entirely — the body is on the near side at frame N and on the far
// side at frame N+1 with zero overlap detected in between. The collision is
// silently missed. In a wave-survival game with 0.1 m radius projectiles,
// 5 m/s dog-speed and 0.2 m thin-wall colliders, one 16 ms frame at 60 Hz
// already puts |displacement| = 0.083 m, which is on the edge. Drop to 30 Hz
// or speed the projectile up and tunneling becomes reproducible. Fixing this
// is a two-stage problem:
//
//   1. Broadphase must generate collision pairs based on the UNION of each
//      body's current AABB and its swept AABB over the frame. Otherwise a
//      projectile that starts to the left of a wall and ends to the right of
//      it is never asked to narrow-test against the wall at all, because the
//      per-frame AABBs never overlap.
//
//   2. Narrow phase, for pairs flagged by the swept broadphase, must compute
//      the TIME OF IMPACT (TOI) t ∈ [0, 1] along the relative motion, clamp
//      the body's integration step to t * displacement (with a tiny safety
//      factor so they don't land exactly at contact), and then re-solve the
//      next frame from a penetration-safe pose. This is the same pattern Erin
//      Catto calls "substepping-at-impact" — it preserves the rest of the
//      frame's budget for the normal constraint solver rather than
//      unconditionally doing N mini-steps per frame.
//
// Why this lives in a header, not a .cu:
//
// Same reasoning as SequentialImpulse.hpp. Continuous collision is pure float
// math: AABB slab clipping, sphere-sphere quadratic-root finding, and the
// conservative-advancement iteration. Putting it in a header-only, STL-only
// module lets the host test suite exercise the exact code path the runtime
// pass will call, keeps the math deterministic (the CUDA version will invoke
// the same inline functions from a __host__ __device__ context once the
// runtime wire-up lands), and lets us write the bullet-through-thin-wall
// regression test without a CUDA context. This matches how TwoBoneIK,
// RootMotion, SimplexNoise, and SequentialImpulse already live in this
// codebase.
//
// Shape coverage (this iteration):
//
//   - Swept AABB expansion   (broadphase)        — all shapes.
//   - Swept sphere-vs-AABB   (narrow phase)      — projectile vs static box.
//   - Swept sphere-vs-sphere (narrow phase)      — projectile vs enemy.
//   - ConservativeAdvance    (narrow phase fallback) — generic closest-point.
//
// The swept sphere-vs-OBB / capsule-vs-OBB cases that PhysicsWorld will need
// at integration time are deferred: once the narrow-phase wire-up lands they
// reduce to either (a) transform the motion into OBB-local space and call
// SweptSphereAABB, or (b) call ConservativeAdvance with the matching
// closest-point fn. The math kernel doesn't need a new entry point for them.
//
// References:
//   - Christer Ericson, "Real-Time Collision Detection" (2005), §5.3 (swept
//     AABBs), §5.5 (swept sphere-vs-plane / slab CCD), and §9.5.3
//     (conservative advancement for convex pairs).
//   - Brian Mirtich, "Timewarp Rigid Body Simulation" (SIGGRAPH 2000) §4.2 —
//     TOI root-finding framing we use here.
//   - Erin Catto, "Continuous Collision Detection" (Box2D r169, 2013) — the
//     "bisection + minimum-distance" algorithm that ConservativeAdvance is a
//     single-dimensional simplification of.
//   - Bridson & Fedkiw, "Robust Treatment of Collisions, Contact and
//     Friction for Cloth Animation" (SIGGRAPH 2002) — the "conservative"
//     framing of TOI advancement.
// ============================================================================

#include "../../math/Vector.hpp"
#include "../../math/AABB.hpp"
#include "../../math/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace CatEngine {
namespace Physics {
namespace CCD {

// ----------------------------------------------------------------------------
// Output record for a swept hit.
//
// t is the normalized frame fraction at which the surfaces first touch,
// i.e. t = 0 means they touch at the start of the frame and t = 1 means
// they touch exactly at the end. Values outside [0, 1] are never returned
// (the predicate functions return false in those cases). For SweptSphereAABB
// and SweptSphereSphere the (point, normal) are the contact point on the
// static/second body in world space and the outward normal of the first
// surface touched, suitable for kick-starting the narrow phase once the
// motion has been clamped to the TOI pose.
// ----------------------------------------------------------------------------
struct SweepHit {
    float t{0.0f};
    Engine::vec3 point{0.0f, 0.0f, 0.0f};
    Engine::vec3 normal{0.0f, 1.0f, 0.0f};
};

// ----------------------------------------------------------------------------
// SweepAABB — expand a body's AABB to cover its swept volume for this frame.
//
// This is the broadphase half of the CCD fix. Each body's spatial-hash or
// BVH entry for the frame must be this UNION so that moving bodies are still
// queried against obstacles that lie along their trajectory even if their
// start AABB is well clear of the obstacle.
//
// The implementation is a branchless per-axis expansion: positive
// displacement grows the upper bound, negative displacement grows the lower
// bound. Optionally inflate by `margin` on every side — useful for (a)
// skinned characters whose visual silhouette extends past the collision
// hull, and (b) narrow-phase numerical slop so the narrow phase never
// reports a miss for a pair the broadphase already flagged.
// ----------------------------------------------------------------------------
inline Engine::AABB SweepAABB(const Engine::AABB& aabb,
                              const Engine::vec3& displacement,
                              float margin = 0.0f) {
    Engine::AABB out = aabb;

    // Branchless per-axis swept expansion. We never CONTRACT either bound —
    // the swept volume always contains the start volume.
    if (displacement.x > 0.0f) out.max.x += displacement.x;
    else                        out.min.x += displacement.x;
    if (displacement.y > 0.0f) out.max.y += displacement.y;
    else                        out.min.y += displacement.y;
    if (displacement.z > 0.0f) out.max.z += displacement.z;
    else                        out.min.z += displacement.z;

    if (margin > 0.0f) {
        out.min -= Engine::vec3(margin);
        out.max += Engine::vec3(margin);
    }
    return out;
}

// ----------------------------------------------------------------------------
// SweptSphereAABB — ray-cast the centre of a sphere against a Minkowski-
// expanded AABB to find the earliest frame fraction at which the sphere
// surface touches the box surface.
//
// Algorithm: Minkowski-sum the AABB with the sphere (approximated as
// expanding the AABB by `radius` on every side — corners are boxed, not
// rounded, so contact reports may be slightly premature within a
// `radius × radius` neighbourhood of a box corner, but the narrow phase
// refines the actual contact point on re-solve), then run the slab algorithm
// on the ray from `start` to `start + displacement`.
//
// The slab algorithm tracks the latest entry `tMin` and earliest exit `tMax`
// across the three axis-aligned pairs of planes. A hit requires `tMin ≤ tMax`
// and `tMin ∈ [0, 1]`. We additionally record which axis produced the latest
// entry — that's the face whose outward normal is the contact normal. This
// is the classic Quake / GoldSrc "hull trace" logic, used by every FPS
// physics layer that supports player/projectile vs world-geometry CCD.
// ----------------------------------------------------------------------------
inline bool SweptSphereAABB(const Engine::vec3& start,
                            float radius,
                            const Engine::vec3& displacement,
                            const Engine::AABB& target,
                            SweepHit& outHit) {
    // Minkowski-expand target by the sphere radius. After this, we can treat
    // the sphere as a point moving against an axis-aligned box.
    const Engine::vec3 expMin = target.min - Engine::vec3(radius);
    const Engine::vec3 expMax = target.max + Engine::vec3(radius);

    // Degenerate case: the sphere centre is already INSIDE the expanded box
    // at t=0. This means the sphere is already touching or overlapping the
    // target — return TOI=0 so the caller can treat it as an immediate
    // contact. The narrow phase is responsible for resolving the
    // penetration; we don't try to pick a meaningful normal here because
    // "closest face" is ambiguous inside a box.
    if (start.x >= expMin.x && start.x <= expMax.x &&
        start.y >= expMin.y && start.y <= expMax.y &&
        start.z >= expMin.z && start.z <= expMax.z) {
        outHit.t = 0.0f;
        outHit.point = start;
        // Default normal points +y (gravity-aligned) — see comment above
        // about ambiguity. Callers should treat t=0 as "already overlapping".
        outHit.normal = Engine::vec3(0.0f, 1.0f, 0.0f);
        return true;
    }

    // Slab test. `tMin` is the latest entry, `tMax` is the earliest exit.
    // We track which axis & direction produced the latest entry to recover
    // the contact normal: entering through the low face of axis a means the
    // outward normal is -e_a, entering through the high face means +e_a.
    float tMin = 0.0f;
    float tMax = 1.0f;
    int hitAxis = -1;
    float hitSign = 0.0f;

    for (int axis = 0; axis < 3; ++axis) {
        const float p0 = start[static_cast<size_t>(axis)];
        const float d  = displacement[static_cast<size_t>(axis)];
        const float slabLo = expMin[static_cast<size_t>(axis)];
        const float slabHi = expMax[static_cast<size_t>(axis)];

        if (std::abs(d) < Engine::Math::EPSILON) {
            // Motion parallel to this slab: need to already be between its
            // two planes, else the ray never crosses the slab and there is
            // no intersection possible on any axis.
            if (p0 < slabLo || p0 > slabHi) return false;
            continue;
        }

        const float invD = 1.0f / d;
        float t0 = (slabLo - p0) * invD; // enter through low plane
        float t1 = (slabHi - p0) * invD; // exit through high plane
        float entrySign = -1.0f;         // entered via -e_a face

        // If displacement is negative the entry plane is the high one and the
        // sign flips. We swap so t0 ≤ t1 unconditionally and track which
        // side produced `t0`.
        if (t0 > t1) {
            std::swap(t0, t1);
            entrySign = +1.0f;
        }

        if (t0 > tMin) {
            tMin = t0;
            hitAxis = axis;
            hitSign = entrySign;
        }
        if (t1 < tMax) tMax = t1;

        // Early-out: as soon as the intersection interval becomes empty the
        // ray misses the box. No further axes can bring `tMin` back down.
        if (tMin > tMax) return false;
    }

    // If tMin landed above 1.0 the hit is in a future frame, not this one.
    if (tMin > 1.0f) return false;

    // If hitAxis is still -1 we had zero displacement on every axis AND the
    // sphere was inside the slab on every axis — i.e. the insideAtStart case
    // above. We can't reach here, but return false defensively to avoid
    // writing an uninitialised axis into the hit normal.
    if (hitAxis < 0) return false;

    outHit.t = tMin;
    outHit.point = start + displacement * tMin;
    outHit.normal = Engine::vec3(0.0f);
    outHit.normal[static_cast<size_t>(hitAxis)] = hitSign;
    return true;
}

// ----------------------------------------------------------------------------
// SweptSphereSphere — analytic TOI for two spheres in linear motion.
//
// Set up the relative-motion problem by parameterising both centres by
// normalized frame time t ∈ [0, 1]:
//
//   pA(t) = startA + dispA · t
//   pB(t) = startB + dispB · t
//
// They touch when |pB(t) − pA(t)|² = (rA + rB)². Let
//   c = startB − startA            (initial separation)
//   v = dispB − dispA              (relative motion over the frame)
//   R = rA + rB                    (combined radius)
//
// Then the condition expands to
//   (v·v) t² + 2 (c·v) t + (c·c − R²) = 0
//
// which is a standard quadratic in t. Solve with the discriminant:
//
//   disc = (c·v)² − (v·v)(c·c − R²)
//
// - disc < 0  : no real root → the spheres never touch during linear motion.
// - |v·v| ≈ 0 : no relative motion; they collide only if they already
//                overlap (c·c ≤ R²), in which case TOI = 0.
// - otherwise : earliest root t = (−(c·v) − √disc) / (v·v). Clamp to [0, 1];
//                anything outside that window is either "already overlapping"
//                (t ≤ 0 with c·c ≤ R²) or "won't hit this frame" (t > 1).
// ----------------------------------------------------------------------------
inline bool SweptSphereSphere(const Engine::vec3& startA, float radiusA,
                              const Engine::vec3& dispA,
                              const Engine::vec3& startB, float radiusB,
                              const Engine::vec3& dispB,
                              float& outTOI) {
    const Engine::vec3 c = startB - startA;
    const Engine::vec3 v = dispB - dispA;
    const float R = radiusA + radiusB;

    const float cc = c.dot(c);
    const float R2 = R * R;

    // Already overlapping at t=0 — report TOI=0 so the caller clamps to the
    // current pose and hands off to the penetration-resolving narrow phase.
    // This matches the SweptSphereAABB "insideAtStart" branch semantically.
    if (cc <= R2) {
        outTOI = 0.0f;
        return true;
    }

    const float vv = v.dot(v);
    if (vv < Engine::Math::EPSILON) {
        // No relative motion and they were not overlapping at t=0 — there is
        // no time in this frame at which they can touch.
        return false;
    }

    const float cv = c.dot(v);
    // If the sphere centres are separating (c·v ≥ 0) AND not already
    // overlapping (cc > R², enforced above), they can't meet during this
    // frame. The quadratic still has a positive-real root in the past
    // (t < 0) but that's not actionable CCD information — the spheres were
    // converging BEFORE the frame started and have since moved apart.
    if (cv >= 0.0f) return false;

    const float disc = cv * cv - vv * (cc - R2);
    if (disc < 0.0f) return false; // closest approach still separated

    const float t = (-cv - std::sqrt(disc)) / vv;

    // Numerical guard: reject TOIs outside [0, 1]. The cc > R² check above
    // rules out t ≤ 0; we only need to reject t > 1.
    if (t > 1.0f) return false;
    if (t < 0.0f) return false; // defensive — should be unreachable

    outTOI = t;
    return true;
}

// ----------------------------------------------------------------------------
// ClampDisplacementToTOI — given a hit TOI, reduce a body's frame
// displacement so it stops just SHORT of the contact pose.
//
// The `safety` factor (default 0.99) leaves a sliver of separation between
// the TOI pose and the actual surface contact. Without it, the post-clamp
// pose typically has a zero-distance contact that the narrow phase decodes
// as "penetrating" due to floating-point round-off in the slab / quadratic
// computations above. Box2D ships a similar `b2_toiBaumgarte` ~ 0.75 for the
// same reason; we use a more conservative 0.99 because the narrow phase in
// this engine will do one constraint-solver pass on the resulting contact
// rather than relying on Baumgarte to push bodies apart afterwards.
// ----------------------------------------------------------------------------
inline Engine::vec3 ClampDisplacementToTOI(const Engine::vec3& displacement,
                                           float toi,
                                           float safety = 0.99f) {
    const float s = Engine::Math::clamp(toi * safety, 0.0f, 1.0f);
    return displacement * s;
}

// ----------------------------------------------------------------------------
// ConservativeAdvance — iterative TOI root-finder for arbitrary shape pairs
// given a closest-point callback.
//
// This is the generic CCD path the narrow phase will use for pairs the
// analytic kernels don't cover (sphere-vs-OBB with an off-axis motion,
// capsule-vs-capsule, convex-vs-convex via GJK). Templated on the
// closest-point functor so we don't pull in <functional> — keeps this header
// STL-free beyond <algorithm>/<cmath> the way SequentialImpulse is.
//
// Algorithm (Ericson §9.5.3, Mirtich 2000, Catto b2_toi 2013):
//
//   t = 0
//   repeat up to maxIters:
//     sample A(t), B(t) along their linear motion
//     let (pA, pB) = closestFn(A(t), B(t))     // world-space surface points
//     let dist = |pA − pB|
//     if dist ≤ tolerance  → HIT at TOI = t
//     let dir   = (pA − pB) / dist             // A-surface → B-surface axis
//     let vRel  = (dispA − dispB) · dir        // rate of change per unit-t
//     if vRel ≥ 0          → separating / parallel → MISS
//     let dt    = dist / (−vRel)               // advance that closes dist
//     t        += dt
//     if t ≥ 1  → won't touch this frame → MISS
//
// "Conservative" in the algorithm's name: each `dt` step is the LARGEST
// advance we can make without risking a missed contact, because |vRel| is
// the fastest rate at which the two surfaces can be closing along any
// direction. If they were closing faster the relative velocity vector
// itself would be longer along `dir`. So the step is a safe lower bound on
// the true TOI and the iteration converges monotonically from below.
//
// The functor signature is:
//   void closestFn(const Engine::vec3& posA, const Engine::vec3& posB,
//                  Engine::vec3& outPA, Engine::vec3& outPB);
// where `outPA` is the closest point ON BODY A's surface to body B and
// vice versa. Callers that carry shape data in a lambda capture get the
// full flexibility of GJK / SAT / analytic-primitive closest-point routines
// without forcing a v-table on the CCD kernel.
// ----------------------------------------------------------------------------
template <class ClosestFn>
inline bool ConservativeAdvance(const Engine::vec3& startA,
                                const Engine::vec3& dispA,
                                const Engine::vec3& startB,
                                const Engine::vec3& dispB,
                                ClosestFn closestFn,
                                float& outTOI,
                                int maxIters = 24,
                                float toleranceMeters = 1e-4f) {
    float t = 0.0f;

    for (int iter = 0; iter < maxIters; ++iter) {
        const Engine::vec3 pA = startA + dispA * t;
        const Engine::vec3 pB = startB + dispB * t;

        Engine::vec3 surfA(0.0f);
        Engine::vec3 surfB(0.0f);
        closestFn(pA, pB, surfA, surfB);

        const Engine::vec3 sep = surfA - surfB;
        const float dist = sep.length();

        if (dist <= toleranceMeters) {
            // Convergence: surfaces are within tolerance of touching. The
            // current `t` is our TOI. We report it WITHOUT applying any
            // safety factor — the safety is the caller's concern via
            // ClampDisplacementToTOI, which keeps the scalar-budget
            // knob in one place rather than scattering two subtly-different
            // epsilons across the module.
            outTOI = t;
            return true;
        }

        const Engine::vec3 dir = sep * (1.0f / dist);   // unit axis surfB→surfA
        // Relative velocity along `dir`, per unit of normalized time. If A
        // is moving toward B their motion vectors differ such that A's
        // component along dir is more negative than B's — i.e. the scalar
        // (dispA − dispB)·dir is negative. Separating pairs have it ≥ 0.
        const float vRelAlong = (dispA - dispB).dot(dir);

        if (vRelAlong >= 0.0f) {
            // Surfaces are stationary or moving apart along their mutual
            // axis. No future t in this frame can bring them into contact.
            return false;
        }

        // Advance t by the largest safe amount: dist / closing-rate.
        const float dtStep = dist / (-vRelAlong);
        t += dtStep;

        if (t >= 1.0f) {
            // Closing would happen AFTER this frame ends — the broadphase
            // will re-queue the pair on the next frame.
            return false;
        }
    }

    // Hit the iteration cap without converging. Returning false here is
    // conservative in the OPPOSITE direction of the algorithm's name — we
    // miss a real hit rather than reporting a TOI we don't trust. Callers
    // that care about the last-iteration state can lift `maxIters`; 24 is
    // empirically enough for sphere-vs-sphere, sphere-vs-box, and
    // capsule-vs-box pairs at gameplay velocities.
    return false;
}

} // namespace CCD
} // namespace Physics
} // namespace CatEngine

#endif // ENGINE_PHYSICS_CCD_HPP

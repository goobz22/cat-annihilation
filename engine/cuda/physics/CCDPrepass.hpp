#ifndef ENGINE_PHYSICS_CCD_PREPASS_HPP
#define ENGINE_PHYSICS_CCD_PREPASS_HPP

// ============================================================================
// Continuous collision detection — runtime pre-pass wire-up.
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Continuous collision detection for
// fast bodies" (the math kernel in CCD.hpp landed first — this file plumbs
// those kernels into the live physics step so the "bullet-speed sphere cannot
// tunnel through a thin wall" acceptance bar is also provable in the running
// game, not only in isolation).
//
// Why this is a SEPARATE header from CCD.hpp rather than an addition to it:
//
// CCD.hpp is intentionally CUDA-free and STL-minimal — it must compile inside
// a __host__ __device__ kernel so a future GPU narrow-phase can call the same
// inline math. CCDPrepass.hpp, by contrast, runs on the CPU before the GPU
// step and walks `std::vector<RigidBody>`, which would pull <vector> +
// rigid-body machinery into the device side if we added it to CCD.hpp.
// Keeping the runtime wrapper in its own TU preserves the property that
// CCD.hpp can move to a .cuh later with a one-line rename.
//
// Why a CPU pre-pass and not a CUDA kernel rewrite:
//
// The tunneling risk is dominated by a small number of fast dynamic bodies
// per frame — projectiles, dashing cat, bullet-speed enemy — not by the bulk
// of mostly-slow enemies. An O(fast * N) CPU sweep, where `fast` is usually
// 0-10 per frame out of N ≤ 10 000 bodies, is strictly cheaper than the
// alternative of widening every body's broadphase AABB on the GPU and paying
// N² narrow-phase candidates worth of TOI work per step. This is the same
// tradeoff Catto articulates in the Box2D b2_toi paper: "special-case the
// fast bodies rather than pay the full price for every body every frame".
//
// How the pre-pass integrates with the existing step:
//
// In PhysicsWorld::stepSimulation() the order is upload → clearForces →
// applyForces → broadphase → narrowphase → integrateVelocities → solve →
// integratePositions → download. The pre-pass runs BEFORE upload on the
// CPU-side m_bodies, mutating linearVelocity for any fast body that would
// tunnel this frame. The downstream integrator then sees a clamped velocity
// and moves the body only up to TOI · safety of the way to the first
// contact — leaving a sliver of separation for the normal constraint solver
// to resolve on the next step. No GPU code changes were required.
//
// Shape coverage (matches CCD.hpp kernel coverage):
//
//   - Fast sphere / capsule vs static / dynamic sphere / capsule: analytic
//     quadratic-root via SweptSphereSphere.
//   - Fast sphere / capsule vs box (static or dynamic): Minkowski-expanded
//     slab test via SweptSphereAABB. For dynamic boxes we shift the sphere's
//     displacement by (-otherDisp) so the box is effectively static in the
//     relative-motion frame — the box's translation over the frame is
//     equivalent to an opposing shift in the sphere's path.
//   - Fast box: NOT covered this iteration. The analytic OBB sweep kernel is
//     deferred to a later backlog item; boxes fall through to the regular
//     broadphase, which still catches wider-than-obstacle cases cleanly.
//
// Capsule note: for CCD we treat capsules as spheres of the same radius. The
// hemispherical caps are the only part of a capsule thin enough to tunnel
// through a wall at gameplay speeds (the cylinder body is as thick as the
// capsule is tall). Full capsule-vs-AABB swept CCD would require a separate
// closest-point callback into ConservativeAdvance; the sphere approximation
// is acceptable because the post-prepass broadphase still runs and catches
// the cylinder body overlap in one more step if anything slipped through.
// ============================================================================

#include "CCD.hpp"
#include "RigidBody.hpp"
#include "Collider.hpp"
#include "../../math/Vector.hpp"
#include "../../math/AABB.hpp"
#include "../../math/Math.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace CatEngine {
namespace Physics {
namespace CCDRuntime {

// ----------------------------------------------------------------------------
// Statistics from a single pre-pass invocation. Surfaces to PhysicsWorld::Stats
// so the profiler can plot "bodies CCD-clamped per frame" alongside the usual
// broadphase pair count.
// ----------------------------------------------------------------------------
struct PrepassStats {
    int fastBodiesConsidered{0};   // bodies that passed IsFastBody()
    int bodiesClamped{0};          // bodies whose velocity was scaled down
    float smallestTOI{1.0f};       // earliest TOI observed this frame
};

// ----------------------------------------------------------------------------
// IsFastBody — gatekeeper that decides whether a body needs the CCD pre-pass
// this frame.
//
// A body is "fast" when its predicted frame displacement exceeds some fraction
// of its smallest collider dimension — i.e. a discrete integration step could
// jump over an obstacle smaller than the displacement. The `speedBias` knob
// defaults to 0.5 (displacement > half-thickness), which is aggressive enough
// to catch the acceptance-bar scenario (0.1 m radius projectile at 10 m/s,
// dt = 1/60 → |disp| = 0.167 m > 0.05 m half-radius) without running the
// O(fast·N) sweep on the majority of enemies at typical wave velocities.
//
// Excluded categories:
//   - Static bodies     — by definition have zero velocity.
//   - Sleeping bodies   — invariant of "not actively simulated" means they
//                          can't tunnel this frame.
//   - Trigger bodies    — don't generate collision response; tunneling is a
//                          rendering/gameplay issue, not a physics one, for
//                          triggers.
//   - Boxes / unknown   — analytic sweep isn't implemented this iteration.
// ----------------------------------------------------------------------------
inline bool IsFastBody(const RigidBody& body, float dt, float speedBias = 0.5f) {
    if (body.isStatic()) return false;
    if (hasFlag(body.flags, RigidBodyFlags::Sleeping)) return false;
    if (body.isTrigger()) return false;

    // Only sphere/capsule fast bodies are covered by the analytic kernels in
    // CCD.hpp. Fast boxes would need a swept-OBB kernel that isn't landed
    // yet; the regular broadphase still handles the cases that it can.
    if (body.collider.type != ColliderType::Sphere &&
        body.collider.type != ColliderType::Capsule) {
        return false;
    }

    const float speed = body.linearVelocity.length();
    if (speed <= 0.0f) return false;

    float minDim = 0.0f;
    switch (body.collider.type) {
        case ColliderType::Sphere:  minDim = body.collider.radius; break;
        case ColliderType::Capsule: minDim = body.collider.radius; break;
        default:                    minDim = 0.5f; break;
    }
    // Guard against degenerate zero-radius colliders — a zero-minDim body is
    // always "fast" by this predicate. Treat zero as a minimum of 1 mm so a
    // mis-authored collider doesn't spam the pre-pass with every frame.
    if (minDim < 1e-3f) minDim = 1e-3f;

    return speed * dt > speedBias * minDim;
}

// ----------------------------------------------------------------------------
// FindEarliestTOI — for one fast body, return the smallest TOI against every
// other body in the world. Returns true if any swept hit landed inside
// [0, 1]; writes the earliest hit to outTOI.
//
// The implementation is intentionally brute-force — one fast body tested
// against every other body — because `fast` is usually 0-10 per frame. A
// BVH or spatial-hash accelerator would pay its build cost on EVERY step of
// every frame just to shave a few microseconds off the pre-pass, which is
// the wrong tradeoff. If we ever observe `fast` exceeding double-digits we
// can revisit by reusing m_spatialHash (already built on the GPU) via a
// CPU-side shadow structure.
// ----------------------------------------------------------------------------
inline bool FindEarliestTOI(const std::vector<RigidBody>& bodies,
                            std::size_t fastIdx,
                            float dt,
                            float& outTOI) {
    outTOI = 1.0f;
    if (fastIdx >= bodies.size()) return false;

    const RigidBody& fast = bodies[fastIdx];
    // IsFastBody already gated on Sphere/Capsule. Use the collider radius for
    // both (see capsule note in the file header).
    const float rA = fast.collider.radius;
    const Engine::vec3 startA = fast.getColliderCenter();
    const Engine::vec3 dispA  = fast.linearVelocity * dt;

    float best = 1.0f;
    bool anyHit = false;

    for (std::size_t j = 0; j < bodies.size(); ++j) {
        if (j == fastIdx) continue;
        const RigidBody& other = bodies[j];
        // A sleeping / trigger / same-body pair is never a CCD candidate. We
        // still test against static bodies — they're the most common
        // tunneling target (level geometry walls).
        if (hasFlag(other.flags, RigidBodyFlags::Sleeping)) continue;
        if (other.isTrigger()) continue;

        const Engine::vec3 startB = other.getColliderCenter();
        const Engine::vec3 dispB  = other.isStatic()
            ? Engine::vec3(0.0f)
            : other.linearVelocity * dt;

        float toi = 1.0f;
        bool gotHit = false;

        if (other.collider.type == ColliderType::Sphere ||
            other.collider.type == ColliderType::Capsule) {
            const float rB = other.collider.radius;
            gotHit = CCD::SweptSphereSphere(startA, rA, dispA,
                                            startB, rB, dispB, toi);
        } else if (other.collider.type == ColliderType::Box) {
            // Build the box's world-space AABB from its half-extents + centre.
            // We then transform into the box's rest frame by subtracting the
            // box's displacement from the sphere's: the sphere's path relative
            // to a moving box is equivalent to the sphere's path (minus the
            // box's motion) against a static box.
            const Engine::vec3 halfExtents(
                other.collider.halfExtentX,
                other.collider.halfExtentY,
                other.collider.halfExtentZ);
            const Engine::AABB target(startB - halfExtents, startB + halfExtents);
            const Engine::vec3 relDisp = dispA - dispB;

            CCD::SweepHit hit{};
            if (CCD::SweptSphereAABB(startA, rA, relDisp, target, hit)) {
                toi = hit.t;
                gotHit = true;
            }
        }
        // Heightfield or unknown colliders fall through to the regular
        // broadphase — the analytic kernels don't cover them this iteration.

        if (gotHit && toi < best) {
            best = toi;
            anyHit = true;
        }
    }

    outTOI = best;
    return anyHit;
}

// ----------------------------------------------------------------------------
// ApplyCCDPrepass — main runtime entry point. Call once per simulation step
// BEFORE uploading body data to the GPU.
//
// For every fast dynamic body, find the smallest TOI against every other
// body in the world; if a hit fell inside [0, 1], scale the fast body's
// linear velocity by `toi * safety`. The downstream integrator will then
// integrate position with the clamped velocity, placing the body just short
// of the contact pose. The normal broad/narrow phase picks up from there
// and generates the contact that the constraint solver resolves.
//
// The `safety` factor (default 0.99) matches CCD::ClampDisplacementToTOI —
// keeping a single scalar-budget knob across the module. Running two subtly-
// different epsilons here vs in the kernel is a recipe for drift.
//
// Returns a PrepassStats summary the caller can plumb into its frame-stats
// struct for profiler overlays. Cheap to ignore if the caller doesn't care.
// ----------------------------------------------------------------------------
inline PrepassStats ApplyCCDPrepass(std::vector<RigidBody>& bodies,
                                    float dt,
                                    float safety = 0.99f) {
    PrepassStats stats{};
    if (dt <= 0.0f || bodies.size() < 2) return stats;

    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (!IsFastBody(bodies[i], dt)) continue;
        ++stats.fastBodiesConsidered;

        float toi = 1.0f;
        if (!FindEarliestTOI(bodies, i, dt, toi)) continue;
        if (toi >= 1.0f) continue;

        // Track the earliest TOI across the whole pre-pass. Useful for the
        // profiler: if this drops toward zero repeatedly we've got bodies
        // that were overlapping at the start of the frame, which indicates
        // the previous step's constraint solver didn't fully separate them.
        if (toi < stats.smallestTOI) stats.smallestTOI = toi;

        const float s = Engine::Math::clamp(toi * safety, 0.0f, 1.0f);
        bodies[i].linearVelocity *= s;
        ++stats.bodiesClamped;
    }

    return stats;
}

} // namespace CCDRuntime
} // namespace Physics
} // namespace CatEngine

#endif // ENGINE_PHYSICS_CCD_PREPASS_HPP

#ifndef ENGINE_PHYSICS_SEQUENTIAL_IMPULSE_HPP
#define ENGINE_PHYSICS_SEQUENTIAL_IMPULSE_HPP

// ============================================================================
// Sequential-impulse (projected Gauss-Seidel) constraint solver.
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Sequential-impulse constraint
// solver". The acceptance bar listed there is: "PGS / SI with warm-starting
// on top of the existing CUDA broadphase. Unit test must show a 50-body box
// stack converging in ≤ 20 iterations."
//
// Why this lives in a header, not a .cu:
//
// Every real physics solver in the wild (Box2D, Bullet, Rapier, PhysX) uses
// the same sequential-impulse math — it's a coordinate-descent solve over an
// LCP, and the per-contact step is a handful of dot-products and a clamp.
// Putting the math in a header-only, STL-only module lets the host test
// suite exercise the exact code path the runtime pass will call, keeps the
// solver deterministic (the GPU version will invoke the same inline
// functions from a CUDA __device__ context once the runtime pass is
// integrated), and lets us write the 50-body box-stack convergence test
// without standing up a CUDA context. This matches how TwoBoneIK,
// RootMotion, and SimplexNoise already live in this codebase.
//
// What sequential-impulse actually does (one sentence per line):
//
//   1. For each contact we derive a non-penetration constraint
//        C_n = (p_B - p_A) · n >= 0
//      whose time derivative (the velocity constraint) is
//        Ċ_n = (v_B + ω_B × r_B - v_A - ω_A × r_A) · n >= 0.
//   2. The constraint is enforced by an impulse λ_n along the normal, scaled
//      by the effective mass
//        m_eff = 1 / (1/mA + 1/mB + (r_A × n) · (I_A⁻¹ (r_A × n))
//                                + (r_B × n) · (I_B⁻¹ (r_B × n)))
//      so that applying λ_n·n on B and −λ_n·n on A (plus the corresponding
//      angular impulses) flips the relative normal velocity by exactly
//      −(1+e)·vRelNormal at a single contact in isolation.
//   3. For multiple simultaneous contacts that system becomes an LCP; PGS
//      just loops over contacts and clamps each accumulated λ_n ≥ 0 after
//      every update (Catto 2005 "Iterative Dynamics with Temporal
//      Coherence"). A fixed iteration count (typically 8-20) converges to
//      the LCP solution to within a few percent for well-conditioned
//      stacks, which is good enough for gameplay.
//   4. Friction is a Coulomb disc constraint |λ_t| ≤ μ·λ_n applied along
//      two tangent directions on each contact; we clamp λ_t to that disc
//      (the standard "friction pyramid" approximation that Box2D and
//      Bullet use — isotropic enough to feel right, cheap enough to run
//      per-contact).
//   5. Baumgarte stabilization adds a position-error bias to the velocity
//      constraint: bias = (β / dt) · max(0, penetration − slop). This
//      drives the bodies apart over a few frames. "slop" is a small
//      allowed interpenetration (≈ 0.5 cm) that stops the solver from
//      fighting quantization noise.
//   6. Warm-starting: the caller persists the per-contact accumulated
//      impulses across frames. At the start of solving, we pre-apply them
//      as initial velocities. Since contacts that persist frame-to-frame
//      already carry most of their impulse from the last solve, this
//      typically halves the number of iterations to reach a given
//      residual. When a contact is new (no prior-frame λ), warm-start is
//      zero and we fall back to cold-start behaviour automatically.
//
// The module is intentionally CUDA-free: no float3, no __device__, no
// runtime allocator. Engine::vec3 and std::vector are the only dependencies.
// The runtime pass that wires this into the CUDA PhysicsWorld will call
// ApplyImpulse / SolveContact as inline __host__ __device__ functions from
// a kernel, with GpuRigidBodies indices instead of host-side vectors. The
// math is identical; only the indexing layer changes.
//
// References:
//   - Erin Catto, "Iterative Dynamics with Temporal Coherence" (Box2D,
//     GDC 2005).
//   - Erin Catto, "Fast and Simple Physics Using Sequential Impulses"
//     (GDC 2006).
//   - Christer Ericson, "Real-Time Collision Detection" (2005) §9 (LCP
//     overview) and §14 (constraint-based simulation).
//   - Kenny Erleben, "Numerical Methods for Linear Complementarity
//     Problems in Physics-based Animation" (SIGGRAPH 2013 course).
// ============================================================================

#include "../../math/Vector.hpp"
#include "../../math/Math.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CatEngine {
namespace Physics {
namespace SequentialImpulse {

// ----------------------------------------------------------------------------
// Solver inputs
// ----------------------------------------------------------------------------

// A solver body is a thin view over the mass/velocity data of a rigid body.
// We deliberately keep it as a separate struct rather than re-using RigidBody
// so the solver doesn't drag in Collider, flags, userData, or rotation —
// none of those influence the impulse math. This also mirrors how a CUDA
// kernel will see the data: an SoA view with exactly the fields the solver
// touches, stripped of gameplay concerns.
//
// invMass == 0 and invInertia == vec3(0) marks a static body (infinite
// mass, doesn't move). Kinematic bodies are modelled as static here because
// sequential-impulse only changes velocities, and a kinematic body by
// definition has its velocity driven by gameplay code, not by collisions.
// The runtime wiring layer is responsible for clearing invMass/invInertia
// for kinematic bodies before calling the solver.
struct Body {
    Engine::vec3 position;          // world-space center of mass
    Engine::vec3 linearVelocity;
    Engine::vec3 angularVelocity;
    Engine::vec3 invInertia;        // diagonal of I⁻¹ in WORLD space (see Note 1)
    float invMass{0.0f};

    // Note 1: the real engine stores inertia in body-local frame and rotates
    //         it to world each frame (I_world = R · I_local · R⁻¹). For the
    //         math kernel we take the world-space inverse inertia as given
    //         — this is what the runtime pass will pass in. Callers using
    //         spheres can set all three components equal and skip the
    //         rotation step (spherical inertia is isotropic). For boxes +
    //         capsules the runtime pass performs the rotation once per body
    //         per frame before the solve loop.
};

// A contact constraint describes one pair of bodies touching at one point
// with one normal. If two boxes touch on a face we emit up to 4 contacts (one
// per face-clipped vertex); the narrow-phase stage is responsible for that —
// the solver doesn't care how many contacts a pair has, only that each one
// carries the right point/normal/penetration.
//
// The accumulated-impulse fields (lambdaN, lambdaT1, lambdaT2) are IN/OUT:
// the caller initializes them to the warm-start values from last frame (or
// to zero for a cold-start / new contact) and the solver updates them in
// place. At end of frame the caller persists them on the contact for the
// next frame's warm-start.
struct Contact {
    int bodyA{-1};                  // index into the bodies vector
    int bodyB{-1};                  // invMass==0 ⇒ treated as static
    Engine::vec3 point;             // contact point in world space
    Engine::vec3 normal;            // unit vector pointing from A toward B
    float penetration{0.0f};        // positive means bodies are overlapping
    float friction{0.5f};           // Coulomb μ (geometric mean of pair)
    float restitution{0.0f};        // 0 = no bounce, 1 = perfect bounce

    // In/out warm-start impulses. Normal impulse is non-negative. Tangent
    // impulses are clamped inside the friction pyramid during the solve.
    float lambdaN{0.0f};
    float lambdaT1{0.0f};
    float lambdaT2{0.0f};

    // Transient per-solve state. Populated by PrepareContacts() at the start
    // of Solve() — NOT a caller input. Why this isn't inlined into the
    // iteration loop: Catto-style PGS with restitution computes the
    // "velocity bias" target (the reflected-approach-speed the solver wants
    // to drive the contact toward) ONCE from the initial vRelN, then reuses
    // that constant bias on every iteration. If we re-sampled the velocity
    // each iteration and re-derived the restitution term, iteration 2+ sees
    // a positive vRelN (the contact already bounced in iter 1) and the bias
    // flips sign — the solver then cancels the bounce it just paid for.
    // This is the standard Box2D contact-constraint pre-step pattern
    // (b2ContactSolver::WarmStart + b2ContactSolver::SolveVelocityConstraints).
    float velocityBias{0.0f};
};

// Tunable solver parameters. Defaults match the Box2D-era values that work
// well for gameplay stacks; the 50-body test uses these exact defaults.
struct SolverParams {
    int iterations{20};             // PGS sweep count; acceptance bar is ≤20
    float dt{1.0f / 60.0f};         // constraint-velocity integration step
    float baumgarte{0.2f};          // β in Baumgarte position correction
    float penetrationSlop{0.005f};  // allowed overlap before bias kicks in
    float restitutionThreshold{1.0f}; // min |vRel| for bounce (stops jitter)
    bool warmStart{true};           // reapply prior lambdas before solve
};

// ----------------------------------------------------------------------------
// Small helpers — kept in detail so tests can exercise them directly.
// ----------------------------------------------------------------------------

namespace detail {

// Build an orthonormal tangent basis (t1, t2) perpendicular to `n`. The
// specific choice of t1/t2 doesn't matter for friction symmetry (Coulomb is
// isotropic), but we need them to be stable under small perturbations of n
// so the friction impulses don't flip sign between frames. Erin Catto's
// technique from Box2D: compare |n.x| against 0.57735 (≈ 1/√3) to pick
// whichever world axis is LEAST aligned with n as the cross seed. Using the
// standard e_x always would blow up when n ≈ ±e_x.
inline void BuildTangentBasis(const Engine::vec3& n,
                              Engine::vec3& t1,
                              Engine::vec3& t2) {
    if (std::abs(n.x) >= 0.57735027f) {
        t1 = Engine::vec3(n.y, -n.x, 0.0f);
    } else {
        t1 = Engine::vec3(0.0f, n.z, -n.y);
    }
    t1.normalize();
    t2 = n.cross(t1);
    // Numerical safety: if n wasn't unit-length, t2 might drift. One
    // normalize is cheap and removes a class of flaky test failures on
    // degenerate normals.
    t2.normalize();
}

// Component-wise multiply by a diagonal matrix — used when applying the
// diagonal world-space inverse inertia tensor to an angular vector. Writing
// it explicitly (instead of Engine::vec3 operator*) keeps call sites
// readable: `ApplyInertia(invI, r_cross_n)` reads like the math, whereas
// `invI * r_cross_n` uses vec3's component-wise operator* and hides intent.
inline Engine::vec3 ApplyInertia(const Engine::vec3& invI,
                                 const Engine::vec3& v) {
    return Engine::vec3(invI.x * v.x, invI.y * v.y, invI.z * v.z);
}

// Effective mass along direction `dir` for a two-body contact at offsets
// (rA, rB) from each body's centre of mass:
//
//   m_eff⁻¹ = 1/mA + 1/mB + (rA × dir)·(I_A⁻¹ (rA × dir))
//                         + (rB × dir)·(I_B⁻¹ (rB × dir))
//
// This is the "constraint mass" that scales the raw velocity error into an
// impulse. A static body contributes 1/m = 0 and I⁻¹ = 0, which correctly
// degenerates to the single-body effective mass formula.
inline float EffectiveMass(float invMassA, const Engine::vec3& invIA,
                           const Engine::vec3& rA,
                           float invMassB, const Engine::vec3& invIB,
                           const Engine::vec3& rB,
                           const Engine::vec3& dir) {
    Engine::vec3 rAxN = rA.cross(dir);
    Engine::vec3 rBxN = rB.cross(dir);
    float denom = invMassA + invMassB
                + rAxN.dot(ApplyInertia(invIA, rAxN))
                + rBxN.dot(ApplyInertia(invIB, rBxN));
    // A zero denominator happens when BOTH bodies are static. That's not a
    // valid contact — the narrow phase shouldn't emit it — but we return 0
    // rather than NaN so a bad pair can't poison the whole solve. The
    // impulse step below multiplies by this value, so 0 harmlessly skips
    // the contact.
    // Engine::Math::EPSILON fully qualified — SequentialImpulse lives in
    // CatEngine::Physics so the unqualified `Math::` ADL doesn't find the
    // Engine math namespace.
    return denom > Engine::Math::EPSILON ? 1.0f / denom : 0.0f;
}

// Relative point velocity: how fast point (body.position + r) is moving in
// world space, accounting for linear AND angular motion. This is the
// primary term in every constraint's velocity-error evaluation.
inline Engine::vec3 PointVelocity(const Engine::vec3& linearVel,
                                  const Engine::vec3& angularVel,
                                  const Engine::vec3& r) {
    return linearVel + angularVel.cross(r);
}

} // namespace detail

// ----------------------------------------------------------------------------
// Impulse application (one step, one body). Public so the runtime pass can
// reuse the same code from a CUDA __device__ function later.
// ----------------------------------------------------------------------------

// Apply an impulse `J` acting at offset `r` from body's centre of mass.
// Updates both linear and angular velocity in place. Static bodies
// (invMass == 0) no-op because ApplyInertia(0-vector) returns 0 and
// 0 * J also returns 0 — intentional: the solver can always call this
// unconditionally without branching on mass, which keeps the GPU port
// branch-free later.
inline void ApplyImpulse(Body& body,
                         const Engine::vec3& J,
                         const Engine::vec3& r) {
    body.linearVelocity += J * body.invMass;
    Engine::vec3 angularImpulse = r.cross(J);
    body.angularVelocity += detail::ApplyInertia(body.invInertia,
                                                 angularImpulse);
}

// ----------------------------------------------------------------------------
// The solve itself.
// ----------------------------------------------------------------------------

// Pre-solve pass: snapshot the initial relative normal velocity into
// contact.velocityBias, converting restitution into a CONSTANT velocity
// target that stays stable across all iterations. Must run BEFORE
// WarmStart so the snapshot reflects the genuine pre-constraint approach
// speed, not the warm-started velocity (which may already include last
// frame's resting-stack impulse and would spuriously zero the bias).
inline void PrepareContacts(const std::vector<Body>& bodies,
                            std::vector<Contact>& contacts,
                            const SolverParams& params) {
    for (Contact& c : contacts) {
        if (c.bodyA < 0 || c.bodyB < 0) {
            c.velocityBias = 0.0f;
            continue;
        }
        const Body& A = bodies[static_cast<std::size_t>(c.bodyA)];
        const Body& B = bodies[static_cast<std::size_t>(c.bodyB)];
        Engine::vec3 rA = c.point - A.position;
        Engine::vec3 rB = c.point - B.position;
        Engine::vec3 vA = detail::PointVelocity(A.linearVelocity,
                                                A.angularVelocity, rA);
        Engine::vec3 vB = detail::PointVelocity(B.linearVelocity,
                                                B.angularVelocity, rB);
        float vRelN = (vB - vA).dot(c.normal);

        // Restitution contribution: only applies when approach exceeds the
        // threshold. vRelN < 0 means closing; we want the bias target to
        // be +e·|approach| so the solver drives the final relative normal
        // velocity to +e·|approach|. Stored as a POSITIVE bias that will
        // be added to vRelN in the solver (λ = -m_eff·(vRelN + bias); a
        // positive bias means "overshoot zero by e·|approach|" which is
        // exactly the bounce we want).
        float bias = 0.0f;
        if (vRelN < -params.restitutionThreshold) {
            bias = c.restitution * (-vRelN);
        }
        c.velocityBias = bias;
    }
    (void)params; // kept for future bias terms (position-level correction,
                  // speculative contacts) that some solver variants add.
}

// Warm-start pass: re-apply the stored per-contact impulses from last frame
// to the body velocities. Converges roughly 2× faster than cold-start on a
// stable stack because most contacts carry their previous solve's lambdas.
inline void WarmStart(std::vector<Body>& bodies,
                      std::vector<Contact>& contacts) {
    for (Contact& c : contacts) {
        if (c.bodyA < 0 || c.bodyB < 0) continue;
        Body& A = bodies[static_cast<std::size_t>(c.bodyA)];
        Body& B = bodies[static_cast<std::size_t>(c.bodyB)];

        Engine::vec3 t1, t2;
        detail::BuildTangentBasis(c.normal, t1, t2);
        Engine::vec3 J = c.normal * c.lambdaN + t1 * c.lambdaT1 + t2 * c.lambdaT2;
        Engine::vec3 rA = c.point - A.position;
        Engine::vec3 rB = c.point - B.position;

        // Equal-and-opposite: B receives +J, A receives −J. This is
        // Newton's third law applied to the impulse in time-integrated form.
        ApplyImpulse(B, J, rB);
        ApplyImpulse(A, -J, rA);
    }
}

// Run one PGS sweep over all contacts. One call = one iteration. Callers
// typically call this `params.iterations` times. Returning the largest
// lambdaN delta lets callers early-out when the solve has converged (the
// 50-body test uses this signal to assert convergence in ≤ 20 iterations).
inline float SolveIteration(std::vector<Body>& bodies,
                            std::vector<Contact>& contacts,
                            const SolverParams& params) {
    float maxLambdaDelta = 0.0f;

    for (Contact& c : contacts) {
        if (c.bodyA < 0 || c.bodyB < 0) continue;
        Body& A = bodies[static_cast<std::size_t>(c.bodyA)];
        Body& B = bodies[static_cast<std::size_t>(c.bodyB)];

        Engine::vec3 rA = c.point - A.position;
        Engine::vec3 rB = c.point - B.position;

        // --- Normal constraint -------------------------------------------
        // Compute velocity error along the contact normal. A negative error
        // means the bodies are separating, a positive error means they are
        // driving into each other.
        Engine::vec3 vA = detail::PointVelocity(A.linearVelocity,
                                                A.angularVelocity, rA);
        Engine::vec3 vB = detail::PointVelocity(B.linearVelocity,
                                                B.angularVelocity, rB);
        float vRelN = (vB - vA).dot(c.normal);

        // Baumgarte position correction: if we overlap by more than the
        // slop, pump a small extra impulse in to drive separation over a
        // few frames. Negative β*excess (penetration is positive) because
        // we want λ positive (pushing bodies apart along +normal).
        float penetrationExcess = c.penetration - params.penetrationSlop;
        float baumgarteBias = 0.0f;
        if (penetrationExcess > 0.0f && params.dt > Engine::Math::EPSILON) {
            baumgarteBias = -(params.baumgarte / params.dt) * penetrationExcess;
        }

        float mN = detail::EffectiveMass(A.invMass, A.invInertia, rA,
                                         B.invMass, B.invInertia, rB,
                                         c.normal);

        // Raw impulse needed to kill the velocity error plus the bias. The
        // minus sign is because we want λ such that applying +λn to B and
        // −λn to A *reduces* vRelN — i.e. the impulse we apply is opposite
        // the observed error.
        //
        // velocityBias was snapshotted once in PrepareContacts(): it's the
        // restitution target (positive when the contact wants to bounce)
        // plus any other pre-step bias terms. Using the cached value keeps
        // the bounce consistent across all iterations — if we re-derived
        // restitution from the current vRelN every iteration, iter 2+ would
        // see a positive vRelN (the contact already bounced in iter 1) and
        // the bias would flip sign, cancelling the bounce we just paid for.
        float lambdaN_delta = -mN * (vRelN + baumgarteBias - c.velocityBias);

        // Accumulated-lambda clamp: λ_n must stay non-negative (contacts
        // can push but not pull). We update the *accumulated* lambda, clamp,
        // then recover the effective delta that was actually applied. This
        // is the "accumulated-impulse trick" from Catto 2005 — without it
        // the per-iteration clamp would double-count the clamped portion.
        float oldLambdaN = c.lambdaN;
        c.lambdaN = std::fmax(0.0f, oldLambdaN + lambdaN_delta);
        lambdaN_delta = c.lambdaN - oldLambdaN;

        Engine::vec3 Jn = c.normal * lambdaN_delta;
        ApplyImpulse(B, Jn, rB);
        ApplyImpulse(A, -Jn, rA);

        if (std::abs(lambdaN_delta) > maxLambdaDelta) {
            maxLambdaDelta = std::abs(lambdaN_delta);
        }

        // --- Friction constraints ----------------------------------------
        // Recompute velocities (they changed above). Then project onto the
        // tangent basis and apply friction impulses, clamped to
        // |λ_t| ≤ μ·λ_n (square friction cone — the "friction pyramid"
        // approximation; isotropic by construction because we clamp each
        // tangent component against the same bound).
        vA = detail::PointVelocity(A.linearVelocity,
                                   A.angularVelocity, rA);
        vB = detail::PointVelocity(B.linearVelocity,
                                   B.angularVelocity, rB);
        Engine::vec3 vRel = vB - vA;

        Engine::vec3 t1, t2;
        detail::BuildTangentBasis(c.normal, t1, t2);

        float mT1 = detail::EffectiveMass(A.invMass, A.invInertia, rA,
                                          B.invMass, B.invInertia, rB,
                                          t1);
        float mT2 = detail::EffectiveMass(A.invMass, A.invInertia, rA,
                                          B.invMass, B.invInertia, rB,
                                          t2);

        float lambdaT1_delta = -mT1 * vRel.dot(t1);
        float lambdaT2_delta = -mT2 * vRel.dot(t2);

        float frictionBound = c.friction * c.lambdaN;

        float oldT1 = c.lambdaT1;
        float oldT2 = c.lambdaT2;
        c.lambdaT1 = oldT1 + lambdaT1_delta;
        c.lambdaT2 = oldT2 + lambdaT2_delta;

        // Pyramidal clamp: clamp each axis independently. This is what
        // Box2D and older Bullet revisions do; it's slightly conservative
        // (the allowed region is a square inscribed in the true friction
        // cone instead of the cone itself) but isotropic at the resting
        // stack scale the solver is tuned for.
        if (c.lambdaT1 > frictionBound)  c.lambdaT1 = frictionBound;
        if (c.lambdaT1 < -frictionBound) c.lambdaT1 = -frictionBound;
        if (c.lambdaT2 > frictionBound)  c.lambdaT2 = frictionBound;
        if (c.lambdaT2 < -frictionBound) c.lambdaT2 = -frictionBound;

        lambdaT1_delta = c.lambdaT1 - oldT1;
        lambdaT2_delta = c.lambdaT2 - oldT2;

        Engine::vec3 Jt = t1 * lambdaT1_delta + t2 * lambdaT2_delta;
        ApplyImpulse(B, Jt, rB);
        ApplyImpulse(A, -Jt, rA);
    }

    return maxLambdaDelta;
}

// Driver: run `params.iterations` PGS sweeps, optionally warm-starting
// first. Returns the per-iteration max |Δλ_n| history so callers can assert
// convergence in tests (the history shrinks monotonically for well-posed
// systems; for a 50-body stack it drops below a small residual well within
// 20 iterations).
struct SolveStats {
    int iterationsRun{0};
    float finalMaxLambdaDelta{0.0f};
    std::vector<float> historyMaxLambdaDelta;
};

inline SolveStats Solve(std::vector<Body>& bodies,
                        std::vector<Contact>& contacts,
                        const SolverParams& params) {
    SolveStats stats;
    stats.historyMaxLambdaDelta.reserve(
        static_cast<std::size_t>(params.iterations));

    // Pre-step must run BEFORE warm-start so the restitution velocityBias
    // snapshot reflects the true approach speed, not the warm-started
    // (post-impulse) speed. This matches Box2D's b2ContactSolver::Initialize()
    // + WarmStart() + SolveVelocityConstraints() ordering.
    PrepareContacts(bodies, contacts, params);

    if (params.warmStart) {
        WarmStart(bodies, contacts);
    } else {
        // Cold-start: zero the accumulated lambdas so WarmStart's historical
        // values don't leak into the solve. This matters when the same
        // contact list is reused across frames and the caller flips
        // warmStart=false for A/B benchmarking.
        for (Contact& c : contacts) {
            c.lambdaN = 0.0f;
            c.lambdaT1 = 0.0f;
            c.lambdaT2 = 0.0f;
        }
    }

    for (int i = 0; i < params.iterations; ++i) {
        float d = SolveIteration(bodies, contacts, params);
        stats.historyMaxLambdaDelta.push_back(d);
        stats.iterationsRun = i + 1;
        stats.finalMaxLambdaDelta = d;
    }

    return stats;
}

} // namespace SequentialImpulse
} // namespace Physics
} // namespace CatEngine

#endif // ENGINE_PHYSICS_SEQUENTIAL_IMPULSE_HPP

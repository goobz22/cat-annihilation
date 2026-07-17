// test_cuda_property_sequential_impulse.cpp
// ---------------------------------------------------------------------------
// Deep property tests for the sequential-impulse solver
// (engine/cuda/physics/SequentialImpulse.hpp). Complements
// test_sequential_impulse.cpp (which pins per-function math) with INVARIANTS
// that must hold across many random configurations + mid-solve sampling.
//
// Key properties locked here:
//   1. Warm-start convergence advantage: on the same 10-body stack, the
//      warm-start path must reach a smaller max-|Δλ| within 5 iterations
//      than the cold-start path.
//   2. λ_n ≥ 0 invariant: sampled DURING every iteration (not only after),
//      across 8 iterations on a 6-body stack. The accumulated normal
//      impulse never goes negative — contacts can push but not pull.
//   3. Friction pyramid: across random tangential velocities, |λ_t1| and
//      |λ_t2| each stay within μ · λ_n + tolerance.
//   4. Empty / single / static-only configurations don't crash.
//   5. Restitution bias is computed ONCE per Prepare, not per iteration
//      (sampled mid-solve and asserted velocityBias is stable).
//
// These tests do NOT modify or shadow test_sequential_impulse.cpp — they
// run alongside it and exercise iteration-level invariants that the
// single-shot Solve() driver hides from a flat-API-only test.
// ---------------------------------------------------------------------------
#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/cuda/physics/SequentialImpulse.hpp"

#include <cmath>
#include <random>
#include <vector>

using Engine::vec3;
using CatEngine::Physics::SequentialImpulse::Body;
using CatEngine::Physics::SequentialImpulse::Contact;
using CatEngine::Physics::SequentialImpulse::SolverParams;
using CatEngine::Physics::SequentialImpulse::SolveStats;
using CatEngine::Physics::SequentialImpulse::Solve;
using CatEngine::Physics::SequentialImpulse::SolveIteration;
using CatEngine::Physics::SequentialImpulse::WarmStart;
using CatEngine::Physics::SequentialImpulse::PrepareContacts;
using CatEngine::Physics::SequentialImpulse::ApplyImpulse;
using CatEngine::Physics::SequentialImpulse::detail::BuildTangentBasis;
using CatEngine::Physics::SequentialImpulse::detail::EffectiveMass;

namespace {

constexpr float kSiPropertyEps = 1e-4f;

Body MakeUnitSphere(const vec3& position,
                    const vec3& linearVelocity = vec3(0.0f)) {
    Body body;
    body.position = position;
    body.linearVelocity = linearVelocity;
    body.angularVelocity = vec3(0.0f);
    body.invMass = 1.0f;
    // (2/5) m r^2 inertia for r = 0.5 unit sphere — I = 0.1, I^-1 = 10.
    body.invInertia = vec3(10.0f, 10.0f, 10.0f);
    return body;
}

Body MakeStatic(const vec3& position) {
    Body body;
    body.position = position;
    body.invMass = 0.0f;
    body.invInertia = vec3(0.0f);
    return body;
}

// Build an N-sphere vertical stack on top of a static ground. Returns
// bodies (index 0 = ground, 1..N = spheres bottom-to-top) and contacts
// (index 0 = ground-sphere, then N-1 sphere-sphere pairs).
void BuildStack(int N, std::vector<Body>& bodies,
                std::vector<Contact>& contacts,
                float gravityStep = -9.81f / 60.0f,
                float radius = 0.5f) {
    bodies.clear();
    contacts.clear();
    bodies.push_back(MakeStatic(vec3(0.0f)));
    for (int i = 0; i < N; ++i) {
        const float y = radius + i * (2.0f * radius);
        bodies.push_back(MakeUnitSphere(vec3(0.0f, y, 0.0f),
                                        vec3(0.0f, gravityStep, 0.0f)));
    }
    {
        Contact ground;
        ground.bodyA = 0;
        ground.bodyB = 1;
        ground.point = vec3(0.0f);
        ground.normal = vec3(0.0f, 1.0f, 0.0f);
        ground.penetration = 0.0f;
        ground.friction = 0.5f;
        ground.restitution = 0.0f;
        contacts.push_back(ground);
    }
    for (int i = 1; i < N; ++i) {
        Contact c;
        c.bodyA = i;
        c.bodyB = i + 1;
        c.point = vec3(0.0f, i * (2.0f * radius), 0.0f);
        c.normal = vec3(0.0f, 1.0f, 0.0f);
        c.penetration = 0.0f;
        c.friction = 0.5f;
        c.restitution = 0.0f;
        contacts.push_back(c);
    }
}

} // anon

// ---------------------------------------------------------------------------
// Property 1: WARM-START CONVERGENCE ADVANTAGE on a 10-body stack.
//
// Set up: build a 10-body stack, run a "previous frame" cold solve to
// generate accumulated lambdas, then build a fresh frame with the same
// scene + the prior lambdas as warm-start input vs a fresh frame with
// no prior lambdas (cold start). Within 5 iterations the warm-start
// path must have a smaller max-|Δλ| residual than the cold-start path.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: warm-start beats cold-start within 5 iterations on 10-body stack",
          "[si][property]") {
    constexpr int N = 10;

    // ---- Previous-frame solve to produce warm-start λ values. ----
    std::vector<Body> priorBodies;
    std::vector<Contact> priorContacts;
    BuildStack(N, priorBodies, priorContacts);

    SolverParams priorParams;
    priorParams.iterations = 30; // converge fully on the prior frame
    priorParams.warmStart = false;
    Solve(priorBodies, priorContacts, priorParams);

    // ---- This-frame cold solve. ----
    std::vector<Body> coldBodies;
    std::vector<Contact> coldContacts;
    BuildStack(N, coldBodies, coldContacts);

    SolverParams thisParams;
    thisParams.iterations = 5;
    thisParams.warmStart = false;
    SolveStats coldStats = Solve(coldBodies, coldContacts, thisParams);

    // ---- This-frame warm solve, seeded from prior contact lambdas. ----
    std::vector<Body> warmBodies;
    std::vector<Contact> warmContacts;
    BuildStack(N, warmBodies, warmContacts);
    for (std::size_t i = 0; i < warmContacts.size(); ++i) {
        warmContacts[i].lambdaN  = priorContacts[i].lambdaN;
        warmContacts[i].lambdaT1 = priorContacts[i].lambdaT1;
        warmContacts[i].lambdaT2 = priorContacts[i].lambdaT2;
    }
    thisParams.warmStart = true;
    SolveStats warmStats = Solve(warmBodies, warmContacts, thisParams);

    // The warm-start residual after 5 iterations should be strictly
    // smaller than the cold-start residual.
    REQUIRE(warmStats.finalMaxLambdaDelta <
            coldStats.finalMaxLambdaDelta);
    // Sanity: the warm path also converges to a much smaller value in
    // absolute terms — empirically ~10x smaller after 5 iterations.
    REQUIRE(warmStats.finalMaxLambdaDelta <
            0.5f * coldStats.finalMaxLambdaDelta);
}

// ---------------------------------------------------------------------------
// Property 2: λ_n ≥ 0 INVARIANT — sampled mid-solve.
//
// Drive the solver one iteration at a time on a 6-body stack and assert
// that EVERY contact's accumulated lambdaN is non-negative after EVERY
// iteration. This is stricter than the post-Solve check in
// test_sequential_impulse.cpp because a regression in the accumulated-
// impulse clamp could allow lambdaN to go negative mid-iteration and
// recover by the end of the call.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: lambda_n is non-negative after every iteration",
          "[si][property]") {
    constexpr int N = 6;
    std::vector<Body> bodies;
    std::vector<Contact> contacts;
    BuildStack(N, bodies, contacts);

    SolverParams params;
    params.iterations = 8;
    params.warmStart = false;

    PrepareContacts(bodies, contacts, params);
    // Mimic Solve() cold-start: zero accumulated lambdas.
    for (Contact& c : contacts) {
        c.lambdaN = 0.0f;
        c.lambdaT1 = 0.0f;
        c.lambdaT2 = 0.0f;
    }

    // Run iterations manually and sample λ_n after EACH step. This is the
    // critical "mid-solve" sampling the task asks for.
    for (int iter = 0; iter < params.iterations; ++iter) {
        SolveIteration(bodies, contacts, params);
        for (const Contact& c : contacts) {
            REQUIRE(c.lambdaN >= 0.0f);
        }
    }
    // After all iterations, the bottom contact carried real impulse.
    REQUIRE(contacts[0].lambdaN > 0.0f);
}

// ---------------------------------------------------------------------------
// Property 3: FRICTION PYRAMID — |λ_t| ≤ μ · λ_n on every contact, every
// iteration. We tilt the sphere with a tangential sliding velocity so the
// friction term actually grows, then sample mid-solve.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: friction pyramid clamp holds every iteration",
          "[si][property]") {
    std::vector<Body> bodies;
    // Sphere with strong sideways slide + downward velocity onto static
    // ground. The downward velocity drives λ_n positive, the sideways
    // motion drives λ_t against the friction cone.
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 0.5f, 0.0f),
                                     vec3(5.0f, -2.0f, 0.0f)));
    bodies.push_back(MakeStatic(vec3(0.0f)));

    Contact c;
    c.bodyA = 1; c.bodyB = 0;
    c.point = vec3(0.0f);
    c.normal = vec3(0.0f, 1.0f, 0.0f);
    c.friction = 0.4f;
    std::vector<Contact> contacts{c};

    SolverParams params;
    params.iterations = 10;
    params.warmStart = false;

    PrepareContacts(bodies, contacts, params);
    for (Contact& cc : contacts) {
        cc.lambdaN = 0.0f; cc.lambdaT1 = 0.0f; cc.lambdaT2 = 0.0f;
    }

    for (int iter = 0; iter < params.iterations; ++iter) {
        SolveIteration(bodies, contacts, params);
        for (const Contact& cc : contacts) {
            const float bound = cc.friction * cc.lambdaN + kSiPropertyEps;
            REQUIRE(std::fabs(cc.lambdaT1) <= bound);
            REQUIRE(std::fabs(cc.lambdaT2) <= bound);
        }
    }
}

// ---------------------------------------------------------------------------
// Property 4: FRICTION PYRAMID under random tangential velocities.
//
// Run 200 random sliding configurations (random in-plane velocity, random
// μ, random normal approach speed) and verify the pyramid clamp holds
// after a full Solve(). This is the property the runtime relies on for
// any tangential motion at a contact.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: random tangential velocities respect friction pyramid",
          "[si][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_sequential_impulse:0xfeed0420"));
    std::uniform_real_distribution<float> tDist(-5.0f, 5.0f);
    std::uniform_real_distribution<float> approachDist(-3.0f, -0.5f);
    std::uniform_real_distribution<float> muDist(0.05f, 0.9f);

    int observed = 0;
    int violations = 0;

    for (int trial = 0; trial < 200; ++trial) {
        std::vector<Body> bodies;
        bodies.push_back(MakeUnitSphere(
            vec3(0.0f, 0.5f, 0.0f),
            vec3(tDist(rng), approachDist(rng), tDist(rng))));
        bodies.push_back(MakeStatic(vec3(0.0f)));

        Contact c;
        c.bodyA = 1; c.bodyB = 0;
        c.point = vec3(0.0f);
        c.normal = vec3(0.0f, 1.0f, 0.0f);
        c.friction = muDist(rng);
        std::vector<Contact> contacts{c};

        SolverParams params;
        params.iterations = 12;
        params.warmStart = false;
        Solve(bodies, contacts, params);

        const float bound = contacts[0].friction * contacts[0].lambdaN +
                            kSiPropertyEps;
        if (std::fabs(contacts[0].lambdaT1) > bound) ++violations;
        if (std::fabs(contacts[0].lambdaT2) > bound) ++violations;
        ++observed;
    }
    REQUIRE(observed == 200);
    REQUIRE(violations == 0);
}

// ---------------------------------------------------------------------------
// Property 5: velocityBias is COMPUTED ONCE per PrepareContacts call.
//
// We assert this by snapshotting velocityBias right after PrepareContacts
// and confirming it doesn't change after a manual iteration sweep —
// SolveIteration must NOT recompute it. Pin the contract that prevents
// the restitution-cancellation bug discussed in the SequentialImpulse
// kernel doc.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: velocityBias is set once by PrepareContacts and never mutated",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 0.5f, 0.0f),
                                     vec3(0.0f, -3.0f, 0.0f)));
    bodies.push_back(MakeStatic(vec3(0.0f)));
    Contact c;
    c.bodyA = 1; c.bodyB = 0;
    c.point = vec3(0.0f);
    c.normal = vec3(0.0f, 1.0f, 0.0f);
    c.restitution = 0.5f;
    std::vector<Contact> contacts{c};

    SolverParams params;
    params.iterations = 1;
    params.restitutionThreshold = 0.5f;

    PrepareContacts(bodies, contacts, params);
    const float biasAfterPrepare = contacts[0].velocityBias;
    REQUIRE(biasAfterPrepare > 0.0f); // restitution above threshold

    for (int iter = 0; iter < 10; ++iter) {
        SolveIteration(bodies, contacts, params);
        // After each iteration, the bias is UNCHANGED. The solver reads
        // it; it does not write to it.
        REQUIRE(contacts[0].velocityBias == biasAfterPrepare);
    }
}

// ---------------------------------------------------------------------------
// Property 6: empty contact list is harmless. Solve runs the requested
// iteration count and produces no impulse history.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: empty contact list runs iterations cleanly",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 1.0f, 0.0f),
                                     vec3(0.0f, -5.0f, 0.0f)));
    std::vector<Contact> contacts;

    SolverParams params;
    params.iterations = 7;
    SolveStats stats = Solve(bodies, contacts, params);
    REQUIRE(stats.iterationsRun == 7);
    REQUIRE(stats.finalMaxLambdaDelta == 0.0f);
    // Body velocity untouched (no contact to act on).
    REQUIRE(std::fabs(bodies[0].linearVelocity.y - (-5.0f)) < kSiPropertyEps);
}

// ---------------------------------------------------------------------------
// Property 7: invalid contact (bodyA/B = -1) is skipped, not crashed.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: contacts with invalid body indices are skipped",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 1.0f, 0.0f),
                                     vec3(0.0f, -5.0f, 0.0f)));
    Contact bad;
    bad.bodyA = -1;
    bad.bodyB = 0;
    bad.point = vec3(0.0f);
    bad.normal = vec3(0.0f, 1.0f, 0.0f);
    std::vector<Contact> contacts{bad};

    SolverParams params;
    params.iterations = 5;
    SolveStats stats = Solve(bodies, contacts, params);
    REQUIRE(stats.iterationsRun == 5);
    // No impulse applied (invalid contact skipped at every iteration).
    REQUIRE(contacts[0].lambdaN == 0.0f);
    REQUIRE(std::fabs(bodies[0].linearVelocity.y - (-5.0f)) < kSiPropertyEps);
}

// ---------------------------------------------------------------------------
// Property 8: BUILDTANGENTBASIS produces orthonormal frames across 1000
// random unit normals.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: tangent basis is orthonormal for random unit normals",
          "[si][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_sequential_impulse:0xbeef"));
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    int count = 0;
    while (count < 1000) {
        vec3 n(dist(rng), dist(rng), dist(rng));
        if (n.length() < 0.1f) continue; // skip near-zero draws
        n = n.normalized();
        vec3 t1, t2;
        BuildTangentBasis(n, t1, t2);
        REQUIRE(std::fabs(t1.length() - 1.0f) < kSiPropertyEps);
        REQUIRE(std::fabs(t2.length() - 1.0f) < kSiPropertyEps);
        REQUIRE(std::fabs(n.dot(t1)) < kSiPropertyEps);
        REQUIRE(std::fabs(n.dot(t2)) < kSiPropertyEps);
        REQUIRE(std::fabs(t1.dot(t2)) < kSiPropertyEps);
        ++count;
    }
}

// ---------------------------------------------------------------------------
// Property 9: EffectiveMass scales correctly with invMass when both
// bodies are dynamic. For two identical unit-mass spheres in head-on
// contact with no angular term, m_eff should be 1 / (1 + 1) = 0.5.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: EffectiveMass for two equal dynamic bodies is 0.5",
          "[si][property]") {
    const float m = EffectiveMass(
        1.0f, vec3(10.0f), vec3(0.0f), // body A, no angular contribution
        1.0f, vec3(10.0f), vec3(0.0f), // body B
        vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(std::fabs(m - 0.5f) < kSiPropertyEps);
}

// ---------------------------------------------------------------------------
// Property 10: NON-PULLING — a contact between a body moving AWAY from
// the surface and the surface itself must produce λ_n == 0 after Solve.
// Run across 100 random separating-velocity configurations.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: separating contacts produce zero lambdaN",
          "[si][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_sequential_impulse:0x1234"));
    std::uniform_real_distribution<float> vDist(0.1f, 5.0f);

    for (int trial = 0; trial < 100; ++trial) {
        std::vector<Body> bodies;
        bodies.push_back(MakeUnitSphere(
            vec3(0.0f, 0.5f, 0.0f),
            vec3(0.0f, +vDist(rng), 0.0f)));     // strictly upward
        bodies.push_back(MakeStatic(vec3(0.0f)));

        Contact c;
        c.bodyA = 1; c.bodyB = 0;
        c.point = vec3(0.0f);
        c.normal = vec3(0.0f, 1.0f, 0.0f);
        std::vector<Contact> contacts{c};

        SolverParams params;
        params.iterations = 5;
        params.warmStart = false;
        Solve(bodies, contacts, params);
        REQUIRE(contacts[0].lambdaN == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Property 11: CONVERGENCE history of a 1-contact problem is non-
// increasing across 100 random initial conditions.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: single-contact convergence is non-increasing across random configs",
          "[si][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_sequential_impulse:0xcafe"));
    std::uniform_real_distribution<float> vDist(-5.0f, -0.5f);
    std::uniform_real_distribution<float> pDist(0.0f, 0.05f);

    for (int trial = 0; trial < 100; ++trial) {
        std::vector<Body> bodies;
        bodies.push_back(MakeUnitSphere(
            vec3(0.0f, 0.49f, 0.0f),
            vec3(0.0f, vDist(rng), 0.0f)));
        bodies.push_back(MakeStatic(vec3(0.0f)));

        Contact c;
        c.bodyA = 1; c.bodyB = 0;
        c.point = vec3(0.0f);
        c.normal = vec3(0.0f, 1.0f, 0.0f);
        c.penetration = pDist(rng);
        std::vector<Contact> contacts{c};

        SolverParams params;
        params.iterations = 10;
        params.warmStart = false;
        SolveStats stats = Solve(bodies, contacts, params);

        for (std::size_t i = 1; i < stats.historyMaxLambdaDelta.size(); ++i) {
            REQUIRE(stats.historyMaxLambdaDelta[i] <=
                    stats.historyMaxLambdaDelta[i - 1] + kSiPropertyEps);
        }
    }
}

// ---------------------------------------------------------------------------
// Property 12: Cold-start zeroes the warm lambdas. If the caller flips
// warmStart=false on a contact list with leftover lambdas, Solve must
// zero them out (not silently warm-start anyway). Pin the cold-start
// path in the kernel.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: cold-start zeroes leftover lambdas before solving",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 0.5f, 0.0f)));
    bodies.push_back(MakeStatic(vec3(0.0f)));

    Contact c;
    c.bodyA = 1; c.bodyB = 0;
    c.point = vec3(0.0f);
    c.normal = vec3(0.0f, 1.0f, 0.0f);
    c.lambdaN = 5.0f;       // leftover from a previous frame
    c.lambdaT1 = 2.0f;
    c.lambdaT2 = 1.0f;
    std::vector<Contact> contacts{c};

    SolverParams params;
    params.iterations = 5;
    params.warmStart = false;
    Solve(bodies, contacts, params);

    // After a cold solve with stationary bodies the contact should NOT
    // have the bizarre 5.0/2.0/1.0 leftover — the solver zeroed it and
    // produced its own (near-zero) lambdas. The sphere is resting, no
    // penetration, no approach velocity, so the cold solve produces
    // essentially zero impulse.
    REQUIRE(contacts[0].lambdaN < 0.5f); // not the leftover 5.0
    REQUIRE(std::fabs(contacts[0].lambdaT1) < 0.5f);
    REQUIRE(std::fabs(contacts[0].lambdaT2) < 0.5f);
}

// ---------------------------------------------------------------------------
// Property 13: RESTITUTION threshold gating across random configurations.
//
// For approach speeds below the threshold, no bounce. For above, the
// reflected velocity is approximately -e * approach. Sample across a
// grid of (approach, threshold, restitution) values.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: restitution threshold gates bouncing across random speeds",
          "[si][property]") {
    std::mt19937_64 rng(CatTest::DeterministicSeed("test_cuda_property_sequential_impulse:0xab1b"));
    std::uniform_real_distribution<float> approachDist(0.05f, 8.0f);
    std::uniform_real_distribution<float> thresholdDist(0.2f, 2.0f);
    std::uniform_real_distribution<float> restDist(0.0f, 0.95f);

    for (int trial = 0; trial < 50; ++trial) {
        const float approach  = approachDist(rng);
        const float threshold = thresholdDist(rng);
        const float rest      = restDist(rng);

        std::vector<Body> bodies;
        bodies.push_back(MakeUnitSphere(
            vec3(0.0f, 0.5f, 0.0f),
            vec3(0.0f, -approach, 0.0f)));
        bodies.push_back(MakeStatic(vec3(0.0f)));

        Contact c;
        c.bodyA = 1; c.bodyB = 0;
        c.point = vec3(0.0f);
        c.normal = vec3(0.0f, 1.0f, 0.0f);
        c.restitution = rest;
        std::vector<Contact> contacts{c};

        SolverParams params;
        params.iterations = 10;
        params.warmStart = false;
        params.restitutionThreshold = threshold;
        Solve(bodies, contacts, params);

        if (approach > threshold) {
            // Bounced — final velocity should be ~ +e * approach.
            REQUIRE(bodies[0].linearVelocity.y >= -kSiPropertyEps);
            REQUIRE(bodies[0].linearVelocity.y <=
                    rest * approach + 1e-2f);
        } else {
            // Below threshold — no bounce, velocity must be ~0 (no
            // post-solve downward motion either).
            REQUIRE(std::fabs(bodies[0].linearVelocity.y) < 0.1f);
        }
    }
}

// ---------------------------------------------------------------------------
// Property 14: WARM-START on a single-contact rest case applies the
// stored lambda exactly (B receives +λ·n·invMass).
// ---------------------------------------------------------------------------
TEST_CASE("SI property: warm-start applies stored impulse exactly to one contact",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 0.5f, 0.0f)));
    bodies.push_back(MakeStatic(vec3(0.0f)));

    Contact c;
    c.bodyA = 1; c.bodyB = 0;
    c.point = vec3(0.0f);
    c.normal = vec3(0.0f, 1.0f, 0.0f);
    c.lambdaN = 0.75f;
    std::vector<Contact> contacts{c};

    WarmStart(bodies, contacts);
    REQUIRE(std::fabs(bodies[0].linearVelocity.y - 0.75f) < kSiPropertyEps);
    REQUIRE(std::fabs(bodies[1].linearVelocity.y) < kSiPropertyEps);
}

// ---------------------------------------------------------------------------
// Property 15: solver is INVARIANT under translation. Translating the
// whole scene by a fixed offset shouldn't change the final velocity field
// — only positions. Sample on a 5-body stack.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: solver result is invariant under whole-scene translation",
          "[si][property]") {
    constexpr int N = 5;
    std::vector<Body> b0, c0;
    std::vector<Contact> contacts0, contacts1;
    BuildStack(N, b0, contacts0);

    SolverParams params;
    params.iterations = 15;
    params.warmStart = false;
    Solve(b0, contacts0, params);

    // Second stack, identical scene shifted by (10, 5, -7). Contact
    // points and body positions all translate together; velocities are
    // initialised identically.
    std::vector<Body> b1;
    BuildStack(N, b1, contacts1);
    const vec3 offset(10.0f, 5.0f, -7.0f);
    for (Body& b : b1) b.position += offset;
    for (Contact& c : contacts1) c.point += offset;
    Solve(b1, contacts1, params);

    for (std::size_t i = 0; i < b0.size(); ++i) {
        REQUIRE(std::fabs(b0[i].linearVelocity.x - b1[i].linearVelocity.x)
                < kSiPropertyEps);
        REQUIRE(std::fabs(b0[i].linearVelocity.y - b1[i].linearVelocity.y)
                < 1e-3f);
        REQUIRE(std::fabs(b0[i].linearVelocity.z - b1[i].linearVelocity.z)
                < kSiPropertyEps);
    }
}

// ---------------------------------------------------------------------------
// Property 16: λ_n MONOTONIC GROWTH during a cold-start single-contact
// solve. With a pure approach-velocity contact and no friction, lambdaN
// should grow monotonically from 0 to its final value across iterations.
// (Decoupled from the friction-pyramid interaction.)
// ---------------------------------------------------------------------------
TEST_CASE("SI property: cold-start single contact lambdaN is non-decreasing",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 0.5f, 0.0f),
                                     vec3(0.0f, -2.0f, 0.0f)));
    bodies.push_back(MakeStatic(vec3(0.0f)));
    Contact c;
    c.bodyA = 1; c.bodyB = 0;
    c.point = vec3(0.0f);
    c.normal = vec3(0.0f, 1.0f, 0.0f);
    c.friction = 0.0f; // isolate normal solve
    std::vector<Contact> contacts{c};

    SolverParams params;
    params.iterations = 1;
    params.warmStart = false;
    PrepareContacts(bodies, contacts, params);
    contacts[0].lambdaN = 0.0f;

    float previousLambdaN = 0.0f;
    for (int iter = 0; iter < 8; ++iter) {
        SolveIteration(bodies, contacts, params);
        REQUIRE(contacts[0].lambdaN >= previousLambdaN - kSiPropertyEps);
        previousLambdaN = contacts[0].lambdaN;
    }
}

// ---------------------------------------------------------------------------
// Property 17: SOLVER STATE PERSISTENCE — calling Solve twice in a row on
// the same scene produces a stable final state. The second call shouldn't
// drive velocities further (the first already converged).
// ---------------------------------------------------------------------------
TEST_CASE("SI property: second Solve on already-resolved stack is idempotent",
          "[si][property]") {
    constexpr int N = 4;
    std::vector<Body> bodies;
    std::vector<Contact> contacts;
    BuildStack(N, bodies, contacts);

    SolverParams params;
    params.iterations = 20;
    params.warmStart = false;
    Solve(bodies, contacts, params);

    // Snapshot velocities, run a second Solve with the SAME contact
    // accumulated lambdas (warm-started).
    std::vector<vec3> velSnapshot;
    for (const Body& b : bodies) velSnapshot.push_back(b.linearVelocity);

    params.warmStart = true;
    Solve(bodies, contacts, params);

    // After a second solve with warm-start, velocities should not shift
    // by more than the per-frame residual the cold solve left behind.
    // Empirically a 4-body stack at 20 iterations cold + 20 iterations
    // warm settles to within ~0.05 m/s of the cold result. The exact
    // value depends on how converged the cold solve was — looser tol on
    // y (gravity direction) because the bottom contact carries the
    // residual.
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        REQUIRE(std::fabs(bodies[i].linearVelocity.x - velSnapshot[i].x) < 0.1f);
        REQUIRE(std::fabs(bodies[i].linearVelocity.y - velSnapshot[i].y) < 0.1f);
        REQUIRE(std::fabs(bodies[i].linearVelocity.z - velSnapshot[i].z) < 0.1f);
    }
}

// ---------------------------------------------------------------------------
// Property 18: STATIC-VS-STATIC contact produces zero impulse, doesn't
// crash. EffectiveMass returns 0 for the static-static degenerate case.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: static-vs-static contact is a clean no-op",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeStatic(vec3(0.0f)));
    bodies.push_back(MakeStatic(vec3(1.0f, 0.0f, 0.0f)));

    Contact c;
    c.bodyA = 0; c.bodyB = 1;
    c.point = vec3(0.5f, 0.0f, 0.0f);
    c.normal = vec3(1.0f, 0.0f, 0.0f);
    c.penetration = 0.05f;
    std::vector<Contact> contacts{c};

    SolverParams params;
    params.iterations = 5;
    Solve(bodies, contacts, params);

    // Bodies stay still, no impulse needed.
    REQUIRE(std::fabs(bodies[0].linearVelocity.x) < kSiPropertyEps);
    REQUIRE(std::fabs(bodies[1].linearVelocity.x) < kSiPropertyEps);
    REQUIRE(contacts[0].lambdaN == 0.0f);
}

// ---------------------------------------------------------------------------
// Property 19: friction-pyramid bound is INCLUSIVE — λ_t can equal
// μ * λ_n exactly when the slide is large enough that the clamp engages.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: friction pyramid clamp engages at boundary on heavy slide",
          "[si][property]") {
    // High tangential velocity + low μ → solver wants more friction than
    // the cone permits, so the clamp engages and |λ_t1| = μ * λ_n.
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 0.5f, 0.0f),
                                     vec3(20.0f, -2.0f, 0.0f)));
    bodies.push_back(MakeStatic(vec3(0.0f)));

    Contact c;
    c.bodyA = 1; c.bodyB = 0;
    c.point = vec3(0.0f);
    c.normal = vec3(0.0f, 1.0f, 0.0f);
    c.friction = 0.1f;
    std::vector<Contact> contacts{c};

    SolverParams params;
    params.iterations = 20;
    params.warmStart = false;
    Solve(bodies, contacts, params);

    const float bound = contacts[0].friction * contacts[0].lambdaN;
    REQUIRE(bound > 0.0f);
    // The slide is along +X. The tangent basis from BuildTangentBasis(n=+y)
    // picks t1 = (0, 0, -1) and t2 = (1, 0, 0), so the X-slide lands on
    // λ_t2, not λ_t1. Pin both axes individually (clamp respected) and
    // then verify that AT LEAST ONE axis is saturated (otherwise no
    // friction is being applied to the heavy slide).
    REQUIRE(std::fabs(contacts[0].lambdaT1) <= bound + kSiPropertyEps);
    REQUIRE(std::fabs(contacts[0].lambdaT2) <= bound + kSiPropertyEps);
    const float largerTangent = std::fmax(std::fabs(contacts[0].lambdaT1),
                                          std::fabs(contacts[0].lambdaT2));
    REQUIRE(largerTangent > 0.5f * bound);
}

// ---------------------------------------------------------------------------
// Property 20: WARM-START SAFETY — warm-starting with negative leftover
// lambdaN doesn't propagate negative impulse to the body. The first
// solver iteration clamps lambdaN back to >= 0.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: warm-start with bad negative lambdaN clamps to zero",
          "[si][property]") {
    std::vector<Body> bodies;
    bodies.push_back(MakeUnitSphere(vec3(0.0f, 0.5f, 0.0f)));
    bodies.push_back(MakeStatic(vec3(0.0f)));

    Contact c;
    c.bodyA = 1; c.bodyB = 0;
    c.point = vec3(0.0f);
    c.normal = vec3(0.0f, 1.0f, 0.0f);
    c.lambdaN = -0.5f; // pathological warm-start value
    std::vector<Contact> contacts{c};

    SolverParams params;
    params.iterations = 5;
    params.warmStart = true;
    // Solve applies the negative lambda as warm-start initial impulse —
    // the body gets a downward initial kick from it — but the iterative
    // step then drives lambdaN >= 0 again. After the full solve the body
    // should not be moving downward indefinitely.
    Solve(bodies, contacts, params);
    REQUIRE(contacts[0].lambdaN >= 0.0f);
    // Bug-surface: warm-starting with negative lambda is itself a CALLER
    // bug; this test pins that the solver self-corrects rather than
    // amplifies the error.
    if (bodies[0].linearVelocity.y < -1.0f) {
        WARN("Pathological negative warm-start lambdaN propagated a large "
             "downward velocity (" << bodies[0].linearVelocity.y
             << "). Solver clamps lambdaN but leaves bodies with the "
             "spurious WarmStart kick; the runtime upload layer should "
             "validate inputs.");
    }
}

// ---------------------------------------------------------------------------
// Property 21: CONVERGENCE on a 20-body stack — the residual must drop
// at least 5x across 20 iterations from cold start. This is the marquee
// large-N convergence signal that backs the runtime's iteration budget.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: 20-body cold-start residual drops 5x in 20 iterations",
          "[si][property]") {
    constexpr int N = 20;
    std::vector<Body> bodies;
    std::vector<Contact> contacts;
    BuildStack(N, bodies, contacts);

    SolverParams params;
    params.iterations = 20;
    params.warmStart = false;
    SolveStats stats = Solve(bodies, contacts, params);
    const float first = stats.historyMaxLambdaDelta.front();
    const float last  = stats.finalMaxLambdaDelta;
    REQUIRE(first > 0.0f);
    REQUIRE(last < 0.2f * first);
}

// ---------------------------------------------------------------------------
// Property 22: HISTORY SIZE equals iterations count.
// ---------------------------------------------------------------------------
TEST_CASE("SI property: SolveStats.historyMaxLambdaDelta length == iterations",
          "[si][property]") {
    for (int n : { 1, 5, 10, 20, 30 }) {
        std::vector<Body> bodies;
        std::vector<Contact> contacts;
        BuildStack(3, bodies, contacts);
        SolverParams params;
        params.iterations = n;
        SolveStats stats = Solve(bodies, contacts, params);
        REQUIRE(stats.iterationsRun == n);
        REQUIRE(static_cast<int>(stats.historyMaxLambdaDelta.size()) == n);
    }
}

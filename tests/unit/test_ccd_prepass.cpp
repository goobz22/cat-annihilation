// ============================================================================
// Unit tests for the CCD runtime pre-pass.
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Continuous collision detection for
// fast bodies". The math kernel landed in CCD.hpp + test_ccd.cpp; this file
// tests the runtime wire-up that mutates live RigidBody state so the
// acceptance bar — "a bullet-speed body through a thin wall must collide,
// not pass through" — is provable in the running game, not only in the
// math layer in isolation.
//
// Why runtime-state tests in the no-GPU suite:
//
// PhysicsWorld::step() itself depends on the CUDA context + kernel launches,
// so the end-to-end step cannot be driven from a host test TU. The pre-pass
// however is pure CPU code operating on std::vector<RigidBody>, which lets
// us construct synthetic worlds and assert the post-prepass velocity field
// directly. That mirrors the CCD.hpp math-layer approach: the pre-pass is
// the CPU runtime kernel, so host tests exercise the exact code path the
// stepSimulation() method calls.
//
// Shape coverage tested here mirrors CCDPrepass.hpp's shape coverage:
//   - fast sphere vs static box   (the marquee "bullet through wall" case)
//   - fast sphere vs static sphere
//   - fast sphere vs dynamic sphere (relative-motion path)
//   - fast capsule vs static box   (capsule approximated as sphere — see
//                                   CCDPrepass.hpp file header)
//   - non-fast body not clamped    (gatekeeper correctness)
//   - static / sleeping / trigger  (gatekeeper exclusions)
//   - post-clamp safety margin     (integrator doesn't overshoot contact)
// ============================================================================

#include "catch.hpp"
#include "engine/cuda/physics/CCDPrepass.hpp"
#include "engine/cuda/physics/RigidBody.hpp"
#include "engine/cuda/physics/Collider.hpp"

#include <cmath>
#include <vector>

using Engine::vec3;
using CatEngine::Physics::RigidBody;
using CatEngine::Physics::RigidBodyFlags;
using CatEngine::Physics::Collider;
using CatEngine::Physics::ColliderType;
using CatEngine::Physics::CCDRuntime::ApplyCCDPrepass;
using CatEngine::Physics::CCDRuntime::FindEarliestTOI;
using CatEngine::Physics::CCDRuntime::IsFastBody;
using CatEngine::Physics::CCDRuntime::PrepassStats;

namespace {

// ----------------------------------------------------------------------------
// Synthetic-world helpers. Each test builds a std::vector<RigidBody> with the
// exact configuration needed to hit one pre-pass code path.
// ----------------------------------------------------------------------------

RigidBody makeSphere(const vec3& pos, float radius, const vec3& velocity) {
    RigidBody body;
    body.position = pos;
    body.linearVelocity = velocity;
    body.collider = Collider::Sphere(radius);
    // A sphere body has non-trivial inertia by default; tests don't care about
    // rotation so leaving the defaults is fine. invMass remains 1.0 so the
    // body is dynamic.
    return body;
}

RigidBody makeStaticBox(const vec3& pos, const vec3& halfExtents) {
    RigidBody body;
    body.position = pos;
    body.linearVelocity = vec3(0.0f);
    body.collider = Collider::Box(halfExtents);
    body.flags = RigidBodyFlags::Static;
    body.invMass = 0.0f;
    return body;
}

RigidBody makeStaticSphere(const vec3& pos, float radius) {
    RigidBody body;
    body.position = pos;
    body.linearVelocity = vec3(0.0f);
    body.collider = Collider::Sphere(radius);
    body.flags = RigidBodyFlags::Static;
    body.invMass = 0.0f;
    return body;
}

RigidBody makeCapsule(const vec3& pos, float radius, float height,
                     const vec3& velocity) {
    RigidBody body;
    body.position = pos;
    body.linearVelocity = velocity;
    body.collider = Collider::Capsule(radius, height);
    return body;
}

// At 60 Hz physics the engine ships 1/60 s fixed timesteps. Use the same dt
// in every test so the "does it tunnel" arithmetic reflects real gameplay.
constexpr float kDt60Hz = 1.0f / 60.0f;
constexpr float kEps = 1e-4f;

} // anon

// ---------------------------------------------------------------------------
// Marquee acceptance-bar test: a fast projectile going into a thin wall at
// 60 Hz cannot tunnel through it. Without the pre-pass the projectile's
// per-frame displacement exceeds the wall's thickness, so a purely discrete
// integrator would jump past the wall between frames.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: bullet-speed projectile cannot tunnel through a thin wall",
          "[ccd_prepass][acceptance]") {
    // Configuration matches the acceptance-bar description:
    //   projectile radius 0.1 m, velocity 600 m/s toward +X
    //   wall at x = 5.0, half-extent 0.1 m on x (0.2 m thick), big in y/z
    //
    // At 600 m/s and dt = 1/60 the projectile's frame displacement is exactly
    // 10.0 m — vastly greater than the 0.2 m wall thickness AND greater than
    // the 5 m distance to the wall. With a discrete integrator and no CCD
    // the next-frame position would land at x = 10.0, clean on the far side
    // of the wall. Pistol bullets run ~400 m/s, rifle rounds ~900 m/s, so
    // 600 m/s is a realistic tunneling-worst-case for a combat game.
    std::vector<RigidBody> bodies;
    bodies.push_back(makeSphere(vec3(0.0f, 0.0f, 0.0f),
                                0.1f,
                                vec3(600.0f, 0.0f, 0.0f)));
    bodies.push_back(makeStaticBox(vec3(5.0f, 0.0f, 0.0f),
                                   vec3(0.1f, 100.0f, 100.0f)));

    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);

    // At least one fast body was considered; at least one was clamped.
    REQUIRE(stats.fastBodiesConsidered >= 1);
    REQUIRE(stats.bodiesClamped >= 1);
    REQUIRE(stats.smallestTOI < 1.0f);

    // Post-clamp, the projectile's frame displacement must land the surface
    // strictly INSIDE the wall's near face (x = 4.9) — i.e. no tunneling.
    // The centre + radius + displacement must not cross 5.0 (the start of the
    // wall).
    const vec3& v = bodies[0].linearVelocity;
    const float postFrameCentreX = bodies[0].position.x + v.x * kDt60Hz;
    const float postFrameSurfaceX = postFrameCentreX + 0.1f;
    // The integrator will land short of the wall's near face by the safety
    // factor — strictly less than 4.9 (wall's near face at x = 4.9, expanded
    // by sphere radius 0.1 → Minkowski contact at centre x = 4.8, scaled by
    // 0.99 safety gives the clamp target).
    REQUIRE(postFrameSurfaceX < 5.0f);
    // And it had to actually move forward: not clamped to zero, not static.
    REQUIRE(v.x > 0.0f);
}

// ---------------------------------------------------------------------------
// Gatekeeper: a slow body with `|v|*dt` smaller than half its radius is NOT
// considered fast, and the pre-pass leaves it alone even if it's on a path
// that would otherwise hit a wall.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: slow body below the speed threshold is not clamped",
          "[ccd_prepass][gatekeeper]") {
    std::vector<RigidBody> bodies;
    // Projectile radius 0.5, velocity 1 m/s. Per-frame displacement at 60 Hz
    // is 0.0167 m — well below half-radius (0.25 m). IsFastBody returns false.
    bodies.push_back(makeSphere(vec3(0.0f), 0.5f, vec3(1.0f, 0.0f, 0.0f)));
    bodies.push_back(makeStaticBox(vec3(5.0f, 0.0f, 0.0f),
                                   vec3(0.1f, 100.0f, 100.0f)));

    const vec3 velocityBefore = bodies[0].linearVelocity;
    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);

    REQUIRE(stats.fastBodiesConsidered == 0);
    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(bodies[0].linearVelocity.x == Approx(velocityBefore.x));
}

// ---------------------------------------------------------------------------
// Gatekeeper: a fast body whose path DOES NOT hit anything isn't clamped.
// The swept math returns no hit → velocity stays intact.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: fast body on an empty trajectory keeps its velocity",
          "[ccd_prepass][gatekeeper]") {
    std::vector<RigidBody> bodies;
    // Fast but aimed at empty space.
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f)));
    // Obstacle placed at y = 10, which the projectile never reaches.
    bodies.push_back(makeStaticBox(vec3(0.0f, 10.0f, 0.0f),
                                   vec3(1.0f, 0.1f, 1.0f)));

    const vec3 velocityBefore = bodies[0].linearVelocity;
    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);

    REQUIRE(stats.fastBodiesConsidered == 1);
    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(bodies[0].linearVelocity.x == Approx(velocityBefore.x));
}

// ---------------------------------------------------------------------------
// Gatekeeper: a static body never has its velocity clamped, even if some
// OTHER fast body is pointed at it. The fast body gets clamped; the static
// body remains untouched.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: static bodies are never clamped",
          "[ccd_prepass][gatekeeper]") {
    std::vector<RigidBody> bodies;
    // Fast projectile toward +X.
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f)));
    // Static box directly in the path.
    bodies.push_back(makeStaticBox(vec3(2.0f, 0.0f, 0.0f),
                                   vec3(0.1f, 100.0f, 100.0f)));

    // Sanity check: the static box has zero velocity going in.
    REQUIRE(bodies[1].linearVelocity.x == Approx(0.0f));

    ApplyCCDPrepass(bodies, kDt60Hz);

    // Box remains untouched — clamping a body with zero velocity is a no-op
    // even if it were considered, but the gatekeeper skips statics entirely.
    REQUIRE(bodies[1].linearVelocity.x == Approx(0.0f));
    REQUIRE(bodies[1].linearVelocity.y == Approx(0.0f));
    REQUIRE(bodies[1].linearVelocity.z == Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Gatekeeper: a sleeping body isn't considered for CCD. Real-game example:
// a dormant enemy at a distant spawn point should never run sweep math.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: sleeping fast bodies are skipped",
          "[ccd_prepass][gatekeeper]") {
    std::vector<RigidBody> bodies;
    RigidBody fast = makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f));
    fast.flags = RigidBodyFlags::Sleeping;
    bodies.push_back(fast);
    bodies.push_back(makeStaticBox(vec3(2.0f, 0.0f, 0.0f),
                                   vec3(0.1f, 100.0f, 100.0f)));

    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);
    REQUIRE(stats.fastBodiesConsidered == 0);
    REQUIRE(stats.bodiesClamped == 0);
    // Sleeping body's velocity is whatever the caller set — shouldn't mutate.
    REQUIRE(bodies[0].linearVelocity.x == Approx(60.0f));
}

// ---------------------------------------------------------------------------
// Gatekeeper: a trigger body doesn't collide physically, so the pre-pass
// doesn't consider it either as a fast body OR as an obstacle.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: trigger bodies are not considered as fast bodies",
          "[ccd_prepass][gatekeeper]") {
    std::vector<RigidBody> bodies;
    RigidBody fast = makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f));
    fast.flags = RigidBodyFlags::Trigger;
    bodies.push_back(fast);
    bodies.push_back(makeStaticBox(vec3(2.0f, 0.0f, 0.0f),
                                   vec3(0.1f, 100.0f, 100.0f)));

    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);
    REQUIRE(stats.fastBodiesConsidered == 0);
}

TEST_CASE("CCDRuntime: trigger bodies are not considered as obstacles",
          "[ccd_prepass][gatekeeper]") {
    std::vector<RigidBody> bodies;
    // Fast projectile.
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f)));
    // A "trigger zone" box — must not clamp the projectile's motion even
    // though its swept path would intersect the zone.
    RigidBody triggerZone = makeStaticBox(vec3(2.0f, 0.0f, 0.0f),
                                          vec3(0.1f, 100.0f, 100.0f));
    triggerZone.flags = RigidBodyFlags::Trigger;
    bodies.push_back(triggerZone);

    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);
    REQUIRE(stats.fastBodiesConsidered == 1);
    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(bodies[0].linearVelocity.x == Approx(60.0f));
}

// ---------------------------------------------------------------------------
// Fast sphere vs static sphere — analytic quadratic-root TOI path.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: fast sphere vs static sphere is clamped before contact",
          "[ccd_prepass][sphere_sphere]") {
    std::vector<RigidBody> bodies;
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f)));
    bodies.push_back(makeStaticSphere(vec3(2.0f, 0.0f, 0.0f), 0.1f));

    ApplyCCDPrepass(bodies, kDt60Hz);

    // Post-clamp: centres can't cross each other. Projectile centre + its
    // new displacement must stop before the obstacle's near surface
    // (x = 2.0 - 0.1 = 1.9) adjusted for the projectile's own radius.
    const vec3& v = bodies[0].linearVelocity;
    const float postFrameCentreX = bodies[0].position.x + v.x * kDt60Hz;
    REQUIRE(postFrameCentreX < 1.9f); // strictly short of contact
    REQUIRE(v.x > 0.0f);              // still moving forward
}

// ---------------------------------------------------------------------------
// Fast sphere vs DYNAMIC sphere — the relative-motion path. The obstacle
// itself is moving, so the pre-pass must compute TOI in the frame where
// the obstacle is stationary (v_rel = v_A - v_B).
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: fast sphere vs moving sphere uses relative motion",
          "[ccd_prepass][sphere_sphere]") {
    std::vector<RigidBody> bodies;
    // Both spheres moving right: A at 30 m/s, B at 15 m/s. Closing speed is
    // 15 m/s. A is behind B. At dt = 1/60 they close by 0.25 m. Start them
    // 1.0 m apart (centres) with 0.1 m radius each — they'll eventually
    // touch when their separation drops to 0.2 m.
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(30.0f, 0.0f, 0.0f)));
    bodies.push_back(makeSphere(vec3(1.0f, 0.0f, 0.0f), 0.1f,
                                vec3(15.0f, 0.0f, 0.0f)));

    // Sanity: without clamping, over one frame A goes to 0.5, B to 1.25 +
    // 0.1 = 1.35. Their centres don't tunnel THIS frame — the CCD kernel
    // should NOT clamp. This test pins the "don't over-clamp on a frame
    // that wasn't going to hit anyway" behaviour.
    const vec3 vA_before = bodies[0].linearVelocity;
    const vec3 vB_before = bodies[1].linearVelocity;

    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);

    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(bodies[0].linearVelocity.x == Approx(vA_before.x));
    REQUIRE(bodies[1].linearVelocity.x == Approx(vB_before.x));
}

TEST_CASE("CCDRuntime: two-body head-on collision clamps the faster body",
          "[ccd_prepass][sphere_sphere]") {
    std::vector<RigidBody> bodies;
    // Head-on closure at 60 m/s combined — at dt = 1/60 they close by 1 m,
    // but they're only 0.4 m apart (centres with 0.1 m radius each => 0.2 m
    // gap). Without CCD they'd swap sides: A would travel 0.667 m to end
    // past B, while B would travel 0.333 m back past A.
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(40.0f, 0.0f, 0.0f)));
    bodies.push_back(makeSphere(vec3(0.4f, 0.0f, 0.0f), 0.1f,
                                vec3(-20.0f, 0.0f, 0.0f)));

    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);

    // Both are "fast" by the predicate; both get clamped. The pre-pass does
    // one sweep pass per body so the per-frame clamp isn't optimal (some
    // sub-frame overlap can remain, which the downstream solver resolves via
    // a contact on the next step), but the tunneling-prevention invariant
    // holds: centres don't swap sides.
    REQUIRE(stats.fastBodiesConsidered == 2);
    REQUIRE(stats.bodiesClamped >= 1);
    REQUIRE(stats.smallestTOI < 1.0f);

    // Anti-tunneling invariant: A's centre must remain on A's original side
    // of B's centre at the end of the frame. Without CCD A's frame-end x
    // would be 0.667 while B's would be 0.067 — swapped. With CCD A stays
    // on the -X side of B even if they slightly overlap.
    const float postCentreA = bodies[0].position.x +
                              bodies[0].linearVelocity.x * kDt60Hz;
    const float postCentreB = bodies[1].position.x +
                              bodies[1].linearVelocity.x * kDt60Hz;
    REQUIRE(postCentreA < postCentreB);
}

// ---------------------------------------------------------------------------
// Fast capsule vs static box — capsule approximated as sphere of capsule
// radius, per the file-header rationale.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: fast capsule cannot tunnel through a thin wall",
          "[ccd_prepass][capsule]") {
    std::vector<RigidBody> bodies;
    // Capsule at 600 m/s (bullet-speed) toward a wall 2 m away. At dt = 1/60
    // the frame displacement is 10 m — the capsule would jump clear through
    // the wall without CCD.
    bodies.push_back(makeCapsule(vec3(0.0f), 0.1f, 0.4f,
                                 vec3(600.0f, 0.0f, 0.0f)));
    bodies.push_back(makeStaticBox(vec3(2.0f, 0.0f, 0.0f),
                                   vec3(0.1f, 100.0f, 100.0f)));

    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);
    REQUIRE(stats.fastBodiesConsidered == 1);
    REQUIRE(stats.bodiesClamped == 1);
    REQUIRE(stats.smallestTOI < 1.0f);
}

// ---------------------------------------------------------------------------
// Post-clamp safety factor — the velocity scale must leave a sub-frame gap
// between the TOI pose and the actual contact so the narrow phase has a
// separation to work with. If the clamp factor were exactly `toi` (no
// safety margin) the integrator would land EXACTLY at contact, which
// round-trips through the solver as "penetrating" due to float round-off.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: clamp leaves sub-frame separation from contact",
          "[ccd_prepass][safety]") {
    std::vector<RigidBody> bodies;
    // Same bullet-speed / near-wall setup as the marquee test — the
    // projectile would land well past the wall at x = 5 without CCD. Here
    // we pin the inverse: after the clamp, the projectile's frame-end
    // surface must land strictly short of the wall face (safety factor
    // leaves a sliver of separation) AND strictly past x = 4.0 (the clamp
    // isn't so conservative it stops the projectile well before contact —
    // it should still consume most of the frame's trajectory budget).
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(600.0f, 0.0f, 0.0f)));
    bodies.push_back(makeStaticBox(vec3(5.0f, 0.0f, 0.0f),
                                   vec3(0.1f, 100.0f, 100.0f)));

    ApplyCCDPrepass(bodies, kDt60Hz);

    // Target: centre + v*dt + radius < 5.0 (strict separation, don't park
    // exactly on the surface — round-off would decode as penetration).
    const float postSurface =
        bodies[0].position.x + bodies[0].linearVelocity.x * kDt60Hz + 0.1f;
    REQUIRE(postSurface < 5.0f);
    // The gap exists but is small — we want most of the frame's budget to
    // still be consumed, not a no-op. Safety factor is 0.99, TOI for this
    // config is ~0.48 → scaled velocity ~ 285 m/s → post-frame surface at
    // ~ 4.85 m. So > 4.0 m is a loose lower bound that fires on a
    // regression (e.g. someone drops safety to 0.5 accidentally).
    REQUIRE(postSurface > 4.0f);
}

// ---------------------------------------------------------------------------
// Empty / degenerate inputs — the pre-pass must be safe on empty lists,
// singletons, and zero / negative dt.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime: empty body list is a no-op",
          "[ccd_prepass][degenerate]") {
    std::vector<RigidBody> bodies;
    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);
    REQUIRE(stats.fastBodiesConsidered == 0);
    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(stats.smallestTOI == Approx(1.0f));
}

TEST_CASE("CCDRuntime: single-body list is a no-op",
          "[ccd_prepass][degenerate]") {
    std::vector<RigidBody> bodies;
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f)));
    PrepassStats stats = ApplyCCDPrepass(bodies, kDt60Hz);
    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(bodies[0].linearVelocity.x == Approx(60.0f));
}

TEST_CASE("CCDRuntime: zero dt is a no-op",
          "[ccd_prepass][degenerate]") {
    std::vector<RigidBody> bodies;
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f)));
    bodies.push_back(makeStaticBox(vec3(0.2f, 0.0f, 0.0f),
                                   vec3(0.05f, 1.0f, 1.0f)));
    PrepassStats stats = ApplyCCDPrepass(bodies, 0.0f);
    REQUIRE(stats.fastBodiesConsidered == 0);
    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(bodies[0].linearVelocity.x == Approx(60.0f));
}

TEST_CASE("CCDRuntime: negative dt is a no-op",
          "[ccd_prepass][degenerate]") {
    std::vector<RigidBody> bodies;
    bodies.push_back(makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f)));
    bodies.push_back(makeStaticBox(vec3(0.2f, 0.0f, 0.0f),
                                   vec3(0.05f, 1.0f, 1.0f)));
    PrepassStats stats = ApplyCCDPrepass(bodies, -1.0f / 60.0f);
    REQUIRE(stats.bodiesClamped == 0);
    REQUIRE(bodies[0].linearVelocity.x == Approx(60.0f));
}

// ---------------------------------------------------------------------------
// IsFastBody predicate — directly test the gatekeeper on each body flag
// combination so a regression in the predicate shows up here even if the
// downstream ApplyCCDPrepass tests happen to compensate.
// ---------------------------------------------------------------------------
TEST_CASE("CCDRuntime::IsFastBody: static body is never fast",
          "[ccd_prepass][is_fast]") {
    RigidBody b = makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f));
    b.flags = RigidBodyFlags::Static;
    REQUIRE(IsFastBody(b, kDt60Hz) == false);
}

TEST_CASE("CCDRuntime::IsFastBody: sleeping body is never fast",
          "[ccd_prepass][is_fast]") {
    RigidBody b = makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f));
    b.flags = RigidBodyFlags::Sleeping;
    REQUIRE(IsFastBody(b, kDt60Hz) == false);
}

TEST_CASE("CCDRuntime::IsFastBody: trigger body is never fast",
          "[ccd_prepass][is_fast]") {
    RigidBody b = makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f));
    b.flags = RigidBodyFlags::Trigger;
    REQUIRE(IsFastBody(b, kDt60Hz) == false);
}

TEST_CASE("CCDRuntime::IsFastBody: box collider is not considered fast",
          "[ccd_prepass][is_fast]") {
    // Box CCD isn't supported by the analytic kernels this iteration; the
    // predicate must reject boxes regardless of speed.
    RigidBody b;
    b.position = vec3(0.0f);
    b.linearVelocity = vec3(60.0f, 0.0f, 0.0f);
    b.collider = Collider::Box(vec3(0.1f));
    REQUIRE(IsFastBody(b, kDt60Hz) == false);
}

TEST_CASE("CCDRuntime::IsFastBody: zero-velocity body is not fast",
          "[ccd_prepass][is_fast]") {
    RigidBody b = makeSphere(vec3(0.0f), 0.1f, vec3(0.0f));
    REQUIRE(IsFastBody(b, kDt60Hz) == false);
}

TEST_CASE("CCDRuntime::IsFastBody: speed above threshold qualifies",
          "[ccd_prepass][is_fast]") {
    // radius 0.1, so half-radius 0.05. At 60 m/s and dt 1/60 displacement is
    // 1.0 m — enormous versus threshold.
    RigidBody b = makeSphere(vec3(0.0f), 0.1f, vec3(60.0f, 0.0f, 0.0f));
    REQUIRE(IsFastBody(b, kDt60Hz));
}

TEST_CASE("CCDRuntime::IsFastBody: speed below threshold does not qualify",
          "[ccd_prepass][is_fast]") {
    // radius 0.5, half-radius 0.25. At 1 m/s dt 1/60 displacement is 0.0167,
    // well under 0.25.
    RigidBody b = makeSphere(vec3(0.0f), 0.5f, vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(IsFastBody(b, kDt60Hz) == false);
}

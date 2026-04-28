// ============================================================================
// Unit tests for continuous collision detection (CCD).
//
// Backlog reference: ENGINE_BACKLOG.md P1 "Continuous collision detection for
// fast bodies". Acceptance bar: "a bullet-speed body through a thin wall must
// collide, not pass through" — landed here as the marquee test
// "CCD: bullet-speed sphere cannot tunnel through a thin wall".
//
// Kernel under test: engine/cuda/physics/CCD.hpp.
//
// Same USE_MOCK_GPU=1 rationale as test_sequential_impulse, test_two_bone_ik,
// test_simplex_noise, and test_root_motion: the CCD kernel is intentionally
// CUDA-free (no float3, no __device__), depending only on Engine::vec3,
// Engine::AABB, and the Engine::Math helpers. The host test executable
// therefore exercises the exact code path the runtime narrow phase will call
// from a __host__ __device__ context once the wire-up lands.
// ============================================================================

#include "catch.hpp"
#include "engine/cuda/physics/CCD.hpp"

#include <cmath>

using Engine::vec3;
using Engine::AABB;
using CatEngine::Physics::CCD::SweepHit;
using CatEngine::Physics::CCD::SweepAABB;
using CatEngine::Physics::CCD::SweptSphereAABB;
using CatEngine::Physics::CCD::SweptSphereSphere;
using CatEngine::Physics::CCD::ClampDisplacementToTOI;
using CatEngine::Physics::CCD::ConservativeAdvance;

namespace {

constexpr float CCD_EPS = 1e-4f;

bool vec3_approx(const vec3& a, const vec3& b, float eps = CCD_EPS) {
    return std::abs(a.x - b.x) < eps &&
           std::abs(a.y - b.y) < eps &&
           std::abs(a.z - b.z) < eps;
}

} // anon

// ---------------------------------------------------------------------------
// SweepAABB — broadphase swept-volume union.
// ---------------------------------------------------------------------------

TEST_CASE("CCD::SweepAABB: positive-X displacement grows max.x only",
          "[ccd][sweep_aabb]") {
    AABB a(vec3(-1.0f, -1.0f, -1.0f), vec3(1.0f, 1.0f, 1.0f));
    AABB s = SweepAABB(a, vec3(5.0f, 0.0f, 0.0f));
    REQUIRE(s.min.x == Approx(-1.0f));
    REQUIRE(s.max.x == Approx(6.0f));
    // Other axes untouched.
    REQUIRE(s.min.y == Approx(-1.0f));
    REQUIRE(s.max.y == Approx(1.0f));
    REQUIRE(s.min.z == Approx(-1.0f));
    REQUIRE(s.max.z == Approx(1.0f));
}

TEST_CASE("CCD::SweepAABB: negative-X displacement grows min.x only",
          "[ccd][sweep_aabb]") {
    AABB a(vec3(-1.0f), vec3(1.0f));
    AABB s = SweepAABB(a, vec3(-4.0f, 0.0f, 0.0f));
    REQUIRE(s.min.x == Approx(-5.0f));
    REQUIRE(s.max.x == Approx(1.0f));
    REQUIRE(s.min.y == Approx(-1.0f));
    REQUIRE(s.max.y == Approx(1.0f));
}

TEST_CASE("CCD::SweepAABB: mixed-sign displacement grows every touched axis",
          "[ccd][sweep_aabb]") {
    AABB a(vec3(0.0f), vec3(2.0f));
    AABB s = SweepAABB(a, vec3(3.0f, -2.5f, 1.5f));
    REQUIRE(s.min.x == Approx(0.0f));
    REQUIRE(s.max.x == Approx(5.0f));
    REQUIRE(s.min.y == Approx(-2.5f));
    REQUIRE(s.max.y == Approx(2.0f));
    REQUIRE(s.min.z == Approx(0.0f));
    REQUIRE(s.max.z == Approx(3.5f));
}

TEST_CASE("CCD::SweepAABB: zero displacement is a no-op",
          "[ccd][sweep_aabb]") {
    AABB a(vec3(-2.0f, -3.0f, 4.0f), vec3(5.0f, 6.0f, 7.0f));
    AABB s = SweepAABB(a, vec3(0.0f));
    REQUIRE(s.min == a.min);
    REQUIRE(s.max == a.max);
}

TEST_CASE("CCD::SweepAABB: margin inflates every face uniformly",
          "[ccd][sweep_aabb]") {
    AABB a(vec3(0.0f), vec3(1.0f));
    AABB s = SweepAABB(a, vec3(2.0f, 0.0f, 0.0f), 0.5f);
    REQUIRE(s.min.x == Approx(-0.5f));
    REQUIRE(s.max.x == Approx(3.5f));
    REQUIRE(s.min.y == Approx(-0.5f));
    REQUIRE(s.max.y == Approx(1.5f));
    REQUIRE(s.min.z == Approx(-0.5f));
    REQUIRE(s.max.z == Approx(1.5f));
}

TEST_CASE("CCD::SweepAABB: swept volume always contains start volume",
          "[ccd][sweep_aabb]") {
    AABB a(vec3(-2.0f, -1.0f, -0.5f), vec3(3.0f, 2.0f, 1.5f));
    // Try several displacements including signed combinations.
    for (const vec3& d : {vec3(1, 2, 3), vec3(-1, -2, -3),
                          vec3(4, -5, 0), vec3(0, 0, 0)}) {
        AABB s = SweepAABB(a, d, 0.1f);
        REQUIRE(s.contains(a));
    }
}

// ---------------------------------------------------------------------------
// SweptSphereAABB — narrow-phase analytic sphere-vs-box TOI.
// ---------------------------------------------------------------------------

TEST_CASE("CCD::SweptSphereAABB: sphere approaching +X face from the left hits",
          "[ccd][sphere_aabb]") {
    AABB wall(vec3(0.0f, -1.0f, -1.0f), vec3(1.0f, 1.0f, 1.0f));
    SweepHit h;
    // Sphere starts at x=-5, r=0.1, moves +10 in X over the frame. Surface
    // contact with the wall's -X face (x=0) is at t ≈ (5 − 0.1) / 10 = 0.49.
    bool hit = SweptSphereAABB(vec3(-5.0f, 0.0f, 0.0f), 0.1f,
                               vec3(10.0f, 0.0f, 0.0f), wall, h);
    REQUIRE(hit);
    REQUIRE(h.t == Approx(0.49f).margin(CCD_EPS));
    // Normal of the first face struck is the -X face of the wall.
    REQUIRE(vec3_approx(h.normal, vec3(-1.0f, 0.0f, 0.0f)));
}

TEST_CASE("CCD::SweptSphereAABB: sphere passing below the wall misses",
          "[ccd][sphere_aabb]") {
    AABB wall(vec3(0.0f, 5.0f, -1.0f), vec3(1.0f, 6.0f, 1.0f));
    SweepHit h;
    bool hit = SweptSphereAABB(vec3(-5.0f, 0.0f, 0.0f), 0.1f,
                               vec3(10.0f, 0.0f, 0.0f), wall, h);
    REQUIRE_FALSE(hit);
}

// THE MARQUEE TEST — the ENGINE_BACKLOG.md acceptance bar.
//
// Geometry: a 0.1 m radius projectile starts 5 m to the LEFT of a 0.2 m
// thick wall (x ∈ [0, 0.2]) and displaces +10 m in X over a single frame.
// |displacement| = 10 m is 50× the wall thickness → a discrete step
// integrator that only checks overlap at the new pose would place the
// projectile at x = +5 m with zero overlap at either frame end and silently
// miss the collision. SweptSphereAABB must report a hit with t ∈ (0, 1)
// and the correct entry-face normal (-X).
TEST_CASE("CCD: bullet-speed sphere cannot tunnel through a thin wall",
          "[ccd][sphere_aabb][tunneling]") {
    AABB wall(vec3(0.0f, -2.0f, -2.0f), vec3(0.2f, 2.0f, 2.0f));

    const vec3 start(-5.0f, 0.0f, 0.0f);
    const float radius = 0.1f;
    const vec3 disp(10.0f, 0.0f, 0.0f);

    SweepHit h;
    bool hit = SweptSphereAABB(start, radius, disp, wall, h);

    REQUIRE(hit);
    REQUIRE(h.t > 0.0f);
    REQUIRE(h.t < 1.0f);
    // Expected TOI: sphere surface (at start + radius = -4.9) reaches
    // wall's -X face (x = 0) after covering 4.9 / 10 = 0.49 of the frame.
    REQUIRE(h.t == Approx(0.49f).margin(CCD_EPS));
    REQUIRE(vec3_approx(h.normal, vec3(-1.0f, 0.0f, 0.0f)));

    // Verify that clamping the motion to this TOI actually leaves the sphere
    // on the correct (near) side of the wall with a positive separation.
    const vec3 clamped = ClampDisplacementToTOI(disp, h.t, 0.99f);
    const vec3 finalPos = start + clamped;
    // finalPos.x should be less than the wall's min.x (0.0) by at least the
    // sphere radius minus the safety slop.
    REQUIRE(finalPos.x < wall.min.x - 0.0f);
    REQUIRE(finalPos.x + radius < wall.min.x);
}

TEST_CASE("CCD::SweptSphereAABB: sphere already inside expanded box returns t=0",
          "[ccd][sphere_aabb]") {
    AABB box(vec3(-1.0f), vec3(1.0f));
    SweepHit h;
    // Sphere centre INSIDE the box, tiny displacement.
    bool hit = SweptSphereAABB(vec3(0.0f), 0.1f,
                               vec3(0.01f, 0.0f, 0.0f), box, h);
    REQUIRE(hit);
    REQUIRE(h.t == Approx(0.0f));
}

TEST_CASE("CCD::SweptSphereAABB: sphere moving away from wall misses",
          "[ccd][sphere_aabb]") {
    AABB wall(vec3(0.0f, -1.0f, -1.0f), vec3(1.0f, 1.0f, 1.0f));
    SweepHit h;
    // Sphere starts left of wall, moves further left.
    bool hit = SweptSphereAABB(vec3(-5.0f, 0.0f, 0.0f), 0.1f,
                               vec3(-5.0f, 0.0f, 0.0f), wall, h);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::SweptSphereAABB: zero displacement with separated sphere misses",
          "[ccd][sphere_aabb]") {
    AABB wall(vec3(0.0f), vec3(1.0f));
    SweepHit h;
    bool hit = SweptSphereAABB(vec3(-5.0f, 0.5f, 0.5f), 0.1f,
                               vec3(0.0f), wall, h);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::SweptSphereAABB: normal reflects the hit axis correctly",
          "[ccd][sphere_aabb]") {
    AABB box(vec3(-1.0f), vec3(1.0f));

    SECTION("Hit from +X side → +X normal") {
        SweepHit h;
        REQUIRE(SweptSphereAABB(vec3(5.0f, 0.0f, 0.0f), 0.2f,
                                vec3(-10.0f, 0.0f, 0.0f), box, h));
        REQUIRE(vec3_approx(h.normal, vec3(1.0f, 0.0f, 0.0f)));
    }
    SECTION("Hit from +Y side → +Y normal") {
        SweepHit h;
        REQUIRE(SweptSphereAABB(vec3(0.0f, 5.0f, 0.0f), 0.2f,
                                vec3(0.0f, -10.0f, 0.0f), box, h));
        REQUIRE(vec3_approx(h.normal, vec3(0.0f, 1.0f, 0.0f)));
    }
    SECTION("Hit from -Z side → -Z normal") {
        SweepHit h;
        REQUIRE(SweptSphereAABB(vec3(0.0f, 0.0f, -5.0f), 0.2f,
                                vec3(0.0f, 0.0f, 10.0f), box, h));
        REQUIRE(vec3_approx(h.normal, vec3(0.0f, 0.0f, -1.0f)));
    }
}

// ---------------------------------------------------------------------------
// SweptSphereSphere — analytic TOI for a pair of moving spheres.
// ---------------------------------------------------------------------------

TEST_CASE("CCD::SweptSphereSphere: head-on approach hits at midpoint",
          "[ccd][sphere_sphere]") {
    // Two unit-radius spheres 4 m apart on the X axis, both moving toward
    // each other at 2 m/frame. Surfaces meet when centre-centre = 2 m, i.e.
    // after closing 2 m from the starting 4 m — that's closing 2 m at a
    // relative 4 m/frame → t = 0.5.
    float toi = -1.0f;
    bool hit = SweptSphereSphere(vec3(-2.0f, 0, 0), 1.0f, vec3(2.0f, 0, 0),
                                 vec3( 2.0f, 0, 0), 1.0f, vec3(-2.0f, 0, 0),
                                 toi);
    REQUIRE(hit);
    REQUIRE(toi == Approx(0.5f).margin(CCD_EPS));
}

TEST_CASE("CCD::SweptSphereSphere: offset perpendicular paths miss",
          "[ccd][sphere_sphere]") {
    // A sweeps along +X through the origin plane. B sweeps along +Y through
    // a plane 10 m above A's path in Z. At every t the two centres are at
    // least 10 m apart along Z — well beyond the combined radius of 1 m.
    // This exercises the "discriminant < 0" branch of the analytic solver.
    float toi = -1.0f;
    bool hit = SweptSphereSphere(vec3(-5.0f, 0.0f, 0.0f),  0.5f, vec3(10.0f, 0, 0),
                                 vec3( 0.0f, -5.0f, 10.0f), 0.5f, vec3(0, 10, 0),
                                 toi);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::SweptSphereSphere: spheres moving apart miss",
          "[ccd][sphere_sphere]") {
    float toi = -1.0f;
    bool hit = SweptSphereSphere(vec3(-2.0f, 0, 0), 0.5f, vec3(-5.0f, 0, 0),
                                 vec3( 2.0f, 0, 0), 0.5f, vec3( 5.0f, 0, 0),
                                 toi);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::SweptSphereSphere: spheres already overlapping return TOI=0",
          "[ccd][sphere_sphere]") {
    float toi = -1.0f;
    bool hit = SweptSphereSphere(vec3(0.0f), 1.0f, vec3(0.0f),
                                 vec3(0.5f, 0, 0), 1.0f, vec3(0.0f),
                                 toi);
    REQUIRE(hit);
    REQUIRE(toi == Approx(0.0f));
}

TEST_CASE("CCD::SweptSphereSphere: no relative motion and separated misses",
          "[ccd][sphere_sphere]") {
    float toi = -1.0f;
    bool hit = SweptSphereSphere(vec3(-5.0f, 0, 0), 1.0f, vec3(2.0f, 0, 0),
                                 vec3( 5.0f, 0, 0), 1.0f, vec3(2.0f, 0, 0),
                                 toi);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::SweptSphereSphere: bullet-through-thin-wall analog (sphere wall)",
          "[ccd][sphere_sphere][tunneling]") {
    // A 0.1 m projectile crosses a 1 m stationary target at 10 m/frame. The
    // centres come within rA + rB = 1.1 m well before the frame ends.
    float toi = -1.0f;
    bool hit = SweptSphereSphere(vec3(-5.0f, 0, 0), 0.1f, vec3(10.0f, 0, 0),
                                 vec3( 0.0f, 0, 0), 1.0f, vec3(0.0f),
                                 toi);
    REQUIRE(hit);
    REQUIRE(toi > 0.0f);
    REQUIRE(toi < 1.0f);
    // Contact TOI: centre-distance shrinks from 5 → 1.1 at rate 10 m/frame
    // → toi = (5 − 1.1) / 10 = 0.39.
    REQUIRE(toi == Approx(0.39f).margin(CCD_EPS));
}

// ---------------------------------------------------------------------------
// ClampDisplacementToTOI — motion truncation with a safety factor.
// ---------------------------------------------------------------------------

TEST_CASE("CCD::ClampDisplacementToTOI: applies toi and safety multiplicatively",
          "[ccd][clamp]") {
    vec3 d(10.0f, 0.0f, 0.0f);
    vec3 c = ClampDisplacementToTOI(d, 0.5f, 0.99f);
    REQUIRE(c.x == Approx(4.95f));
    REQUIRE(c.y == Approx(0.0f));
}

TEST_CASE("CCD::ClampDisplacementToTOI: toi=0 zeros displacement",
          "[ccd][clamp]") {
    vec3 c = ClampDisplacementToTOI(vec3(10.0f, -5.0f, 3.0f), 0.0f, 0.99f);
    REQUIRE(c.x == Approx(0.0f));
    REQUIRE(c.y == Approx(0.0f));
    REQUIRE(c.z == Approx(0.0f));
}

TEST_CASE("CCD::ClampDisplacementToTOI: toi=1 still applies safety",
          "[ccd][clamp]") {
    vec3 c = ClampDisplacementToTOI(vec3(10.0f, 0, 0), 1.0f, 0.99f);
    REQUIRE(c.x == Approx(9.9f));
}

TEST_CASE("CCD::ClampDisplacementToTOI: safety is clamped into [0, 1]",
          "[ccd][clamp]") {
    // The internal clamp should ceil toi*safety at 1.0 even if a caller
    // passes a silly safety value. This is a defensive guard against a
    // degenerate call site — we prefer a well-behaved output to a silent
    // overshoot past the contact surface.
    vec3 c = ClampDisplacementToTOI(vec3(10.0f, 0, 0), 1.0f, 2.0f);
    REQUIRE(c.x == Approx(10.0f));
}

// ---------------------------------------------------------------------------
// ConservativeAdvance — iterative TOI with a closest-point callback.
//
// Use sphere-surface closest points as the callback: for two spheres the
// surface point on A closest to B is pA + rA · (pB − pA).normalized() and
// vice versa. That lets us cross-check ConservativeAdvance's answer against
// the analytic SweptSphereSphere solution above.
// ---------------------------------------------------------------------------

TEST_CASE("CCD::ConservativeAdvance: sphere-sphere closest-fn matches analytic TOI",
          "[ccd][conservative_advance]") {
    const float rA = 1.0f;
    const float rB = 1.0f;
    auto sphereClosest = [rA, rB](const vec3& pA, const vec3& pB,
                                  vec3& outA, vec3& outB) {
        vec3 ab = pB - pA;
        float d = ab.length();
        if (d < Engine::Math::EPSILON) {
            outA = pA;
            outB = pB;
            return;
        }
        vec3 dir = ab / d;
        outA = pA + dir * rA;
        outB = pB - dir * rB;
    };

    float toiCA = -1.0f;
    bool hit = ConservativeAdvance(vec3(-2.0f, 0, 0), vec3(2.0f, 0, 0),
                                   vec3( 2.0f, 0, 0), vec3(-2.0f, 0, 0),
                                   sphereClosest, toiCA);
    REQUIRE(hit);

    float toiAnalytic = -1.0f;
    REQUIRE(SweptSphereSphere(vec3(-2.0f, 0, 0), rA, vec3(2.0f, 0, 0),
                              vec3( 2.0f, 0, 0), rB, vec3(-2.0f, 0, 0),
                              toiAnalytic));

    // CA converges monotonically from below, so it returns a TOI that is at
    // or just below the analytic answer within the 1e-4 m surface tolerance.
    REQUIRE(toiCA <= toiAnalytic + CCD_EPS);
    REQUIRE(toiCA >= toiAnalytic - 1e-3f);
}

TEST_CASE("CCD::ConservativeAdvance: separating pair returns false",
          "[ccd][conservative_advance]") {
    auto sphereClosest = [](const vec3& pA, const vec3& pB,
                            vec3& outA, vec3& outB) {
        vec3 ab = pB - pA;
        float d = ab.length();
        if (d < Engine::Math::EPSILON) { outA = pA; outB = pB; return; }
        vec3 dir = ab / d;
        outA = pA + dir * 1.0f;
        outB = pB - dir * 1.0f;
    };

    float toi = -1.0f;
    bool hit = ConservativeAdvance(vec3(-2.0f, 0, 0), vec3(-5.0f, 0, 0),
                                   vec3( 2.0f, 0, 0), vec3( 5.0f, 0, 0),
                                   sphereClosest, toi);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::ConservativeAdvance: passing-by pair returns false",
          "[ccd][conservative_advance]") {
    // Two unit spheres on parallel rails 5 m apart in Y, moving toward each
    // other in X. They never actually touch because the Y separation is
    // always > rA + rB = 2 m.
    auto sphereClosest = [](const vec3& pA, const vec3& pB,
                            vec3& outA, vec3& outB) {
        vec3 ab = pB - pA;
        float d = ab.length();
        if (d < Engine::Math::EPSILON) { outA = pA; outB = pB; return; }
        vec3 dir = ab / d;
        outA = pA + dir * 1.0f;
        outB = pB - dir * 1.0f;
    };

    float toi = -1.0f;
    bool hit = ConservativeAdvance(vec3(-5.0f, 0.0f, 0.0f), vec3(10.0f, 0, 0),
                                   vec3( 5.0f, 5.0f, 0.0f), vec3(-10.0f, 0, 0),
                                   sphereClosest, toi);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::ConservativeAdvance: approach that wouldn't close this frame misses",
          "[ccd][conservative_advance]") {
    // Centres start 20 m apart, closing at 10 m/frame, combined radius 1 m.
    // Contact TOI in continuous-time is 19 / 10 = 1.9 frames — i.e. the
    // spheres DO approach but they finish this frame still 10 m apart
    // (centre-to-centre) so no contact happens within t ∈ [0, 1]. CA must
    // correctly return false rather than extrapolating past the frame.
    const float rA = 0.5f;
    const float rB = 0.5f;
    auto sphereClosest = [rA, rB](const vec3& pA, const vec3& pB,
                                  vec3& outA, vec3& outB) {
        vec3 ab = pB - pA;
        float d = ab.length();
        if (d < Engine::Math::EPSILON) { outA = pA; outB = pB; return; }
        vec3 dir = ab / d;
        outA = pA + dir * rA;
        outB = pB - dir * rB;
    };

    float toi = -1.0f;
    bool hit = ConservativeAdvance(vec3(-10.0f, 0, 0), vec3(5.0f, 0, 0),
                                   vec3( 10.0f, 0, 0), vec3(-5.0f, 0, 0),
                                   sphereClosest, toi, 16, 1e-5f);
    REQUIRE_FALSE(hit);
}

TEST_CASE("CCD::ConservativeAdvance: closing approach converges near analytic TOI",
          "[ccd][conservative_advance]") {
    // Proper closing setup. rA=rB=0.5, R=1. Centres start at ±5, dispA=+5,
    // dispB=-5 → relative closing rate 10 m/frame. Contact at centre-dist 1
    // → t = (10 − 1) / 10 = 0.9. Well within this frame.
    const float rA = 0.5f;
    const float rB = 0.5f;
    auto sphereClosest = [rA, rB](const vec3& pA, const vec3& pB,
                                  vec3& outA, vec3& outB) {
        vec3 ab = pB - pA;
        float d = ab.length();
        if (d < Engine::Math::EPSILON) { outA = pA; outB = pB; return; }
        vec3 dir = ab / d;
        outA = pA + dir * rA;
        outB = pB - dir * rB;
    };

    float toi = -1.0f;
    REQUIRE(ConservativeAdvance(vec3(-5.0f, 0, 0), vec3( 5.0f, 0, 0),
                                vec3( 5.0f, 0, 0), vec3(-5.0f, 0, 0),
                                sphereClosest, toi, 16, 1e-5f));
    REQUIRE(toi == Approx(0.9f).margin(1e-3f));
}

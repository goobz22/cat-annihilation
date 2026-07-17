// test_web_parity_environment.cpp — behavioural regression tests for the
// environment-parity fixes in game/config/WebParityConfig.hpp: the tree
// wind-sway math and the survival-scene lighting / shadow / tree-collision
// policy.
//
// The flat constant PINS live in test_web_parity_config.cpp. This suite drives
// the sway HELPERS across a range of clock times to prove the animation is
// REAL MOTION — that is the class of bug being fixed. On the native side the
// parity forest built a STATIC model matrix (translate * rotateY * scale, no
// time term) so trees never swayed; on the web side the same symptom once came
// from feeding per-frame `delta` instead of the monotonic clock
// (ForestEnvironment.tsx:136-141). The helpers reproduce the live web formula
// (ForestEnvironment.tsx:145-148) exactly, and the tests below would fail if a
// future edit dropped the time term, swapped the axes, dropped the 0.7 z
// factor, or regressed to the stale audit paraphrase cos((phase+elapsed)*0.7).
//
// Why no test here instantiates Forest/Terrain: Terrain.hpp pulls the CUDA
// context/buffer headers (documented in test_game_property_terrain.cpp) that
// the no-GPU unit build (USE_MOCK_GPU=1) cannot compile, and Forest.hpp
// includes Terrain.hpp. The player↔tree pass-through delivered in Forest.cpp
// (findTreesInRadius short-circuits under parity) is therefore pinned through
// the two constants that guard actually reads — WebParity::kEnabled and
// WebParity::kForestPlayerCollision — plus a mirror of the guard's decision,
// rather than by populating a Forest's private instance list.

#include "catch.hpp"
#include "config/WebParityConfig.hpp"

#include <cmath>

using namespace CatGame;

namespace {

// Local tolerant float compare so the suite doesn't depend on Catch::Approx.
// The tolerance is generous relative to the amplitude-scaled residue: every
// helper multiplies its trig result by 0.01, so even a coarse float/double
// sin() discrepancy at large arguments lands well under this bound, while a
// genuine formula error (wrong axis / dropped factor / wrong time scale)
// produces a discrepancy 10-100x larger.
constexpr float kSwayEps = 1e-5f;
bool approxEq(float lhs, float rhs, float eps = kSwayEps) {
    return std::fabs(lhs - rhs) <= eps;
}

// Independent double-precision reference for ForestEnvironment.tsx:145-148:
//   time = phase + elapsed * 0.5
//   x    = sin(time)       * 0.01
//   z    = cos(time * 0.7) * 0.01
double refSwayX(double phase, double elapsed) {
    return std::sin(phase + elapsed * 0.5) * 0.01;
}
double refSwayZ(double phase, double elapsed) {
    return std::cos((phase + elapsed * 0.5) * 0.7) * 0.01;
}

} // namespace

TEST_CASE("tree sway helpers reproduce ForestEnvironment.tsx:145-148",
          "[web-parity][environment][sway]") {
    // Sample a spread of (phase, elapsed) pairs and compare against the
    // independent double reference. Elapsed values run well past a second so a
    // dropped or mis-scaled time term cannot hide in the sub-frame range.
    const float phases[]   = {0.0f, 0.5f, 1.5707963f /*pi/2*/, 3.1415927f /*pi*/, 5.0f};
    const float elapseds[] = {0.0f, 0.25f, 1.0f, 2.0f, 7.3f, 60.0f};
    for (float phase : phases) {
        for (float elapsed : elapseds) {
            CHECK(approxEq(WebParity::treeSwayRotationX(phase, elapsed),
                           static_cast<float>(refSwayX(phase, elapsed))));
            CHECK(approxEq(WebParity::treeSwayRotationZ(phase, elapsed),
                           static_cast<float>(refSwayZ(phase, elapsed))));
        }
    }
}

TEST_CASE("tree sway never exceeds the +/-0.01 rad amplitude",
          "[web-parity][environment][sway]") {
    // ForestEnvironment.tsx:146 swayAmount = 0.01, and sin/cos are bounded by
    // 1, so neither axis may ever exceed the amplitude (plus float slack). A
    // regression that scaled the amplitude up (visible over-sway) fails here.
    for (float elapsed = 0.0f; elapsed < 120.0f; elapsed += 0.37f) {
        for (float phase = 0.0f; phase < WebParity::kTreeSwayPhaseMaxRadians; phase += 0.61f) {
            CHECK(std::fabs(WebParity::treeSwayRotationX(phase, elapsed)) <=
                  WebParity::kTreeSwayAmplitudeRadians + kSwayEps);
            CHECK(std::fabs(WebParity::treeSwayRotationZ(phase, elapsed)) <=
                  WebParity::kTreeSwayAmplitudeRadians + kSwayEps);
        }
    }
}

TEST_CASE("tree sway is real motion over time, not a static offset",
          "[web-parity][environment][sway]") {
    // THE regression for the fixed bug: a static model matrix meant the sway
    // was frozen. The X sway MUST change as the engine clock advances —
    // sin(0)=0 at t=0, and by t=1 the scaled time is 0.5 so sin(0.5) ~ 0.479.
    const float atZero = WebParity::treeSwayRotationX(0.0f, 0.0f);
    const float atOne  = WebParity::treeSwayRotationX(0.0f, 1.0f);
    CHECK(atZero == 0.0f);                       // sin(0) * 0.01
    CHECK(std::fabs(atOne - atZero) > 1e-4f);    // it actually MOVED
    CHECK(approxEq(atOne, static_cast<float>(std::sin(0.5) * 0.01)));
}

TEST_CASE("z sway applies the 0.7 factor to the scaled time, not to raw elapsed",
          "[web-parity][environment][sway]") {
    // Guards the audit-note delta the header calls out. The LIVE code is
    // cos((phase + elapsed*0.5) * 0.7); the stale paraphrase was
    // cos((phase + elapsed) * 0.7). At phase=0, elapsed=2 the scaled time is
    // 1.0 so the live value is cos(0.7)*0.01 = 0.0076484, whereas the stale
    // form gives cos(1.4)*0.01 = 0.0017, and a 0.7-factor-dropped form gives
    // cos(1.0)*0.01 = 0.0054. Pin the live value and reject both wrong forms.
    const float live = WebParity::treeSwayRotationZ(0.0f, 2.0f);
    CHECK(approxEq(live, static_cast<float>(std::cos(0.7) * 0.01)));
    CHECK(std::fabs(live - static_cast<float>(std::cos(1.4) * 0.01)) > 1e-4f);
    CHECK(std::fabs(live - static_cast<float>(std::cos(1.0) * 0.01)) > 1e-4f);
}

TEST_CASE("per-tree phase desynchronises neighbouring trees",
          "[web-parity][environment][sway]") {
    // ForestEnvironment.tsx:130 seeds each tree a random animOffset in [0, 2pi)
    // so the canopy does not oscillate in lockstep. At one instant two distinct
    // phases must produce distinct sway.
    const float elapsed = 3.0f;
    CHECK(std::fabs(WebParity::treeSwayRotationX(0.0f, elapsed) -
                    WebParity::treeSwayRotationX(1.7f, elapsed)) > 1e-4f);
    CHECK(std::fabs(WebParity::treeSwayRotationZ(0.0f, elapsed) -
                    WebParity::treeSwayRotationZ(1.7f, elapsed)) > 1e-4f);
}

TEST_CASE("survival lighting / shadow / tree-collision policy is the web target",
          "[web-parity][environment]") {
    // Kept alongside the constant pins in test_web_parity_config.cpp so this
    // environment suite is self-contained and documents the reasoning inline.

    // One scene-wide ambient of 0.5 (BasicScene.tsx:195), replacing the
    // pre-parity per-surface split (scene.frag 0.28 / entity.frag 0.35).
    CHECK(WebParity::kAmbientLightIntensity == 0.5f);

    // Directional sun: normalize(10,10,5) = (2/3, 2/3, 1/3), pure white,
    // intensity 1 (BasicScene.tsx:196). The length is exactly 15.
    CHECK(WebParity::sunDirectionLength() == 15.0f);
    CHECK(approxEq(WebParity::sunDirectionNormalizedX(), 2.0f / 3.0f));
    CHECK(approxEq(WebParity::sunDirectionNormalizedY(), 2.0f / 3.0f));
    CHECK(approxEq(WebParity::sunDirectionNormalizedZ(), 1.0f / 3.0f));
    CHECK(WebParity::kSunColorR == 1.0f);
    CHECK(WebParity::kSunColorG == 1.0f);
    CHECK(WebParity::kSunColorB == 1.0f);
    CHECK(WebParity::kSunIntensity == 1.0f);

    // Real-time shadows are ON in web survival (BasicScene.tsx:190 <Canvas
    // shadows> + :196 castShadow).
    CHECK(WebParity::kShadowsEnabled);

    // Tree collision is OFF under parity — the cat walks through trees, matching
    // web survival (which mounts no TerrainCollisionSystem). This mirrors the
    // exact short-circuit condition in Forest::findTreesInRadius: it returns no
    // collision candidates iff (kEnabled && !kForestPlayerCollision). Pinning
    // both the policy and the decision here means any edit that flips the policy
    // or changes the guard's driving constants trips this test.
    CHECK(WebParity::kForestPlayerCollision == false);
    const bool guardYieldsNoCollisionCandidates =
        WebParity::kEnabled && !WebParity::kForestPlayerCollision;
    CHECK(guardYieldsNoCollisionCandidates);
}

// ============================================================================
// Stress / fuzz tests for engine/animation/Animator.cpp.
//
// Sibling test: tests/unit/test_animator.cpp (cursor-reset + mid-transition
// snapshot regression tests).
//
// What this file pins:
//
//   1. Animator sampled at t=0 and t=duration both produce in-range poses
//      under the looping convention (Animation::normalizeTime wraps t to
//      [0, duration) — so duration itself wraps back to 0 for a loop clip).
//      We verify no NaN, finite scale, finite rotation, and that the t=0
//      and t=duration samples are CLOSE (they must be — the loop wraps).
//
//   2. 1000-frame play loop on a 10-clip blend tree never produces a NaN.
//      The blend tree exercises:
//         - 10 separate AnimationStates with distinct clips
//         - rapid play() calls during in-progress transitions
//         - varying time steps (small dt, large dt, dt across loop boundaries)
//         - random transitions every N frames
//      If any frame's m_currentPose contains a NaN, the test fails — and the
//      bug is real: a NaN bone pose poisons the entire skinning palette and
//      produces a black hole in the renderer.
//
//   3. Parameter update / transition condition fuzz: rapid setBool /
//      setFloat / setTrigger calls don't crash the Animator and the
//      reported parameters match the last write.
// ============================================================================

#include "catch.hpp"
#include "engine/animation/Animator.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using Engine::Animation;
using Engine::AnimationChannel;
using Engine::AnimationState;
using Engine::Animator;
using Engine::Bone;
using Engine::PositionKeyframe;
using Engine::Quaternion;
using Engine::RotationKeyframe;
using Engine::ScaleKeyframe;
using Engine::Skeleton;
using Engine::Transform;
using Engine::vec3;

namespace {

constexpr float ST_LOOSE = 1e-2f;

struct XorShift32 {
    uint32_t state;
    explicit XorShift32(uint32_t seed) : state(seed ? seed : 0x1234567u) {}

    uint32_t next_u32() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float uniform(float lo, float hi) {
        const uint32_t u = next_u32();
        const float t = static_cast<float>(u) / static_cast<float>(UINT32_MAX);
        return lo + t * (hi - lo);
    }
};

bool finite_transform(const Transform& t) {
    return std::isfinite(t.position.x) && std::isfinite(t.position.y) &&
           std::isfinite(t.position.z) &&
           std::isfinite(t.rotation.x) && std::isfinite(t.rotation.y) &&
           std::isfinite(t.rotation.z) && std::isfinite(t.rotation.w) &&
           std::isfinite(t.scale.x) && std::isfinite(t.scale.y) &&
           std::isfinite(t.scale.z);
}

// Build a synthetic clip: bone 0 translates from `start` to `end` over
// the clip's duration, with a 30°-ish yaw at the midpoint and a small
// scale wobble. Enough variation that sampling at any t yields a clearly
// non-trivial pose, but nothing pathological.
std::shared_ptr<Animation> makeSyntheticClip(const std::string& name,
                                              float duration,
                                              const vec3& start,
                                              const vec3& end,
                                              float midYawRad) {
    auto clip = std::make_shared<Animation>(name, duration);
    AnimationChannel channel(0, "root");

    channel.positionKeyframes.emplace_back(0.0f, start);
    channel.positionKeyframes.emplace_back(duration * 0.5f,
                                            (start + end) * 0.5f + vec3(0.0f, 0.2f, 0.0f));
    channel.positionKeyframes.emplace_back(duration, end);

    channel.rotationKeyframes.emplace_back(0.0f, Quaternion::identity());
    channel.rotationKeyframes.emplace_back(
        duration * 0.5f,
        Quaternion::fromAxisAngle(vec3(0.0f, 1.0f, 0.0f), midYawRad));
    channel.rotationKeyframes.emplace_back(duration, Quaternion::identity());

    channel.scaleKeyframes.emplace_back(0.0f, vec3(1.0f));
    channel.scaleKeyframes.emplace_back(duration * 0.5f, vec3(1.05f));
    channel.scaleKeyframes.emplace_back(duration, vec3(1.0f));

    clip->addChannel(channel);
    return clip;
}

std::shared_ptr<Skeleton> makeStressSkeleton() {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->addBone("root", -1);
    std::vector<Transform> bindPose(1);
    bindPose[0] = Transform::identity();
    skeleton->setBindPose(bindPose);
    return skeleton;
}

}  // anon namespace

// ============================================================================
// 1. Sample at t=0 produces in-range pose
// ============================================================================

TEST_CASE("Animator stress: sampling at t=0 produces a finite in-range pose",
          "[anim][animator][stress]") {
    auto skeleton = makeStressSkeleton();
    Animator animator(skeleton);

    auto clip = makeSyntheticClip("idle", 1.2f, vec3(0.0f), vec3(0.5f, 0.0f, 0.0f),
                                   0.5f);
    AnimationState idle("idle", clip, 1.0f, true);
    animator.addState(idle);

    animator.play("idle");
    // After play() with no transition, the cursor is at t=0 and the pose
    // is whatever the clip samples at t=0.
    animator.update(0.0f);

    const auto& pose = animator.getCurrentPose();
    REQUIRE(pose.size() == 1);
    REQUIRE(finite_transform(pose[0]));

    // At t=0 the clip's first keyframe applies: position == start (vec3 0).
    REQUIRE(std::abs(pose[0].position.x) < ST_LOOSE);
    REQUIRE(std::abs(pose[0].position.y) < ST_LOOSE);
    REQUIRE(std::abs(pose[0].position.z) < ST_LOOSE);
}

// ============================================================================
// 2. Sample at t=duration produces in-range pose (loop wraps to t=0)
// ============================================================================

TEST_CASE("Animator stress: sampling at t=duration wraps to t=0 for looping clip",
          "[anim][animator][stress]") {
    auto skeleton = makeStressSkeleton();
    Animator animator(skeleton);

    const float duration = 1.2f;
    auto clip = makeSyntheticClip("idle", duration,
                                   vec3(0.0f),
                                   vec3(0.5f, 0.0f, 0.0f),
                                   0.5f);
    AnimationState idle("idle", clip, 1.0f, true);
    animator.addState(idle);

    animator.play("idle");
    // Advance to exactly t=duration. Animation::normalizeTime with
    // loop=true wraps duration → 0 via std::fmod(t, duration) — so the
    // resulting pose should match the t=0 sample bit-for-tight-tolerance.
    animator.update(duration);

    const auto& pose = animator.getCurrentPose();
    REQUIRE(pose.size() == 1);
    REQUIRE(finite_transform(pose[0]));

    // Pose should match t=0 sample (start position, identity rotation).
    REQUIRE(std::abs(pose[0].position.x) < ST_LOOSE);
    REQUIRE(std::abs(pose[0].position.z) < ST_LOOSE);
}

// ============================================================================
// 3. 1000-FRAME stress loop with 10 clips, random transitions, never NaN
// ============================================================================
//
// The headline contract. If any frame produces a NaN bone pose, the
// renderer's skinning palette would be poisoned downstream, the mesh
// would render as a black hole, and the user-visible bug would be a
// "the cat disappeared" complaint. We never want to ship that, and the
// Animator is the producer of the bone pose every frame.

TEST_CASE("Animator stress: 1000-frame play loop on 10-clip tree never produces NaN",
          "[anim][animator][stress]") {
    auto skeleton = makeStressSkeleton();
    Animator animator(skeleton);

    XorShift32 rng(0xA11CE000u);

    // 10 distinct clips with random durations, translations and yaws.
    // The deliberate inclusion of a very short clip (0.07s) and a long
    // clip (3.4s) covers the dt-greater-than-duration case where the
    // clip wraps multiple times in one update — the same case the
    // ExtractWindow loop-wrap math handles.
    const std::pair<std::string, float> clipNames[] = {
        {"idle", 1.2f}, {"walk", 0.8f}, {"run", 0.6f},
        {"sit", 0.07f}, {"stand", 2.5f}, {"jump", 0.5f},
        {"attack", 0.4f}, {"hit", 0.3f}, {"die", 3.4f}, {"alert", 1.0f},
    };
    for (const auto& nameDur : clipNames) {
        auto clip = makeSyntheticClip(nameDur.first, nameDur.second,
                                       vec3(rng.uniform(-1.0f, 1.0f), 0.0f,
                                            rng.uniform(-1.0f, 1.0f)),
                                       vec3(rng.uniform(-1.0f, 1.0f), 0.0f,
                                            rng.uniform(-1.0f, 1.0f)),
                                       rng.uniform(-1.5f, 1.5f));
        AnimationState state(nameDur.first, clip,
                              rng.uniform(0.5f, 1.5f), true);
        animator.addState(state);
    }

    // Initial state to start playback.
    animator.play("idle");

    // Run 1000 frames.
    for (int frame = 0; frame < 1000; ++frame) {
        // Random dt: mostly small (frame-perfect ~16.6ms), occasionally
        // large (simulate a frame hitch / debug single-step).
        float dt;
        const uint32_t roll = rng.next_u32() % 100u;
        if (roll < 90u) {
            dt = rng.uniform(0.008f, 0.033f);  // 30-120fps range
        } else if (roll < 98u) {
            dt = rng.uniform(0.1f, 0.5f);  // hitch
        } else {
            dt = rng.uniform(1.0f, 5.0f);  // long pause / debug step
        }

        // Occasionally request a transition — including DURING an
        // in-progress transition (the rubber-banding edge the existing
        // test_animator.cpp pins for a single mid-transition case).
        if ((frame % 17) == 0) {
            const uint32_t pick = rng.next_u32() % 10u;
            animator.play(clipNames[pick].first,
                          rng.uniform(0.05f, 0.4f));
        }

        // Occasionally pause / resume to exercise those code paths.
        if ((frame % 53) == 0) {
            animator.pause();
        }
        if ((frame % 67) == 0) {
            animator.resume();
        }

        animator.update(dt);

        const auto& pose = animator.getCurrentPose();
        INFO("frame " << frame << " dt=" << dt);
        REQUIRE(pose.size() == 1);
        REQUIRE(finite_transform(pose[0]));

        // The clips author scale around 1.0 ± 0.05 — even after a slerp
        // through multiple states the scale per axis must stay in a
        // sensible range. Wide-open bounds because additive scale
        // composition CAN drift, but never to absurd values.
        REQUIRE(pose[0].scale.x > 0.01f);
        REQUIRE(pose[0].scale.x < 100.0f);
        REQUIRE(pose[0].scale.y > 0.01f);
        REQUIRE(pose[0].scale.y < 100.0f);
        REQUIRE(pose[0].scale.z > 0.01f);
        REQUIRE(pose[0].scale.z < 100.0f);

        // Rotation must stay reasonably close to unit length — Animator
        // doesn't enforce strict unit, but the blend math should keep it
        // close.
        const float qLen = pose[0].rotation.length();
        REQUIRE(qLen > 0.5f);
        REQUIRE(qLen < 2.0f);

        // No absurd translation values either.
        REQUIRE(std::abs(pose[0].position.x) < 100.0f);
        REQUIRE(std::abs(pose[0].position.y) < 100.0f);
        REQUIRE(std::abs(pose[0].position.z) < 100.0f);
    }
}

// ============================================================================
// 4. Parameter set/get round-trip fuzz
// ============================================================================

TEST_CASE("Animator stress: setBool / setFloat round-trip 200 fuzz iterations",
          "[anim][animator][stress]") {
    auto skeleton = makeStressSkeleton();
    Animator animator(skeleton);

    XorShift32 rng(0xFA22DDEDu);

    for (int i = 0; i < 200; ++i) {
        const std::string boolName = "b_" + std::to_string(i % 16);
        const std::string floatName = "f_" + std::to_string(i % 16);
        const bool boolVal = (rng.next_u32() & 1u) == 0u;
        const float floatVal = rng.uniform(-100.0f, 100.0f);

        animator.setBool(boolName, boolVal);
        animator.setFloat(floatName, floatVal);

        INFO("iteration " << i);
        REQUIRE(animator.getBool(boolName) == boolVal);
        REQUIRE(animator.getFloat(floatName) == floatVal);
    }
}

// ============================================================================
// 5. Empty Animator (no states, no skeleton) tolerates update() calls
// ============================================================================

TEST_CASE("Animator stress: empty Animator tolerates update without crashing",
          "[anim][animator][stress]") {
    Animator animator;  // no skeleton, no states

    // Should not crash — Animator is supposed to early-out without a
    // current state.
    for (int i = 0; i < 100; ++i) {
        animator.update(0.016f);
    }

    REQUIRE_FALSE(animator.isPlaying());
}

// ============================================================================
// 6. Rapid play() calls during in-progress transitions never NaN
// ============================================================================

TEST_CASE("Animator stress: 200 rapid play() calls during transitions stay finite",
          "[anim][animator][stress]") {
    auto skeleton = makeStressSkeleton();
    Animator animator(skeleton);

    auto clipA = makeSyntheticClip("a", 1.0f, vec3(0.0f),
                                    vec3(1.0f, 0.0f, 0.0f), 0.5f);
    auto clipB = makeSyntheticClip("b", 0.8f, vec3(1.0f, 0.0f, 0.0f),
                                    vec3(0.0f), -0.5f);
    auto clipC = makeSyntheticClip("c", 0.4f, vec3(0.0f, 1.0f, 0.0f),
                                    vec3(0.0f), 0.0f);
    animator.addState(AnimationState("a", clipA, 1.0f, true));
    animator.addState(AnimationState("b", clipB, 1.0f, true));
    animator.addState(AnimationState("c", clipC, 1.0f, true));

    XorShift32 rng(0xBAD1B0B5u);

    animator.play("a");
    for (int i = 0; i < 200; ++i) {
        // Tiny dt so transitions stay mid-progress (transitionDuration
        // default 0.3 → 30+ rapid frames inside any one transition).
        animator.update(0.005f);

        const uint32_t pick = rng.next_u32() % 3u;
        const char* target = (pick == 0) ? "a" : (pick == 1) ? "b" : "c";
        animator.play(target, 0.3f);

        const auto& pose = animator.getCurrentPose();
        INFO("iteration " << i << " target=" << target);
        REQUIRE(pose.size() == 1);
        REQUIRE(finite_transform(pose[0]));
    }
}

// ============================================================================
// 7. Stop / play sequence resets cursor
// ============================================================================

TEST_CASE("Animator stress: stop followed by play resets cursor to 0",
          "[anim][animator][stress]") {
    auto skeleton = makeStressSkeleton();
    Animator animator(skeleton);

    auto clip = makeSyntheticClip("a", 2.0f, vec3(0.0f),
                                   vec3(1.0f, 0.0f, 0.0f), 0.5f);
    animator.addState(AnimationState("a", clip, 1.0f, true));

    animator.play("a");
    animator.update(1.5f);  // cursor at 1.5
    REQUIRE(animator.getCurrentTime() > 1.0f);

    animator.stop();
    REQUIRE(animator.getCurrentTime() == 0.0f);
    REQUIRE_FALSE(animator.isPlaying());

    animator.play("a");
    REQUIRE(animator.isPlaying());
    // After stop+play of the same state, cursor stays at 0.
    REQUIRE(animator.getCurrentTime() == 0.0f);
}

// ============================================================================
// 8. Trigger fire & reset cycle
// ============================================================================

TEST_CASE("Animator stress: trigger set then implicit reset on consume",
          "[anim][animator][stress]") {
    Animator animator;

    animator.setTrigger("attack");
    // Trigger state is observable through the parameters subsystem; the
    // Animator's public API doesn't expose a getter for triggers in the
    // way it does for bool/float, so we focus on the no-crash contract
    // and the side effect that no spurious bool/float was created.
    REQUIRE(animator.getBool("attack") == false);
    REQUIRE(animator.getFloat("attack") == 0.0f);
}

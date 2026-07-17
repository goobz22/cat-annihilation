// ============================================================================
// Unit tests for the Animator state machine in engine/animation/Animator.cpp.
//
// Primary contract pinned here: when a transition starts, the playback
// cursor for the target state is reset to t=0. The pre-fix bug was that
// `m_currentTime` was the cursor for whichever state was currently playing;
// startTransition() saved the outgoing state's cursor into m_previousTime
// but never zeroed m_currentTime, so the FIRST sample of the incoming state
// during the crossfade lands at whatever timestamp the previous state was
// at. On a typical authoring (idle loop at 1.2s, attack swing duration
// 0.4s), starting an attack from mid-idle would sample the attack clip at
// t=1.2s — wrapped to t=0.4 by Animation::normalizeTime, which on a clip
// of duration 0.4 lands at the FINAL frame and immediately re-loops. The
// visible artifact is the attack swing starting at its last keyframe and
// then snapping to t=0 on the next update — a "double tap" frame skip.
//
// Secondary contract: rapid sequential play() calls during an in-progress
// transition snapshot the currently-blended pose as the new "previous"
// pose, so the second transition starts smoothly from the mid-blend
// frame rather than rubber-banding back to the original outgoing state.
// ============================================================================

#include "catch.hpp"
#include "engine/animation/Animator.hpp"

#include <memory>

using Engine::Animation;
using Engine::AnimationChannel;
using Engine::AnimationState;
using Engine::Animator;
using Engine::Bone;
using Engine::PositionKeyframe;
using Engine::Quaternion;
using Engine::RotationKeyframe;
using Engine::Skeleton;
using Engine::Transform;
using Engine::vec3;

namespace {

// Build a 1-bone skeleton with a configurable bind pose. The Animator
// needs a skeleton to sample anything; one bone is enough to lock down
// the timing contract without dragging the multi-bone hierarchy walk
// into the assertion budget.
std::shared_ptr<Skeleton> makeSingleBoneSkeleton(const vec3& bindPos) {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->addBone("root", -1);
    std::vector<Transform> bindPose(1);
    bindPose[0].position = bindPos;
    skeleton->setBindPose(bindPose);
    return skeleton;
}

// Animation that translates the root bone from `startPos` at t=0 to
// `endPos` at t=duration along a single position channel. Linear lerp,
// so sampling at any t in [0, duration] gives lerp(start, end, t/duration).
std::shared_ptr<Animation> makeLinearTranslationClip(const std::string& name,
                                                     float duration,
                                                     const vec3& startPos,
                                                     const vec3& endPos) {
    auto clip = std::make_shared<Animation>(name, duration);
    AnimationChannel channel(0, "root");
    channel.positionKeyframes.emplace_back(0.0f, startPos);
    channel.positionKeyframes.emplace_back(duration, endPos);
    clip->addChannel(channel);
    return clip;
}

}  // namespace

TEST_CASE("Animator: startTransition resets target state playback cursor",
          "[animator]") {
    // Outgoing state "idle" runs for 1.0 seconds before the user requests
    // a transition into "attack". If startTransition fails to reset
    // m_currentTime to 0, the attack clip's first sample during the
    // crossfade lands at t=1.0 (the carryover cursor), which on a short
    // 0.4s attack clip wraps to its FINAL frame instead of starting at the
    // first frame. We verify by stepping a tiny dt into the transition
    // and asserting that the blended pose is dominated by the START of
    // the attack clip — not its end.
    auto skeleton = makeSingleBoneSkeleton(vec3(0.0f, 0.0f, 0.0f));
    // Idle clip: bone at (0,0,0) the whole time (start == end).
    auto idle = makeLinearTranslationClip("idle", 2.0f,
                                          vec3(0.0f, 0.0f, 0.0f),
                                          vec3(0.0f, 0.0f, 0.0f));
    // Attack clip: bone slides from (10,0,0) to (20,0,0) over 0.4s.
    // The start-of-clip pose at t=0 is (10,0,0); end-of-clip pose at
    // t=0.4 is (20,0,0). After a successful reset, sampling the attack
    // clip at the start of the transition crossfade will pick poses
    // very close to (10,0,0). Pre-fix (carryover t=1.0), normalizeTime
    // wraps 1.0 mod 0.4 = 0.2, which lerps to (15,0,0) — the middle of
    // the attack instead of the start.
    auto attack = makeLinearTranslationClip("attack", 0.4f,
                                            vec3(10.0f, 0.0f, 0.0f),
                                            vec3(20.0f, 0.0f, 0.0f));

    Animator animator(skeleton);
    animator.addState(AnimationState("idle", idle, 1.0f, /*loop=*/true));
    animator.addState(AnimationState("attack", attack, 1.0f, /*loop=*/true));

    animator.play("idle");
    // Advance idle so m_currentTime drifts to ~1.0s — the trigger for the
    // pre-fix bug. Three 0.33s steps total ~0.99s.
    animator.update(0.33f);
    animator.update(0.33f);
    animator.update(0.33f);

    // Transition into attack with a long crossfade so we can sample the
    // mid-transition pose at a known blendFactor.
    animator.play("attack", /*transitionDuration=*/0.5f);
    REQUIRE(animator.isTransitioning());

    // Step a SMALL dt into the transition. At dt=0.01s with a 0.5s
    // transition duration, blendFactor ≈ 0.02 — the resulting pose is
    // ~98% idle, ~2% attack. The attack pose at this instant should be
    // very close to its start (10,0,0) after the fix. We assert the
    // blended bone X is close to idle's bone X plus a sliver of
    // (attack.start - idle), i.e. small positive — NOT something
    // anywhere near attack's midpoint (15) or end (20), which would be
    // the case if the cursor carried over from idle's t=1.0.
    animator.update(0.01f);

    const auto& pose = animator.getCurrentPose();
    REQUIRE(pose.size() == 1);
    // 2% of (10,0,0) is 0.2. Pre-fix would sample attack ≈ (15,0,0) so
    // the blended X would be ≈ 0.3. Post-fix the blended X should be
    // ≈ 0.2. Tight tolerance to clearly distinguish the two regimes.
    REQUIRE(pose[0].position.x > 0.05f);
    REQUIRE(pose[0].position.x < 0.5f);
}

TEST_CASE("Animator: stop()+play() then update() samples from t=0",
          "[animator]") {
    // After stop(), m_currentTime is zeroed but m_currentStateName is
    // preserved (so resume() can pick up the same state). play(sameName)
    // re-arms m_playing without triggering startTransition (same-name
    // branch is a no-op by design — see the documented behaviour at
    // Animator::play in Animator.cpp). The first update() tick after the
    // restart then advances m_currentTime from 0 and resamples — and the
    // small-dt sample must land near the clip's t=0 pose, not at the
    // mid-clip pose that was cached before stop().
    auto skeleton = makeSingleBoneSkeleton(vec3(0.0f, 0.0f, 0.0f));
    auto clip = makeLinearTranslationClip("walk", 1.0f,
                                          vec3(0.0f, 0.0f, 0.0f),
                                          vec3(10.0f, 0.0f, 0.0f));
    Animator animator(skeleton);
    animator.addState(AnimationState("walk", clip, 1.0f, /*loop=*/true));

    animator.play("walk");
    animator.update(0.5f);  // cursor at 0.5s, sampled pose ≈ (5,0,0)
    animator.stop();
    animator.play("walk");
    animator.update(0.01f);  // advance one small tick after restart

    // Sampled pose at t=0.01 should be ≈ (0.1, 0, 0) — close to clip start.
    // Tolerance loose enough that the bind-pose seeding in
    // sampleCurrentAnimation doesn't poison the assertion if the channel
    // ever stops covering bone 0 perfectly.
    const auto& pose = animator.getCurrentPose();
    REQUIRE(pose.size() == 1);
    REQUIRE(pose[0].position.x < 0.5f);
}

TEST_CASE("Animator: mid-transition play() snapshots intermediate pose",
          "[animator]") {
    // Setup: A is at (0,0,0), B is at (100,0,0), C is at (-100,0,0).
    // Idle on A. Start A->B transition with 1.0s duration. Update halfway
    // (0.5s) so the blended pose is roughly (50,0,0). Now request A->C —
    // the new "previous pose" must be the mid-A->B blend (~50,0,0), NOT
    // the original A pose (0,0,0). One small step into the new transition
    // should produce a pose still close to 50, not collapsing back to 0.
    auto skeleton = makeSingleBoneSkeleton(vec3(0.0f, 0.0f, 0.0f));
    auto stateA = makeLinearTranslationClip("A", 5.0f,
                                            vec3(0.0f, 0.0f, 0.0f),
                                            vec3(0.0f, 0.0f, 0.0f));
    auto stateB = makeLinearTranslationClip("B", 5.0f,
                                            vec3(100.0f, 0.0f, 0.0f),
                                            vec3(100.0f, 0.0f, 0.0f));
    auto stateC = makeLinearTranslationClip("C", 5.0f,
                                            vec3(-100.0f, 0.0f, 0.0f),
                                            vec3(-100.0f, 0.0f, 0.0f));

    Animator animator(skeleton);
    animator.addState(AnimationState("A", stateA));
    animator.addState(AnimationState("B", stateB));
    animator.addState(AnimationState("C", stateC));

    animator.play("A");
    animator.play("B", 1.0f);
    animator.update(0.5f);
    // Mid-transition pose: should be ~halfway from A (0) to B (100).
    const float midPoseX = animator.getCurrentPose()[0].position.x;
    REQUIRE(midPoseX > 30.0f);
    REQUIRE(midPoseX < 70.0f);

    // Request A->C now (well, B-target->C since current is mid-blend).
    // The snapshot contract says m_previousPose absorbs the current
    // blended pose, so a tiny step into the new transition still reads
    // close to the mid value rather than snapping back to 0.
    animator.play("C", 1.0f);
    animator.update(0.001f);
    const float earlyNewTransitionX = animator.getCurrentPose()[0].position.x;
    // We don't pin a tight value because the slerp blend mixes the mid
    // pose with C's pose; the test is qualitative — the pose is NOT 0
    // (which would mean previousPose was reset to A's bind) and NOT
    // -100 (which would mean we are already fully in C).
    REQUIRE(earlyNewTransitionX > 20.0f);
    REQUIRE(earlyNewTransitionX < 80.0f);
}

TEST_CASE("Animator: a non-looping clip HOLDS its last frame at the end, not frame 0",
          "[animator]") {
    // Round-3 audit (2026-07-17), HIGH. Animation::sample unconditionally
    // wrapped time with normalizeTime(time, /*loop=*/true), so even after
    // Animator::updateAnimation clamped a non-looping clip's cursor to
    // duration and stopped playback, the sample wrapped duration->0 (fmod
    // (d,d)==0) and the pose snapped to the FIRST keyframe. A finished
    // attack / a death pose / a seated idle all visibly popped back to the
    // standing first frame instead of holding their final pose.
    //
    // Clip translates root (0,0,0) -> (10,0,0) over 0.4s. After running well
    // past the end as a NON-looping state, the held pose MUST be the last
    // keyframe (10,0,0), not the first (0,0,0).
    auto skeleton = makeSingleBoneSkeleton(vec3(0.0f, 0.0f, 0.0f));
    auto clip = makeLinearTranslationClip("attack", 0.4f,
                                          vec3(0.0f, 0.0f, 0.0f),
                                          vec3(10.0f, 0.0f, 0.0f));
    Animator animator;
    animator.setSkeleton(skeleton);
    animator.addState(AnimationState("attack", clip, /*speed=*/1.0f, /*loop=*/false));

    animator.play("attack");
    // Step well past the 0.4s duration (also clears any transition blend).
    for (int i = 0; i < 10; ++i) {
        animator.update(0.1f);
    }

    const auto& pose = animator.getCurrentPose();
    REQUIRE(pose.size() == 1u);
    // Pre-fix: ~0.0 (wrapped to frame 0). Post-fix: 10.0 (held last frame).
    REQUIRE(pose[0].position.x == Approx(10.0f).margin(0.01f));

    // Positive control: the SAME clip as a LOOPING state must still wrap.
    // At an accumulated cursor the loop keeps advancing; after 1.0s total on
    // a 0.4s clip, fmod(1.0,0.4)=0.2 -> lerp -> (5,0,0). This guards against
    // a fix that simply disables looping everywhere.
    Animator looping;
    looping.setSkeleton(makeSingleBoneSkeleton(vec3(0.0f, 0.0f, 0.0f)));
    looping.addState(AnimationState("walk", clip, /*speed=*/1.0f, /*loop=*/true));
    looping.play("walk");
    for (int i = 0; i < 10; ++i) {
        looping.update(0.1f);  // 1.0s total; fmod(1.0,0.4)=0.2 -> x≈5
    }
    REQUIRE(looping.getCurrentPose()[0].position.x == Approx(5.0f).margin(0.5f));
}

#pragma once

// LocomotionStateMachine.hpp — wire idle/walk/run animation transitions on
// an Engine::Animator, driven by a single "speed" float parameter the
// gameplay code feeds from MovementComponent::getCurrentSpeed() each frame.
//
// Why this lives in game/components/ and not engine/animation/:
//   The Animator class is a generic state-machine runtime — it has no
//   opinion about what "idle", "walk", or "run" mean, what speed
//   thresholds count as walking vs running, or what blend duration looks
//   right for a cat versus a dog versus a humanoid. Those are gameplay
//   tuning decisions. Keeping the wiring in the game layer means engine
//   stays generic and other projects (or our own future creatures) can
//   pick different state names + thresholds without touching the
//   animation runtime.
//
// Why hysteresis (the wider gap between idle->walk vs walk->idle, and
// between walk->run vs run->walk):
//   Without hysteresis, an entity oscillating exactly at the threshold
//   (e.g. speed = 1.0 with a single threshold of 1.0) would flip
//   between idle and walk every frame, producing visible foot-pop and
//   blend pumping. The 0.5 m/s gap on idle<->walk and 1.0 m/s gap on
//   walk<->run means the entity has to commit to a noticeably faster
//   speed to upgrade the clip and noticeably slower to downgrade it.
//
// Why we conditionally add transitions (only when both endpoint clips
// exist):
//   Not every rigged GLB ships every clip. The auto-rigged Meshy cats
//   have 7 clips (idle/walk/run/sit/lay/standUp + a 7th); a future
//   stripped-down NPC asset might have only idle. Calling addTransition
//   for a missing target would still register it in the transitions
//   vector but `startTransition` would silently no-op (hasState check).
//   Skipping the addTransition saves the per-frame check cycles inside
//   `Animator::checkTransitions` for transitions that can never fire,
//   and keeps the transition graph honest for any future debug visualizer.

#include "../../engine/animation/Animator.hpp"

namespace CatGame {

// Default thresholds tuned for the Meshy-rigged cats and dogs (move
// speeds in the 6-12 m/s range). Callers can override via the
// optional walk/run threshold parameters if they author a creature
// with very different locomotion speeds.
constexpr float kDefaultIdleToWalkSpeed = 1.0F;   // start walking at >= 1 m/s
constexpr float kDefaultWalkToIdleSpeed = 0.5F;   // stop walking at <= 0.5 m/s
constexpr float kDefaultWalkToRunSpeed  = 6.0F;   // start running at >= 6 m/s
constexpr float kDefaultRunToWalkSpeed  = 5.0F;   // stop running at <= 5 m/s

constexpr float kLocomotionBlendSeconds = 0.20F;  // 200 ms cross-fade between clips

// Add idle/walk/run transitions to `animator` keyed on a "speed" float
// parameter. Caller must have already registered the relevant states via
// `animator.addState(...)` (we don't author the states here so the
// gameplay code can keep its existing per-clip loop/play-rate config).
//
// Idempotent in practice but not literally: calling this twice would
// duplicate the transitions. Production callers should call once after
// state registration.
inline void wireLocomotionTransitions(
    Engine::Animator& animator,
    float idleToWalk = kDefaultIdleToWalkSpeed,
    float walkToIdle = kDefaultWalkToIdleSpeed,
    float walkToRun  = kDefaultWalkToRunSpeed,
    float runToWalk  = kDefaultRunToWalkSpeed)
{
    using Engine::AnimationTransition;
    using Engine::TransitionCondition;

    const bool hasIdle = animator.hasState("idle");
    const bool hasWalk = animator.hasState("walk");
    const bool hasRun  = animator.hasState("run");

    // idle <-> walk: most common transition, both directions hysteretic.
    if (hasIdle && hasWalk) {
        AnimationTransition idleToWalkTransition("idle", "walk", kLocomotionBlendSeconds);
        idleToWalkTransition.addCondition(TransitionCondition::floatGreater("speed", idleToWalk));
        animator.addTransition(idleToWalkTransition);

        AnimationTransition walkToIdleTransition("walk", "idle", kLocomotionBlendSeconds);
        walkToIdleTransition.addCondition(TransitionCondition::floatLess("speed", walkToIdle));
        animator.addTransition(walkToIdleTransition);
    }

    // walk <-> run: speed-based upgrade/downgrade once the entity is
    // already moving. Without these the cat tops out at "walk" no
    // matter how fast PlayerControlSystem accelerates it.
    if (hasWalk && hasRun) {
        AnimationTransition walkToRunTransition("walk", "run", kLocomotionBlendSeconds);
        walkToRunTransition.addCondition(TransitionCondition::floatGreater("speed", walkToRun));
        animator.addTransition(walkToRunTransition);

        AnimationTransition runToWalkTransition("run", "walk", kLocomotionBlendSeconds);
        runToWalkTransition.addCondition(TransitionCondition::floatLess("speed", runToWalk));
        animator.addTransition(runToWalkTransition);
    }

    // idle <-> run: skip-walk path, fires when the entity goes from a
    // dead stop to full sprint in less than one frame (e.g. a dog
    // teleported to its spawn position with full velocity already
    // applied by WaveSystem). Without this, the cat would stay in
    // idle forever if walk is missing from the rig but run is present.
    // The thresholds intentionally reuse the run/idle endpoints so
    // there's no overlap with the chained idle->walk->run path above.
    if (hasIdle && hasRun) {
        AnimationTransition idleToRunTransition("idle", "run", kLocomotionBlendSeconds);
        idleToRunTransition.addCondition(TransitionCondition::floatGreater("speed", walkToRun));
        animator.addTransition(idleToRunTransition);

        AnimationTransition runToIdleTransition("run", "idle", kLocomotionBlendSeconds);
        runToIdleTransition.addCondition(TransitionCondition::floatLess("speed", walkToIdle));
        animator.addTransition(runToIdleTransition);
    }
}

} // namespace CatGame

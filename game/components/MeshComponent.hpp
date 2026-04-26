#pragma once

#include "../../engine/assets/ModelLoader.hpp"
#include "../../engine/animation/Animator.hpp"
#include "../../engine/animation/Skeleton.hpp"
#include "../../engine/math/Vector.hpp"
#include <memory>
#include <string>

namespace CatGame {

/**
 * Renderable mesh + skeletal-animation state attached to an ECS entity.
 *
 * Holds strong references to the AssetManager-cached Model so the renderer
 * and animation subsystems can safely consume the same data. The Animator
 * is owned per-entity because playback state (current clip, blend timer,
 * parameter bag) is entity-local, while the Skeleton is shared from the
 * source model.
 */
struct MeshComponent {
    // Source asset path (kept so the renderer/hot-reloader can resolve the
    // asset without round-tripping through the model).
    std::string sourcePath;

    // Cached model data (vertices, materials, nodes, animation clips).
    // Shared via AssetManager so identical paths reuse GPU/CPU buffers.
    std::shared_ptr<CatEngine::Model> model;

    // Per-entity skeleton and animator. The skeleton's bone hierarchy is
    // derived from the model's node graph; the animator drives bone poses.
    std::shared_ptr<Engine::Skeleton> skeleton;
    std::shared_ptr<Engine::Animator> animator;

    // Visibility flag consumed by the renderer. Culled entities stay alive
    // in the ECS but skip vertex submission.
    bool visible = true;

    // ---- Per-entity tint override (gameplay-driven flat-shaded color) -----
    //
    // When `hasTintOverride` is true, MeshSubmissionSystem feeds `tintOverride`
    // straight into the EntityDraw's per-draw color push constant instead of
    // reading the GLB material's `baseColorFactor`. Two reasons this exists
    // before the textured-PBR pass lands:
    //
    //   1. The Meshy-authored cat / dog GLBs ship with a baseColorTEXTURE
    //      (an embedded JPEG in a bufferView) but NO baseColorFACTOR — every
    //      material defaults to glm::vec4(1.0F), so until the entity pipeline
    //      grows a sampler binding every cat renders as flat white. That
    //      collapses 16 NPC cats from 4 distinct clans into a single visually-
    //      identical white herd, which directly contradicts the user-directive
    //      goal "different dog variants render different GLBs in the same
    //      wave so they're visually distinguishable" (and the same goal
    //      transitively for clan-tagged cats).
    //
    //   2. Per-clan / per-variant tints are a *gameplay* signal, not a
    //      rendering one — the EmberClan flame-orange or BossDog blood-red
    //      reads aren't a property of the asset, they're a property of the
    //      entity's role in the game. Threading them through MeshComponent
    //      keeps the asset honest (the GLB still owns its authored color)
    //      while letting NPCSystem/DogEntity/CatEntity stamp identity onto
    //      whatever mesh they happen to load. Once the textured PBR pass
    //      lands, this override becomes the *modulator* (multiplied with the
    //      sampled baseColor texel) instead of the only color source — same
    //      field, same callers, just a fragment-shader tweak.
    //
    // Default-off: a MeshComponent built without setting these falls through
    // to MeshSubmissionSystem's pre-existing baseColorFactor → fallback-grey
    // path, so existing code paths are unaffected.
    bool hasTintOverride = false;
    Engine::vec3 tintOverride = Engine::vec3(1.0F, 1.0F, 1.0F);

    // ---- Idle-variant cycling (sitDown / layDown / standUp clips) -------
    //
    // The Meshy auto-rigger ships 7 animation clips per cat: idle, walk,
    // run, sitDown, layDown, standUpFromSit, standUpFromLay. The
    // locomotion state machine (LocomotionStateMachine.hpp) drives idle
    // <-> walk <-> run from MovementComponent::getCurrentSpeed() — but
    // the four sit/lay clips are loaded into the animator
    // (CatEntity::configureAnimations registers every clip in
    // mesh->model->animations) and never selected, so 16 stationary NPC
    // cats render as a herd of frozen idles for the entire session.
    // Cycling them through sitDown/Resting/standUp on a staggered timer
    // is the single biggest "world feels alive" delta available without
    // touching the renderer or asset pipeline.
    //
    // Why a 4-phase machine instead of just play("sitDown") on a timer:
    //   The down-clip animates the act of sitting (~1-2 s); after it
    //   finishes the cat is in the seated pose at the clip's final frame,
    //   frozen there because the clip is non-looping. We want to *hold*
    //   that pose for several seconds (Resting) before triggering the
    //   standUp clip. Without an explicit Resting phase the cat would
    //   bob from sitting straight back to standing as fast as the clip
    //   plays, producing a sit-stand-sit-stand pump rather than the
    //   intended "the cat sat down for a while" read.
    //
    // Why fields live on MeshComponent rather than a dedicated component:
    //   Pure-animation state, lifetime exactly matches `animator`, only
    //   meaningful when an Animator exists. Adding a second component
    //   pool + ECS lookup per entity is strict overhead with no caller
    //   diversity benefit — the ONLY code that reads these is the
    //   per-frame cycling tick in CatAnnihilation::update.
    //
    // Why the per-entity seed (idleVariantSeed):
    //   16 NPCs spawned in the same frame would otherwise inherit
    //   identical default `idleVariantNextDelay = -1.0F` and lazy-init
    //   to identical first cooldowns, sitting in lock-step. The seed is
    //   computed once from the Knuth-hashed entity id at first tick so
    //   each entity gets a distinct first-cooldown jitter and a distinct
    //   sit-vs-lay rotation across cycles.
    enum class IdleVariantPhase : uint8_t {
        Idle,        // looping idle clip; idleVariantNextDelay ticks down
        GoingDown,   // sitDown or layDown is playing (non-looping)
        Resting,     // hold the seated/laying final-frame pose
        ComingUp     // standUpFromSit or standUpFromLay is playing
    };
    IdleVariantPhase idleVariantPhase = IdleVariantPhase::Idle;
    float idleVariantPhaseTimer = 0.0F; // counts up within the current phase
    float idleVariantNextDelay = -1.0F; // < 0 means "lazy-seed on first observe"
    uint8_t idleVariantSeed = 0;        // per-entity jitter, set once on first tick
    bool idleVariantUsedLay = false;    // true when current cycle is lay (vs sit)

    // ---- Per-entity attack-lunge visual pulse ---------------------------
    //
    // A normalised 1.0 -> 0.0 progress value driven by CombatSystem each
    // time an attacker successfully starts a swing or projectile spell. The
    // renderer reads this in MeshSubmissionSystem and pre-multiplies the
    // entity's modelMatrix by a small forward-pitch rotation around the
    // entity's local +X axis whose magnitude is `sin(pi * (1 - pulse)) *
    // kAttackLungeMaxAngleRadians`. That curve starts at zero, peaks at
    // mid-pulse (~0.175 s into the 0.35 s window when the swing actually
    // commits to damage), and returns to zero at the end — a clean
    // cosine-bell lunge that doesn't snap on entry or exit.
    //
    // Why this lives on MeshComponent rather than CombatComponent:
    //   The pulse is consumed by the renderer (MeshSubmissionSystem already
    //   reads MeshComponent), so adding the field here avoids forcing the
    //   engine-side renderer to take a dependency on a game-side combat
    //   header. The write happens from CombatSystem (game) which can freely
    //   include MeshComponent.hpp; the read happens from
    //   MeshSubmissionSystem (engine) which already includes
    //   MeshComponent.hpp. Keeping the layering one-way preserves the
    //   engine's "no game-system includes" rule.
    //
    // Why a visual-only field instead of an animation-state trigger:
    //   The Meshy auto-rigger ships seven clips per cat (idle, walk, run,
    //   sitDown, layDown, standUpFromSit, standUpFromLay) — there is NO
    //   attack clip authored on the GLBs we ship. Wiring a clip-trigger
    //   would do nothing because the trigger has no destination state. A
    //   pure modelMatrix lean is a one-iteration delivery that makes every
    //   melee swing and every spellcast visibly punch the cat forward —
    //   the closest we can get to "the cat is attacking" with the assets
    //   on disk today. When we eventually author or import an attack clip,
    //   this field becomes obsolete (or it stays as a subtle additive on
    //   top of the clip — a chef's-kiss bit of secondary motion).
    //
    // Default-zero, decayed every frame in CombatSystem::update by a
    // forEach<MeshComponent> pass that runs at the same low cost as the
    // existing combo/block/dodge per-frame ticks. Entities without a
    // CombatComponent never get their pulse bumped above zero so the
    // decay path is a single comparison + early-out per non-combatant
    // (NPCs, props, projectiles).
    float attackVisualPulse = 0.0F;

    // ---- Per-entity hit-flinch visual pulse -----------------------------
    //
    // Mirror of attackVisualPulse on the receive side: a normalised
    // 1.0 -> 0.0 progress value driven by CombatSystem::applyDamage and
    // CombatSystem::applyDamageWithType every time the target actually
    // takes non-zero damage (after block / dodge / invincibility have
    // already eaten any negation). The renderer reads this in
    // MeshSubmissionSystem and pre-multiplies the entity's modelMatrix
    // by a small *backward* pitch rotation around the entity's local +X
    // axis whose magnitude is `sin(pi * (1 - pulse)) *
    // kHitFlinchMaxAngleRadians` (kHitFlinchMaxAngleRadians is NEGATIVE,
    // producing a flinch-back lean rather than the attack-lunge lean
    // forward). The same cosine-bell envelope as the attack pulse keeps
    // the entry / exit smooth.
    //
    // Why a separate field instead of a signed `combatPulse` that the
    // attacker bumps positive and the target bumps negative:
    //   They overlap. An entity hit mid-swing genuinely *is* both
    //   lunging forward and flinching back — the net effect is a
    //   visible tussle that reads as combat-impact-recoil. With a
    //   single signed field the bumps would race each other (whichever
    //   write happens last in the same frame wins), masking the dual
    //   state. Two independent fields let the renderer add the two
    //   contributions and produce the realistic combined motion.
    //
    // Why 0.30 s pulse and ~9 deg peak (set in CombatSystem.cpp /
    // MeshSubmissionSystem.cpp respectively, not here, so the constants
    // sit next to their callsites):
    //   The flinch is a *secondary* motion — it should read as "the cat
    //   recoiled" without competing with the swing as the primary
    //   action. A shorter pulse (0.30 vs 0.35) and a smaller peak (~9
    //   vs 14 deg) keep the flinch subordinate. Shorter also matters
    //   for chain-attack scenarios: a flinch decaying within 0.30 s is
    //   safely cleared before the next 0.5-1.0 s attack cooldown can
    //   fire another bump, so flinches never visibly stack on a
    //   single-target chain.
    //
    // Default-zero, decayed every frame in the same
    // forEach<MeshComponent> pass that decays attackVisualPulse — a
    // single combined sweep over the component pool keeps cache
    // behaviour identical to the single-pulse era. Entities that never
    // take damage skip the work after a single comparison.
    float hitVisualPulse = 0.0F;

    // ---- Death-pose latch ------------------------------------------------
    //
    // Set true exactly once when HealthSystem::handleDeath fires for this
    // entity, after the system has played the rig's `layDown` non-looping
    // clip on the animator (transition blend ~0.15 s for a punchy "the dog
    // dropped" read). Stays true for the remainder of the entity's
    // lifetime — HealthSystem's own deathTimer drives the eventual
    // destroyEntity 3 s later (deathAnimationDuration is bumped at the
    // same time so the corpse is a visible beat instead of a one-frame
    // pop).
    //
    // Why this latch exists at all (the per-frame logic that has to skip
    // dying entities):
    //
    //   1. Locomotion-speed feed. CatAnnihilation::update's animator tick
    //      writes `setFloat("speed", MovementComponent::getCurrentSpeed())`
    //      on every entity that has both an animator and a movement
    //      component. The locomotion-state-machine transitions wired by
    //      `wireLocomotionTransitions` only have edges between
    //      idle/walk/run, so they cannot pull a layDown back to walk —
    //      BUT the speed parameter is still observed. Skipping the write
    //      is a defensive "don't even leave a stale speed in the
    //      parameter bag for a dead entity".
    //
    //   2. Idle-variant cycler. The 4-phase machine (Idle / GoingDown /
    //      Resting / ComingUp) lives in the same per-frame lambda and
    //      will, on a stationary dying entity, eventually fire the
    //      standUpFromLay clip when its Resting phase elapses — undoing
    //      the death pose visually right as HealthSystem is about to
    //      destroyEntity. Gating the cycler on !deathPosed kills that
    //      class of regression at the source.
    //
    //   3. Pulse decay (attack/hit). CombatSystem::update's
    //      forEach<MeshComponent> sweep keeps decaying the pulses on a
    //      dead entity, which is harmless (the renderer reshape is
    //      gated on pulse > 0 and the values are already at zero by
    //      the time death triggers in nearly every case), but feels
    //      tidier to leave the sweep ungated and rely on the natural
    //      decay rather than special-case dead entities here.
    //
    // Why a flag on MeshComponent vs a dedicated DeathPoseComponent or
    // a flag on HealthComponent:
    //   The flag is read by the animator-tick lambda which already
    //   has a hot MeshComponent pointer in hand — no extra lookup. A
    //   dedicated component pool would cost an extra ECS lookup per
    //   entity per frame for a one-bit signal that's strictly tied
    //   to the existence of an animator. HealthComponent doesn't make
    //   sense as the home: the field is semantically "the renderer/
    //   animator is in death-pose mode", not "the health system thinks
    //   you're dead" (those overlap today but only because there's
    //   one death path; a future stagger / dodge / temporary down
    //   would want death-pose without isDead).
    bool deathPosed = false;
};

} // namespace CatGame

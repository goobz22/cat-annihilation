#include "HealthSystem.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/EnemyComponent.hpp"
#include "../components/MeshComponent.hpp"
#include "../components/MovementComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/core/Logger.hpp"
#include <vector>

namespace CatGame {

namespace {

// Window the corpse stays visible AFTER the kill is registered before
// HealthSystem::update runs destroyEntity. The dying entity transitions
// from `idle/walk/run` into `layDown` over kDeathPoseTransitionSeconds and
// then holds at the clip's final frame for the remainder. Three seconds is
// the sweet spot for a portfolio playtest:
//   - <1.5 s reads as a flicker — viewer hasn't even processed the kill
//     before the entity vanishes, so the death pose contributes nothing.
//   - >5 s pollutes the field with corpses during boss waves with 8+
//     simultaneous spawns, and a stationary corpse pool starts looking
//     like a debug-state failure rather than intentional death feedback.
// Default HealthComponent::deathAnimationDuration ships at 1.0 s (a
// pre-pose-feature value used when the entity was destroyed instantly
// after the death-effect particle burst). Bumping per-entity at the
// death moment (vs editing the default) keeps the field's old
// semantic intact for any non-Mesh entity that might rely on a
// shorter despawn window.
constexpr float kDeathPoseHoldSeconds = 3.0F;

// Blend duration when transitioning from whatever clip was playing
// (typically `walk` or `run` for a dog mid-charge) into the `layDown`
// non-looping clip. 0.15 s is the *impact* sweet spot:
//   - 0.0 s pops the rig in a single frame (looks like a teleport-to-
//     layDown rather than a fall).
//   - >0.30 s lets the rig spend ~10 frames mid-blend half-walking,
//     half-laying — visually muddy because the swing/run pose still
//     reads as "moving" while we're trying to convey "dropped".
// Sub-locomotion (0.20 s) for that punchy "the dog dropped" beat.
constexpr float kDeathPoseTransitionSeconds = 0.15F;

// Name of the non-looping clip every Meshy auto-rigged cat / dog ships
// (the rig_quadruped pipeline authors all 7 clips per character — see
// MeshComponent.hpp's IdleVariantPhase docblock for the full list).
// Documented as a constant rather than a literal so a future asset
// pipeline rename — e.g. `down` instead of `layDown` — touches one
// place. The clip is registered as non-looping in
// CatEntity::configureAnimations (only idle/walk/run are flagged
// looping), so Animator::updateAnimation clamps to the duration and
// flips m_playing=false on completion. That's exactly what we want
// for a corpse: hold the final frame until destroyEntity fires.
constexpr const char* kDeathPoseClipName = "layDown";

} // namespace

HealthSystem::HealthSystem(int priority)
    : System(priority)
{}

void HealthSystem::update(float dt) {
    if (!ecs_) return;

    // Query all entities with health
    auto query = ecs_->query<HealthComponent>();

    // Track entities to destroy
    std::vector<CatEngine::Entity> toDestroy;

    for (auto [entity, health] : query.view()) {
        updateHealth(entity, dt);

        // Remove dead entities after death animation
        if (health->isDead && health->deathTimer >= health->deathAnimationDuration) {
            toDestroy.push_back(entity);
        }
    }

    // Destroy dead entities
    for (auto entity : toDestroy) {
        ecs_->destroyEntity(entity);
    }
}

void HealthSystem::updateHealth(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    // Update invincibility
    updateInvincibility(entity, dt);

    // Check for death
    if (health->currentHealth <= 0.0f && !health->isDead) {
        health->isDead = true;
        health->currentHealth = 0.0f;
        handleDeath(entity);
    }

    // Update death animation
    if (health->isDead) {
        updateDeathAnimation(entity, dt);
        return; // Don't process regeneration if dead
    }

    // Update regeneration
    updateRegeneration(entity, dt);
}

void HealthSystem::updateInvincibility(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    if (health->invincibilityTimer > 0.0f) {
        health->invincibilityTimer -= dt;
        if (health->invincibilityTimer < 0.0f) {
            health->invincibilityTimer = 0.0f;
        }
    }
}

void HealthSystem::updateRegeneration(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    if (!health->canRegenerate || health->regenerationRate <= 0.0f) {
        return;
    }

    // Update time since last damage
    health->timeSinceLastDamage += dt;

    // Only regenerate after delay
    if (health->timeSinceLastDamage >= health->regenerationDelay) {
        float regenAmount = health->regenerationRate * dt;
        heal(entity, regenAmount);
    }
}

void HealthSystem::updateDeathAnimation(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    health->deathTimer += dt;
}

void HealthSystem::handleDeath(CatEngine::Entity entity) {
    // Check if entity is an enemy
    bool isEnemy = ecs_->hasComponent<EnemyComponent>(entity);

    if (isEnemy) {
        handleEnemyDeath(entity);
    } else {
        handlePlayerDeath(entity);
    }

    // Trigger callback
    if (onEntityDeath_) {
        onEntityDeath_(entity, isEnemy);
    }

    // ===== Death-pose freeze ============================================
    //
    // Switch the dying entity's animator into the rig's `layDown` non-
    // looping clip and extend its despawn window to ~3 s so the corpse is
    // a visible beat instead of a one-frame pop. Pairs with the attack-
    // lunge (iter 23:30) and hit-flinch (iter 00:47) as the third beat in
    // the combat-feedback triplet (anticipation → recoil → consequence).
    //
    // Why the trigger lives in HealthSystem::handleDeath and not in
    // CombatSystem::applyDamage:
    //   handleDeath is the single canonical "this entity just died" point
    //   in the codebase — it fires for ALL death paths (direct combat,
    //   DOT ticks via CombatSystem::applyDamageWithType, scripted kills
    //   via HealthSystem::kill, and any future damage source we wire).
    //   CombatSystem only sees its own swing/projectile path; status-
    //   effect kills happen inside processStatusEffects which calls
    //   applyDamageWithType, which doesn't touch CombatSystem's
    //   isDead branch. Hooking here is the union of all paths.
    //
    // Why this works without an explicit pause() at the clip end:
    //   Animator::updateAnimation clamps non-looping clips to their
    //   duration and flips m_playing=false on completion (Animator.cpp
    //   line 387-389). Once m_playing is false, update() exits at the
    //   top guard and m_currentPose stays at the final frame.
    //   wireLocomotionTransitions only registers transitions between
    //   idle/walk/run (no fromState=="layDown" exists), so even with
    //   the speed parameter still being fed (which we gate against
    //   anyway via deathPosed in CatAnnihilation::update), there's no
    //   transition that could pull the corpse out of layDown.
    //
    // Why we zero the velocity:
    //   AIState::Dead has an empty updateDeadState (EnemyAISystem.cpp
    //   line 158), so the AI no longer drives velocity — but the
    //   velocity from the last pre-death AI tick stays on the
    //   MovementComponent and gets applied by whatever physics tick
    //   advances position. Without the zero, a charging dog hits 0 hp
    //   and continues sliding forward as a corpse for 3 s before
    //   despawn. movement->stop() is the documented "freeze in place"
    //   helper.
    //
    // Why this is null-tolerant on every component lookup:
    //   Death paths can fire for entities with very partial component
    //   sets — a damage-source-marker entity from a particle DOT or a
    //   testing fixture might have HealthComponent and nothing else.
    //   Skipping silently when MeshComponent / Animator / movement is
    //   absent keeps the path safe for any future damageable entity
    //   that doesn't render a mesh.
    if (auto* mesh = ecs_->getComponent<MeshComponent>(entity)) {
        if (!mesh->deathPosed && mesh->animator
            && mesh->animator->hasState(kDeathPoseClipName)) {
            mesh->animator->play(kDeathPoseClipName, kDeathPoseTransitionSeconds);
            mesh->deathPosed = true;

            // One-time confirmation log per session — a "the death-pose
            // path is alive" signal for portfolio playtest reviewers
            // and a regression canary if a future refactor accidentally
            // disconnects the trigger. Mirrors the same pattern the
            // attack-lunge and hit-flinch iterations established.
            static bool firstDeathPoseLogged = false;
            if (!firstDeathPoseLogged) {
                firstDeathPoseLogged = true;
                Engine::Logger::info(
                    std::string("[HealthSystem] first death-pose triggered (entity=")
                    + std::to_string(entity.index())
                    + ", clip=" + kDeathPoseClipName + ")");
            }
        }
    }

    // Bump the despawn window even if the entity has no mesh to pose —
    // a future test fixture or proxy entity that has health but no
    // visible model might still want to see the death-state callback
    // fire and tick down before the entity is reaped. The default 1 s
    // is preserved for entities whose HealthComponent was hand-tuned
    // to despawn fast (none in the current codebase, but the pattern
    // is "bump the per-entity field, don't edit the default").
    if (auto* health = ecs_->getComponent<HealthComponent>(entity)) {
        health->deathAnimationDuration = kDeathPoseHoldSeconds;
    }

    // Stop the corpse from sliding under whatever velocity the AI had
    // applied immediately before the killing blow. Safe even when the
    // entity has no MovementComponent (skipped silently).
    if (auto* movement = ecs_->getComponent<MovementComponent>(entity)) {
        movement->stop();
    }
}

void HealthSystem::handleEnemyDeath(CatEngine::Entity entity) {
    auto* enemy = ecs_->getComponent<EnemyComponent>(entity);
    if (!enemy) return;

    // Loot spawning, score adding, and death effects are handled by the game layer
    // via the onEntityDeath_ callback registered during game initialization.
    // This allows the CatAnnihilation class to coordinate with:
    // - LootSystem for item drops
    // - ScoreSystem/GameState for score tracking
    // - GameAudio for death sounds
    // - ParticleSystem for death visual effects
}

void HealthSystem::handlePlayerDeath(CatEngine::Entity /*entity*/) {
    // Game over and death effects are handled by the game layer
    // via the onEntityDeath_ callback registered during game initialization.
    // This allows the CatAnnihilation class to:
    // - Trigger game over state transition
    // - Play player death animation and sounds
    // - Show game over UI
}

bool HealthSystem::applyDamage(CatEngine::Entity entity, float damage) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return false;
    }

    // Check if invincible
    if (health->isInvincible()) {
        return false;
    }

    // Check if already dead
    if (health->isDead) {
        return false;
    }

    // Apply damage
    health->currentHealth -= damage;
    health->timeSinceLastDamage = 0.0f;

    // Clamp to 0
    if (health->currentHealth < 0.0f) {
        health->currentHealth = 0.0f;
    }

    // Per-hit log. Fires only on successful damage application (past the
    // invincibility + already-dead gates above), so the frequency is
    // bounded by how often attacks actually land — typically 1-10 Hz
    // during combat, not per-frame. The log line is what finally lets a
    // portfolio viewer or nightly agent answer "why did the player die"
    // without attaching a debugger: entity id, damage amount, resulting
    // hp. Distinguishing an entity id alone isn't perfect (could be
    // player or enemy) but the adjacent [kill]/[death] lines already
    // disambiguate downstream in the kill/death callbacks.
    // entity.index() gives the slot number which is small & human-readable;
    // id includes the 32-bit generation counter in the high bits which is
    // noisy in a log line and not what a human cross-references with other
    // log lines. Entity isn't std::to_string'able directly — it's a
    // generational-index struct, not a primitive.
    Engine::Logger::info(
        std::string("[damage] entity=") + std::to_string(entity.index()) +
        " dmg=" + std::to_string(damage) +
        " hp=" + std::to_string(health->currentHealth) +
        "/" + std::to_string(health->maxHealth));

    // Trigger callback
    if (onDamageTaken_) {
        onDamageTaken_(entity, damage);
    }

    return true;
}

float HealthSystem::heal(CatEngine::Entity entity, float amount) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return 0.0f;
    }

    // Can't heal if dead
    if (health->isDead) {
        return 0.0f;
    }

    // Calculate actual heal amount
    float actualHeal = std::min(amount, health->maxHealth - health->currentHealth);
    if (actualHeal <= 0.0f) {
        return 0.0f;
    }

    // Apply heal
    health->currentHealth += actualHeal;

    // Clamp to max
    if (health->currentHealth > health->maxHealth) {
        health->currentHealth = health->maxHealth;
    }

    return actualHeal;
}

void HealthSystem::kill(CatEngine::Entity entity) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return;
    }

    health->currentHealth = 0.0f;
    health->isDead = true;
    handleDeath(entity);
}

void HealthSystem::revive(CatEngine::Entity entity, float health_amount) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return;
    }

    health->isDead = false;
    health->deathTimer = 0.0f;
    health->currentHealth = std::min(health_amount, health->maxHealth);
    health->invincibilityTimer = 1.0f; // Brief invincibility after revive
}

} // namespace CatGame

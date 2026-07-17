#include "EnemyAISystem.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/MovementComponent.hpp"
#include "../components/CombatComponent.hpp"
#include "../config/WebParityConfig.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/math/Quaternion.hpp"
#include <cmath>

namespace CatGame {

EnemyAISystem::EnemyAISystem(int priority)
    : System(priority)
{}

void EnemyAISystem::update(float dt) {
    if (!ecs_) return;

    // Query all enemies
    auto query = ecs_->query<EnemyComponent, Engine::Transform>();

    for (auto [entity, enemy, transform] : query.view()) {
        // Skip dead enemies (cleanup is handled by HealthSystem)
        auto* health = ecs_->getComponent<HealthComponent>(entity);
        if (health && !health->isAlive()) {
            if (enemy->state != AIState::Dead) {
                transitionToState(*enemy, AIState::Dead);
            }
        }

        updateEnemyAI(entity, *enemy, dt);
    }
}

void EnemyAISystem::updateEnemyAI(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    enemy.stateTimer += dt;

    // Update attack cooldown
    if (enemy.attackCooldownTimer > 0.0f) {
        enemy.attackCooldownTimer -= dt;
    }

    // State machine
    switch (enemy.state) {
        case AIState::Idle:
            updateIdleState(entity, enemy, dt);
            break;
        case AIState::Chasing:
            updateChasingState(entity, enemy, dt);
            break;
        case AIState::Attacking:
            updateAttackingState(entity, enemy, dt);
            break;
        case AIState::Dead:
            updateDeadState(entity, enemy, dt);
            break;
    }
}

void EnemyAISystem::updateIdleState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    // Wait for idle timer
    if (enemy.stateTimer < enemy.idleWaitTime) {
        return;
    }

    // Check if target is valid and in aggro range
    if (!ecs_->isAlive(enemy.target)) {
        return;
    }

    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    auto* targetTransform = ecs_->getComponent<Engine::Transform>(enemy.target);

    if (!transform || !targetTransform) {
        return;
    }

    float distance = getDistanceToTarget(transform->position, targetTransform->position);

    if (distance <= enemy.aggroRange) {
        transitionToState(enemy, AIState::Chasing);
    }
}

void EnemyAISystem::updateChasingState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    if (!ecs_->isAlive(enemy.target)) {
        transitionToState(enemy, AIState::Idle);
        return;
    }

    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    auto* targetTransform = ecs_->getComponent<Engine::Transform>(enemy.target);

    if (!transform || !targetTransform) {
        return;
    }

    float distance = getDistanceToTarget(transform->position, targetTransform->position);

    // Check if in attack range
    if (distance <= enemy.attackRange) {
        transitionToState(enemy, AIState::Attacking);
        return;
    }

    // Check if target escaped
    if (distance > enemy.aggroRange * 1.5f) {
        transitionToState(enemy, AIState::Idle);
        return;
    }

    // Move toward target
    moveTowardTarget(entity, targetTransform->position, enemy.moveSpeed, dt);
    faceTarget(entity, targetTransform->position);

    // Web-parity shield: after the enemy's position has advanced this frame,
    // enforce the player's shield sphere. The web runs its shield collision as
    // a post-move position validation (LocalEnemySystem.tsx:296-338), so we
    // mirror that ordering here — the dog moves, then any penetration of the
    // sphere is corrected, producing the "dogs pile up against the shield"
    // behaviour rather than letting them clip through the player.
    applyShieldBarrier(entity, enemy);
}

void EnemyAISystem::updateAttackingState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    if (!ecs_->isAlive(enemy.target)) {
        transitionToState(enemy, AIState::Idle);
        return;
    }

    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    auto* targetTransform = ecs_->getComponent<Engine::Transform>(enemy.target);

    if (!transform || !targetTransform) {
        return;
    }

    float distance = getDistanceToTarget(transform->position, targetTransform->position);

    // Check if target moved out of attack range
    if (distance > enemy.attackRange * 1.2f) {
        transitionToState(enemy, AIState::Chasing);
        return;
    }

    // Face the target
    faceTarget(entity, targetTransform->position);

    // Attack if cooldown is ready
    if (enemy.canAttack()) {
        auto* targetHealth = ecs_->getComponent<HealthComponent>(enemy.target);
        if (targetHealth) {
            // ---- Shared-i-frame gate: web parity removes it -----------------
            // The pre-parity native build stamped a shared 0.2 s player i-frame
            // after ANY dog hit (below) and gated every other attacking dog
            // behind !isInvincible(), so a mob could only land 15 damage per
            // 0.2 s — a 75 DPS ceiling that made a swarm strictly gentler than
            // the web. The web has no such shared window: damagePlayer
            // (gameStore.ts:671-693) subtracts every hit with ZERO invincibility
            // and each dog swings on its own 1000 ms cooldown
            // (LocalEnemySystem.tsx:353-377, keyed on per-enemy lastAttackTime),
            // so N dogs in the ring each land their full 15 in the SAME frame.
            // Under parity we therefore DROP the gate and evaluate every
            // attacking dog's blow independently; with parity off the native
            // gate is preserved for native-flavor balance. `WebParity::kEnabled`
            // is constexpr, so the unused branch folds out — no per-frame cost.
            //
            // Skipping the gate ALSO skips the cooldown reset (as the original
            // did), so a native dog blocked by the shared i-frame does not
            // consume its swing and re-attempts next frame — unchanged behavior
            // off parity.
            bool blockedBySharedIFrame = false;
            if constexpr (!WebParity::kEnabled) {
                blockedBySharedIFrame = targetHealth->isInvincible();
            }

            if (!blockedBySharedIFrame) {
                // Web-parity shield front-arc negation (LocalEnemySystem.tsx:356-375):
                // if the player is raising the shield and this dog is attacking from
                // within the front arc of the player's facing, the blow is fully
                // negated — no health is lost. The dog still "swings" and is put on
                // cooldown below (matching tsx:377, where enemy.lastAttackTime is
                // stamped OUTSIDE the shieldBlocks branch), so a blocked dog does not
                // retry every frame; it just harmlessly bonks the shield each cycle.
                const bool negatedByShield =
                    isAttackNegatedByShield(enemy, *transform, *targetTransform);

                if (!negatedByShield) {
                    // Route enemy damage through HealthComponent::damage() instead
                    // of mutating currentHealth directly. The direct-mutation
                    // pre-fix path:
                    //
                    //   (a) bypassed HealthComponent::damage()'s own invincibility
                    //       + death-flag bookkeeping, so a target dropped to ≤0 hp
                    //       by this branch never flipped isDead until the next
                    //       HealthSystem::updateHealth tick — a one-frame window
                    //       where an enemy could deal a "post-mortem" second hit on
                    //       the same target before HealthSystem caught up. Removing
                    //       the shared i-frame under parity lets multiple dogs hit
                    //       the player in one frame, so that window is now exercised
                    //       every swarm — but it is HARMLESS for the player: the
                    //       cat's HealthComponent::onDeath is deliberately UNSET
                    //       (CatEntity.cpp), so damage() never dispatches death
                    //       inline, and the canonical Playing→GameOver transition
                    //       fires EXACTLY ONCE from HealthSystem::updateHealth behind
                    //       its `!isDead` guard. A same-frame second hit on a 0-hp
                    //       player therefore cannot double-fire GameOver.
                    //
                    //   (b) never tagged a damage source, so the friendly-fire
                    //       detection layer (HitInfo.attacker / HealthComponent
                    //       callbacks) couldn't tell a dog-on-cat hit from a dog-
                    //       on-dog hit. Routing through damage() and stamping
                    //       lastDamageType=Physical along with the entity-tagged
                    //       fields means the death callback's EntityDeathEvent
                    //       picks the right (attacker, target) pair for whatever
                    //       game-layer subscriber wants to gate on it.
                    targetHealth->lastDamageType = DamageType::Physical;
                    targetHealth->damage(enemy.attackDamage);

                    // Overwrite the i-frame HealthComponent::damage() just wrote
                    // (it stores invincibilityDuration — 0.5 s by default — into
                    // invincibilityTimer). Under parity this becomes 0 s
                    // (kEnemyMeleeIFrameSeconds), so the NEXT dog in the same swarm
                    // is not blocked by damage()'s OWN internal i-frame check and
                    // lands its own 15 this frame — reproducing the web's uncapped
                    // melee. With parity off it stays the tighter 0.2 s native
                    // window (dogs are meant to chain-attack faster than the
                    // player's own i-frame budget). The native value is a local
                    // constant on purpose: it is native-flavor balance, NOT a web
                    // literal, so it does not belong in WebParityConfig.hpp.
                    if constexpr (WebParity::kEnabled) {
                        targetHealth->invincibilityTimer =
                            WebParity::kEnemyMeleeIFrameSeconds;
                    } else {
                        constexpr float kNativeMeleeIFrameSeconds = 0.2f;
                        targetHealth->invincibilityTimer = kNativeMeleeIFrameSeconds;
                    }
                }

                // Reset attack cooldown whether or not the shield ate the hit — a
                // blocked swing still consumes the dog's attack, so it must wait a
                // full cooldown before bonking the shield again. Under parity this
                // per-dog cooldown is the ONLY rate limit on melee (there is no
                // shared player i-frame), so each dog still swings just once per
                // its own 1.0 s kEnemyAttackCooldown, exactly like the web.
                enemy.attackCooldownTimer = enemy.attackCooldown;
            }
        }
    }
}

void EnemyAISystem::updateDeadState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    // Dead state is handled by HealthSystem
    // This is just here for completeness
}

void EnemyAISystem::transitionToState(EnemyComponent& enemy, AIState newState) {
    if (enemy.state == newState) {
        return;
    }

    enemy.state = newState;
    enemy.stateTimer = 0.0f;

    // State-specific initialization
    switch (newState) {
        case AIState::Idle:
            // Reset to idle
            break;
        case AIState::Chasing:
            // Start chasing
            break;
        case AIState::Attacking:
            // Reset attack cooldown
            enemy.attackCooldownTimer = 0.0f;
            break;
        case AIState::Dead:
            // Begin death
            break;
    }
}

bool EnemyAISystem::isTargetInRange(const Engine::vec3& position, const Engine::vec3& targetPos, float range) const {
    return getDistanceToTarget(position, targetPos) <= range;
}

float EnemyAISystem::getDistanceToTarget(const Engine::vec3& position, const Engine::vec3& targetPos) const {
    Engine::vec3 diff = targetPos - position;
    return diff.length();
}

void EnemyAISystem::moveTowardTarget(CatEngine::Entity entity, const Engine::vec3& targetPos, float speed, float dt) {
    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    if (!transform) return;

    // Calculate direction to target
    Engine::vec3 direction = (targetPos - transform->position).normalized();

    // Move position
    transform->position += direction * speed * dt;
}

void EnemyAISystem::faceTarget(CatEngine::Entity entity, const Engine::vec3& targetPos) {
    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    if (!transform) return;

    // Calculate direction to target
    Engine::vec3 direction = (targetPos - transform->position).normalized();

    // Only rotate if direction is valid
    if (direction.lengthSquared() > Engine::Math::EPSILON) {
        // Create look rotation (looking along direction, with up vector)
        transform->lookAt(targetPos, Engine::vec3(0.0f, 1.0f, 0.0f));
    }
}

void EnemyAISystem::applyShieldBarrier(CatEngine::Entity enemyEntity, const EnemyComponent& enemy) {
    // The shield barrier is a pure web-parity feature: the original native
    // build had no shield item and no physical enemy-blocking sphere, so there
    // is no pre-parity behaviour to fall back to. Gating the whole body on the
    // parity master switch means the barrier compiles out entirely when the
    // native-flavour build is selected, and enemies close on the player exactly
    // as they did before.
    if constexpr (WebParity::kEnabled) {
        // The shield only pushes enemies while the player is actively raising
        // it. shieldRaised and facingYaw are published each frame onto the
        // player's CombatComponent by the hotbar/input layer; the AI is a pure
        // reader of that state (per the shared cross-agent contract).
        auto* playerCombat = ecs_->getComponent<CombatComponent>(enemy.target);
        if (playerCombat == nullptr || !playerCombat->shieldRaised) {
            return;
        }

        auto* enemyTransform = ecs_->getComponent<Engine::Transform>(enemyEntity);
        auto* playerTransform = ecs_->getComponent<Engine::Transform>(enemy.target);
        if (enemyTransform == nullptr || playerTransform == nullptr) {
            return;
        }

        // Web literals from LocalEnemySystem.tsx. These cannot live in
        // WebParityConfig.hpp because that header is owned by a different agent
        // in this integration; they are declared here with the same exact-line
        // citations the config header uses, so a future consolidation can lift
        // them verbatim.
        constexpr float kShieldDistance = 1.2f;                   // tsx:236,299 shieldDistance
        constexpr float kShieldRadius = 0.9f;                     // tsx:264,302 shieldRadius (1.5x base)
        constexpr float kInFrontMaxProjection = kShieldDistance + 0.5f; // tsx:261,319 dot < shieldDistance + 0.5
        constexpr float kSurfaceEpsilon = 0.1f;                   // tsx:333 pushed to shieldRadius + 0.1

        // Native facing convention (shared contract): facing =
        // (sin yaw, 0, -cos yaw). The web forward is (sin R, cos R) in its own
        // frame (CatCharacter/index.tsx:289-290); the hotbar layer resolves that
        // into facingYaw so this native formula reproduces the same world-space
        // forward the shield extends along.
        const float yaw = playerCombat->facingYaw;
        const Engine::vec3 facing(std::sin(yaw), 0.0f, -std::cos(yaw));

        const Engine::vec3& playerPos = playerTransform->position;
        const Engine::vec3 shieldCenter = playerPos + facing * kShieldDistance;

        // All shield math is planar (x/z). The enemy's ground height (y) is
        // never touched, matching the web's 2D collision resolution.
        const Engine::vec3 enemyPos = enemyTransform->position;
        const float toCenterX = enemyPos.x - shieldCenter.x;
        const float toCenterZ = enemyPos.z - shieldCenter.z;
        const float distanceToCenter =
            std::sqrt(toCenterX * toCenterX + toCenterZ * toCenterZ);

        // Forward projection of the enemy relative to the player. The web only
        // treats an enemy as blocking the shield when this projection is
        // positive and below shieldDistance + 0.5 (tsx:310-319), which clips the
        // far hemisphere of the sphere out of the collision — a web quirk we
        // reproduce deliberately for 1:1 behaviour.
        const float forwardProjection =
            (enemyPos.x - playerPos.x) * facing.x + (enemyPos.z - playerPos.z) * facing.z;
        const bool isInFrontOfShield =
            forwardProjection > 0.0f && forwardProjection < kInFrontMaxProjection;

        // Snap a penetrating enemy to just outside the sphere surface along the
        // radial from the shield center (tsx:321-336). The distanceToCenter
        // guard doubles as the web's `correctionDistance > 0` divide-by-zero
        // check for an enemy sitting exactly on the center.
        if (distanceToCenter < kShieldRadius &&
            isInFrontOfShield &&
            distanceToCenter > Engine::Math::EPSILON) {
            const float targetDistance = kShieldRadius + kSurfaceEpsilon;
            enemyTransform->position.x =
                shieldCenter.x + (toCenterX / distanceToCenter) * targetDistance;
            enemyTransform->position.z =
                shieldCenter.z + (toCenterZ / distanceToCenter) * targetDistance;
        }
    }
}

bool EnemyAISystem::isAttackNegatedByShield(const EnemyComponent& enemy,
                                            const Engine::Transform& enemyTransform,
                                            const Engine::Transform& playerTransform) const {
    if constexpr (WebParity::kEnabled) {
        // Same reader contract as applyShieldBarrier: the block only applies
        // while the player holds the shield up.
        auto* playerCombat = ecs_->getComponent<CombatComponent>(enemy.target);
        if (playerCombat == nullptr || !playerCombat->shieldRaised) {
            return false;
        }

        // Native facing convention (shared contract): (sin yaw, 0, -cos yaw),
        // unit length because sin^2 + cos^2 == 1.
        const float yaw = playerCombat->facingYaw;
        const Engine::vec3 facing(std::sin(yaw), 0.0f, -std::cos(yaw));

        // Planar player->enemy vector: the direction the incoming attack comes
        // from, measured from the player. The web resolves the same
        // relationship (LocalEnemySystem.tsx:363-366) to decide whether the
        // attacker is in front of the shield.
        const float toEnemyX = enemyTransform.position.x - playerTransform.position.x;
        const float toEnemyZ = enemyTransform.position.z - playerTransform.position.z;
        const float toEnemyLengthSquared = toEnemyX * toEnemyX + toEnemyZ * toEnemyZ;
        if (toEnemyLengthSquared <= Engine::Math::EPSILON) {
            // Degenerate: the enemy is exactly on the player, so there is no
            // well-defined attack direction to test — do not block.
            return false;
        }

        // cos(angle) between facing (unit) and the player->enemy direction is
        // the planar dot product divided by the player->enemy length.
        const float dot = facing.x * toEnemyX + facing.z * toEnemyZ;
        const float cosAngle = dot / std::sqrt(toEnemyLengthSquared);

        // Negate when the attacker is inside the player's front arc — within
        // +/-45 degrees of facing (a 90-degree-wide front arc), per the shared
        // contract and PARITY_MATRIX "front 90 arc" description. cos(45deg) =
        // sqrt(2)/2, so an attacker whose facing-cosine is at least this stands
        // inside the arc.
        //
        // WEB-SOURCE NOTE: the raw web expression (tsx:364-369) builds
        // angleToEnemy from the *enemy->player* vector (dx,dz = player - enemy)
        // and blocks at normalizedAngle <= PI/2; because that vector is the
        // reverse of player->enemy, the literal check actually gates the REAR
        // hemisphere. The shared contract specifies the intended front +/-45
        // arc (what a player experiences as "the shield blocks frontal hits"),
        // which is what is reproduced here.
        constexpr float kFrontArcCosThreshold = 0.70710678118654752f; // cos(pi/4)
        return cosAngle >= kFrontArcCosThreshold;
    } else {
        // With parity off there is no shield feature, so attacks are never
        // negated and the dog's damage always lands.
        return false;
    }
}

} // namespace CatGame

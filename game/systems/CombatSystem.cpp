#include "CombatSystem.hpp"
#include "../components/GameComponents.hpp"
#include "../components/combat_components.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Math.hpp"
#include <cmath>
#include <random>
#include <algorithm>

namespace CatGame {

// Random number generator for crits and effects
static std::random_device rd;
static std::mt19937 gen(rd());

CombatSystem::CombatSystem(int priority)
    : System(priority)
{
}

void CombatSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);
    gameTime_ = 0.0f;
}

void CombatSystem::shutdown() {
    clearProjectiles();
}

void CombatSystem::update(float dt) {
    if (!isEnabled()) {
        return;
    }

    // Update game time for perfect block timing
    gameTime_ += dt;

    // Update combat states
    updateBlockStates(dt);
    updateDodgeStates(dt);
    updateComboStates(dt);

    // Process status effects
    processStatusEffects(dt);

    // Process attacks
    processMeleeAttacks();
    processProjectileAttacks();

    // Update active projectiles
    updateProjectiles(dt);
}

// ===== BLOCKING SYSTEM =====

bool CombatSystem::startBlock(CatEngine::Entity entity) {
    auto* blockComp = ecs_->getComponent<BlockComponent>(entity);
    if (!blockComp) {
        return false;
    }

    return blockComp->state.startBlock(gameTime_);
}

void CombatSystem::endBlock(CatEngine::Entity entity) {
    auto* blockComp = ecs_->getComponent<BlockComponent>(entity);
    if (!blockComp) {
        return;
    }

    blockComp->state.endBlock();
}

bool CombatSystem::isBlocking(CatEngine::Entity entity) const {
    auto* blockComp = ecs_->getComponent<BlockComponent>(entity);
    if (!blockComp) {
        return false;
    }

    return blockComp->state.isBlocking;
}

float CombatSystem::applyBlockDamageReduction(CatEngine::Entity entity, float incomingDamage, float attackTiming) {
    auto* blockComp = ecs_->getComponent<BlockComponent>(entity);
    if (!blockComp || !blockComp->state.isBlocking) {
        return incomingDamage;
    }

    // Check for perfect block
    if (blockComp->state.isPerfectBlock(attackTiming)) {
        // Perfect block - no damage and trigger callback
        if (onPerfectBlock) {
            onPerfectBlock(entity);
        }
        return incomingDamage * (1.0f - blockComp->state.perfectBlockDamageReduction);
    }

    // Regular block - reduce damage
    return incomingDamage * (1.0f - blockComp->state.damageReduction);
}

bool CombatSystem::isPerfectBlock(CatEngine::Entity entity, float attackTiming) const {
    auto* blockComp = ecs_->getComponent<BlockComponent>(entity);
    if (!blockComp) {
        return false;
    }

    return blockComp->state.isPerfectBlock(attackTiming);
}

float CombatSystem::getBlockStamina(CatEngine::Entity entity) const {
    auto* blockComp = ecs_->getComponent<BlockComponent>(entity);
    if (!blockComp) {
        return 0.0f;
    }

    return blockComp->state.blockStamina;
}

void CombatSystem::updateBlockStates(float dt) {
    ecs_->forEach<BlockComponent>([dt](CatEngine::Entity entity, BlockComponent* block) {
        block->state.update(dt);
    });
}

// ===== DODGING SYSTEM =====

bool CombatSystem::startDodge(CatEngine::Entity entity, const Engine::vec3& direction) {
    auto* dodgeComp = ecs_->getComponent<DodgeComponent>(entity);
    auto* transform = ecs_->getComponent<Engine::Transform>(entity);

    if (!dodgeComp || !transform) {
        return false;
    }

    bool started = dodgeComp->state.startDodge(direction, transform->position);

    if (started && onDodge) {
        onDodge(entity);
    }

    return started;
}

bool CombatSystem::canDodge(CatEngine::Entity entity) const {
    auto* dodgeComp = ecs_->getComponent<DodgeComponent>(entity);
    if (!dodgeComp) {
        return false;
    }

    return dodgeComp->state.canDodge();
}

bool CombatSystem::isDodging(CatEngine::Entity entity) const {
    auto* dodgeComp = ecs_->getComponent<DodgeComponent>(entity);
    if (!dodgeComp) {
        return false;
    }

    return dodgeComp->state.isDodging;
}

bool CombatSystem::isInvincible(CatEngine::Entity entity) const {
    auto* dodgeComp = ecs_->getComponent<DodgeComponent>(entity);
    if (!dodgeComp) {
        return false;
    }

    return dodgeComp->state.isInvincible();
}

float CombatSystem::getDodgeCooldownProgress(CatEngine::Entity entity) const {
    auto* dodgeComp = ecs_->getComponent<DodgeComponent>(entity);
    if (!dodgeComp) {
        return 0.0f;
    }

    return dodgeComp->state.getCooldownProgress();
}

void CombatSystem::updateDodgeStates(float dt) {
    ecs_->forEach<DodgeComponent, Engine::Transform>(
        [dt](CatEngine::Entity entity, DodgeComponent* dodge, Engine::Transform* transform) {
            // Apply dodge movement
            if (dodge->state.isDodging) {
                Engine::vec3 velocity = dodge->state.getDodgeVelocity();
                transform->position += velocity * dt;
            }

            dodge->state.update(dt);
        }
    );
}

// ===== COMBO SYSTEM =====

void CombatSystem::performAttack(CatEngine::Entity entity, const std::string& attackType) {
    auto* comboComp = ecs_->getComponent<ComboComponent>(entity);
    if (!comboComp || attackType.empty()) {
        return;
    }

    char attackChar = attackType[0];
    comboComp->state.addAttack(attackChar);

    if (onComboHit) {
        onComboHit(entity, comboComp->state.comboStep);
    }
}

void CombatSystem::performComboAttack(CatEngine::Entity entity) {
    performAttack(entity, "L");  // Default to light attack
}

bool CombatSystem::isInCombo(CatEngine::Entity entity) const {
    auto* comboComp = ecs_->getComponent<ComboComponent>(entity);
    if (!comboComp) {
        return false;
    }

    return comboComp->state.isInCombo();
}

int CombatSystem::getComboStep(CatEngine::Entity entity) const {
    auto* comboComp = ecs_->getComponent<ComboComponent>(entity);
    if (!comboComp) {
        return 0;
    }

    return comboComp->state.comboStep;
}

float CombatSystem::getCurrentDamageMultiplier(CatEngine::Entity entity) const {
    auto* comboComp = ecs_->getComponent<ComboComponent>(entity);
    if (!comboComp) {
        return 1.0f;
    }

    return comboComp->state.getCurrentDamageMultiplier();
}

void CombatSystem::updateComboStates(float dt) {
    ecs_->forEach<ComboComponent>([dt](CatEngine::Entity entity, ComboComponent* combo) {
        combo->state.update(dt);
    });
}

// ===== ENHANCED DAMAGE SYSTEM =====

float CombatSystem::calculateDamage(
    CatEngine::Entity attacker,
    CatEngine::Entity defender,
    const AttackData& attack
) {
    float damage = attack.baseDamage;

    // Apply combo multiplier
    float comboMultiplier = getCurrentDamageMultiplier(attacker);
    damage *= comboMultiplier;

    // Apply status effect modifiers (attacker)
    auto* attackerEffects = ecs_->getComponent<StatusEffectsComponent>(attacker);
    if (attackerEffects) {
        damage *= attackerEffects->getDamageMultiplier();
    }

    // Check for critical hit
    bool isCrit = false;
    if (attack.critChance > 0.0f) {
        float roll = randomFloat(0.0f, 1.0f);
        if (roll < attack.critChance) {
            damage *= attack.critMultiplier;
            isCrit = true;
        }
    }

    // Apply defender's damage reduction
    auto* defenderEffects = ecs_->getComponent<StatusEffectsComponent>(defender);
    if (defenderEffects) {
        float reduction = defenderEffects->getDamageReduction();
        damage *= (1.0f - reduction);
    }

    // Check for block
    if (attack.canBeBlocked && isBlocking(defender)) {
        damage = applyBlockDamageReduction(defender, damage, gameTime_);
    }

    // Check for dodge/invincibility
    if (attack.canBeDodged && isInvincible(defender)) {
        damage = 0.0f;
    }

    return std::max(0.0f, damage);
}

void CombatSystem::applyDamageWithType(
    CatEngine::Entity target,
    float damage,
    DamageType type,
    CatEngine::Entity attacker
) {
    auto* health = ecs_->getComponent<HealthComponent>(target);
    if (!health) {
        return;
    }

    // Apply damage
    bool damageApplied = health->damage(damage);

    if (damageApplied) {
        // Trigger callbacks
        if (onDamageTaken) {
            onDamageTaken(target, damage);
        }

        if (onDamageDealt && attacker.isValid()) {
            onDamageDealt(attacker, damage);
        }
    }
}

void CombatSystem::applyKnockback(
    CatEngine::Entity target,
    const Engine::vec3& direction,
    float force
) {
    auto* movement = ecs_->getComponent<MovementComponent>(target);
    if (!movement) {
        return;
    }

    // Add knockback velocity
    Engine::vec3 knockbackVel = direction.normalized() * force;
    movement->velocity += knockbackVel;
}

void CombatSystem::applyStun(CatEngine::Entity target, float duration) {
    StatusEffect stunEffect(StatusEffectType::Stunned, duration, 0.0f, CatEngine::Entity());
    applyStatusEffect(target, stunEffect);
}

// ===== STATUS EFFECTS =====

void CombatSystem::applyStatusEffect(CatEngine::Entity target, const StatusEffect& effect) {
    auto* statusComp = ecs_->getComponent<StatusEffectsComponent>(target);
    if (!statusComp) {
        return;
    }

    statusComp->addEffect(effect);
}

void CombatSystem::removeStatusEffect(CatEngine::Entity target, StatusEffectType type) {
    auto* statusComp = ecs_->getComponent<StatusEffectsComponent>(target);
    if (!statusComp) {
        return;
    }

    statusComp->removeEffect(type);
}

std::vector<StatusEffect> CombatSystem::getActiveEffects(CatEngine::Entity target) const {
    auto* statusComp = ecs_->getComponent<StatusEffectsComponent>(target);
    if (!statusComp) {
        return {};
    }

    return statusComp->effects;
}

void CombatSystem::processStatusEffects(float dt) {
    ecs_->forEach<StatusEffectsComponent, HealthComponent>(
        [this, dt](CatEngine::Entity entity, StatusEffectsComponent* status, HealthComponent* health) {
            // Update all effects
            status->update(dt);

            // Process ticks (DOT/HOT)
            for (auto& effect : status->effects) {
                if (effect.shouldTick()) {
                    float value = effect.getEffectiveValue();

                    switch (effect.type) {
                        case StatusEffectType::Burning:
                        case StatusEffectType::Poisoned:
                        case StatusEffectType::Bleeding:
                        case StatusEffectType::Frozen:
                            // Apply damage
                            applyDamageWithType(entity, value, effect.damageType, effect.source);
                            break;

                        case StatusEffectType::Regenerating:
                            // Apply healing
                            health->heal(value);
                            break;

                        default:
                            break;
                    }
                }
            }
        }
    );
}

// ===== ORIGINAL COMBAT PROCESSING =====

void CombatSystem::setOnHitCallback(std::function<void(const HitInfo&)> callback) {
    onHitCallback_ = callback;
}

void CombatSystem::setOnKillCallback(std::function<void(CatEngine::Entity, CatEngine::Entity)> callback) {
    onKillCallback_ = callback;
}

void CombatSystem::clearProjectiles() {
    projectiles_.clear();
}

CatEngine::Entity CombatSystem::spawnProjectile(
    CatEngine::Entity owner,
    const Engine::vec3& position,
    const Engine::vec3& direction,
    float damage
) {
    Projectile projectile;
    projectile.owner = owner;
    projectile.position = position;
    projectile.velocity = direction.normalized() * projectileSpeed_;
    projectile.damage = damage;
    projectile.lifetime = 0.0f;
    projectile.active = true;

    projectiles_.push_back(projectile);

    // Return a pseudo-entity ID (index in projectile array)
    return CatEngine::Entity(static_cast<uint32_t>(projectiles_.size() - 1), 0);
}

void CombatSystem::processMeleeAttacks() {
    // Query all entities with combat and transform components
    ecs_->forEach<CombatComponent, Engine::Transform>(
        [this](CatEngine::Entity attacker, CombatComponent* combat, Engine::Transform* transform) {
            // Skip if not a melee weapon
            if (!combat->isMeleeWeapon()) {
                return;
            }

            // Skip if attack not in progress or cooldown active
            if (combat->attackCooldown <= 0.0f) {
                return; // No active attack
            }

            // Check if this is the first frame of the attack (cooldown just started)
            float cooldownDuration = combat->getCooldownDuration();
            if (combat->attackCooldown < cooldownDuration - 0.016f) {
                return; // Attack already processed in previous frames
            }

            // Check if stunned
            auto* statusEffects = ecs_->getComponent<StatusEffectsComponent>(attacker);
            if (statusEffects && !statusEffects->canAct()) {
                return;
            }

            // Get combo multiplier
            float damageMultiplier = getCurrentDamageMultiplier(attacker);

            // Find all potential targets
            ecs_->forEach<HealthComponent, Engine::Transform>(
                [this, attacker, combat, transform, damageMultiplier](
                    CatEngine::Entity target,
                    HealthComponent* targetHealth,
                    Engine::Transform* targetTransform
                ) {
                    // Don't attack self
                    if (attacker == target) {
                        return;
                    }

                    // Check if target is invincible (dodging)
                    if (isInvincible(target)) {
                        return;
                    }

                    // Check if target is in range
                    if (!checkMeleeHit(transform->position, targetTransform->position, combat->attackRange)) {
                        return;
                    }

                    // Calculate final damage
                    float baseDamage = combat->getEffectiveDamage() * damageMultiplier;

                    // Check for block
                    float finalDamage = baseDamage;
                    bool wasBlocked = false;
                    bool wasPerfectBlock = false;

                    if (isBlocking(target)) {
                        finalDamage = applyBlockDamageReduction(target, baseDamage, gameTime_);
                        wasBlocked = true;
                        wasPerfectBlock = isPerfectBlock(target, gameTime_);

                        // Parry - stun attacker on perfect block
                        auto* blockComp = ecs_->getComponent<BlockComponent>(target);
                        if (wasPerfectBlock && blockComp && blockComp->state.canParry) {
                            applyStun(attacker, blockComp->state.parryStunDuration);
                        }
                    }

                    // Apply damage
                    applyDamage(attacker, target, finalDamage, targetTransform->position);

                    // Enhanced hit info
                    if (onHitCallback_) {
                        HitInfo hitInfo;
                        hitInfo.attacker = attacker;
                        hitInfo.target = target;
                        hitInfo.damage = baseDamage;
                        hitInfo.finalDamage = finalDamage;
                        hitInfo.hitPosition = targetTransform->position;
                        hitInfo.hitDirection = (targetTransform->position - transform->position).normalized();
                        hitInfo.wasBlocked = wasBlocked;
                        hitInfo.wasPerfectBlock = wasPerfectBlock;
                        hitInfo.comboStep = getComboStep(attacker);

                        onHitCallback_(hitInfo);
                    }
                }
            );
        }
    );
}

void CombatSystem::processProjectileAttacks() {
    // Query all entities with combat and transform components
    ecs_->forEach<CombatComponent, Engine::Transform>(
        [this](CatEngine::Entity attacker, CombatComponent* combat, Engine::Transform* transform) {
            // Skip if not a ranged weapon
            if (!combat->isRangedWeapon()) {
                return;
            }

            // Check if attack just started (first frame of cooldown)
            float cooldownDuration = combat->getCooldownDuration();
            if (combat->attackCooldown <= 0.0f ||
                combat->attackCooldown < cooldownDuration - 0.016f) {
                return; // No active attack or attack already processed
            }

            // Check if stunned
            auto* statusEffects = ecs_->getComponent<StatusEffectsComponent>(attacker);
            if (statusEffects && !statusEffects->canAct()) {
                return;
            }

            // Calculate spawn position (in front of attacker)
            Engine::vec3 forward = transform->forward();
            Engine::vec3 spawnPosition = transform->position + forward * 2.0f + Engine::vec3(0.0f, 1.0f, 0.0f);

            // Apply combo multiplier
            float damage = combat->getEffectiveDamage() * getCurrentDamageMultiplier(attacker);

            // Spawn projectile
            spawnProjectile(attacker, spawnPosition, forward, damage);
        }
    );
}

void CombatSystem::updateProjectiles(float dt) {
    // Update all projectiles
    for (auto& projectile : projectiles_) {
        if (!projectile.active) {
            continue;
        }

        // Update lifetime
        projectile.lifetime += dt;
        if (projectile.lifetime >= projectile.maxLifetime) {
            projectile.active = false;
            continue;
        }

        // Update position
        projectile.position += projectile.velocity * dt;

        // Check collision with entities
        bool hit = false;
        ecs_->forEach<HealthComponent, Engine::Transform>(
            [this, &projectile, &hit](
                CatEngine::Entity target,
                HealthComponent* health,
                Engine::Transform* transform
            ) {
                // Don't hit owner
                if (projectile.owner == target) {
                    return;
                }

                // Already hit something
                if (hit) {
                    return;
                }

                // Check if target is invincible (dodging)
                if (isInvincible(target)) {
                    return;
                }

                // Check collision
                if (checkProjectileHit(projectile.position, transform->position, projectileHitRadius_)) {
                    // Check for block
                    float finalDamage = projectile.damage;
                    bool wasBlocked = false;
                    bool wasPerfectBlock = false;

                    if (isBlocking(target)) {
                        finalDamage = applyBlockDamageReduction(target, projectile.damage, gameTime_);
                        wasBlocked = true;
                        wasPerfectBlock = isPerfectBlock(target, gameTime_);
                    }

                    // Apply damage
                    applyDamage(projectile.owner, target, finalDamage, projectile.position);

                    // Enhanced hit info
                    if (onHitCallback_) {
                        HitInfo hitInfo;
                        hitInfo.attacker = projectile.owner;
                        hitInfo.target = target;
                        hitInfo.damage = projectile.damage;
                        hitInfo.finalDamage = finalDamage;
                        hitInfo.hitPosition = projectile.position;
                        hitInfo.hitDirection = projectile.velocity.normalized();
                        hitInfo.wasBlocked = wasBlocked;
                        hitInfo.wasPerfectBlock = wasPerfectBlock;
                        hitInfo.comboStep = getComboStep(projectile.owner);

                        onHitCallback_(hitInfo);
                    }

                    // Deactivate projectile
                    projectile.active = false;
                    hit = true;
                }
            }
        );
    }

    // Remove inactive projectiles
    projectiles_.erase(
        std::remove_if(
            projectiles_.begin(),
            projectiles_.end(),
            [](const Projectile& p) { return !p.active; }
        ),
        projectiles_.end()
    );
}

bool CombatSystem::checkMeleeHit(
    const Engine::vec3& attackerPos,
    const Engine::vec3& targetPos,
    float attackRange
) const {
    // Calculate distance
    Engine::vec3 delta = targetPos - attackerPos;
    float distanceSquared = delta.lengthSquared();

    // Check if within range (using squared distance for efficiency)
    return distanceSquared <= (attackRange * attackRange);
}

bool CombatSystem::checkProjectileHit(
    const Engine::vec3& projectilePos,
    const Engine::vec3& targetPos,
    float hitRadius
) const {
    // Calculate distance
    Engine::vec3 delta = targetPos - projectilePos;
    float distanceSquared = delta.lengthSquared();

    // Check if within hit radius
    return distanceSquared <= (hitRadius * hitRadius);
}

void CombatSystem::applyDamage(
    CatEngine::Entity attacker,
    CatEngine::Entity target,
    float damage,
    const Engine::vec3& hitPosition
) {
    // Get target health component
    auto* health = ecs_->getComponent<HealthComponent>(target);
    if (!health) {
        return;
    }

    // Apply damage
    bool damageApplied = health->damage(damage);

    if (!damageApplied) {
        return; // Target was invincible
    }

    // Trigger hit callback
    if (onHitCallback_) {
        HitInfo hitInfo;
        hitInfo.attacker = attacker;
        hitInfo.target = target;
        hitInfo.damage = damage;
        hitInfo.finalDamage = damage;
        hitInfo.hitPosition = hitPosition;

        // Calculate hit direction
        auto* attackerTransform = ecs_->getComponent<Engine::Transform>(attacker);
        if (attackerTransform) {
            hitInfo.hitDirection = (hitPosition - attackerTransform->position).normalized();
        }

        onHitCallback_(hitInfo);
    }

    // Check if target died
    if (health->isDead) {
        if (onKillCallback_) {
            onKillCallback_(attacker, target);
        }
    }
}

float CombatSystem::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

} // namespace CatGame

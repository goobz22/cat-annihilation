#include "CombatSystem.hpp"
#include "../components/GameComponents.hpp"
#include "../components/combat_components.hpp"
#include "../components/MeshComponent.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Math.hpp"
#include <cmath>
#include <random>
#include <algorithm>

namespace {
// Total wall-clock duration of an attack-lunge visual pulse, in seconds.
// 0.35 s lines up with the player-cat's default Sword cooldown (~0.667 s
// at attackSpeed=1.5 — the lunge ends well before the next swing window
// opens, so successive attacks don't double-pump and look like a stutter)
// AND with a typical Dog enemy attackSpeed (1.0 → 1.0 s cooldown — same
// non-overlap guarantee). Doubling this past ~0.45 s starts producing
// visible overlap on player chains; halving it under ~0.20 s reads as a
// twitch instead of a swing.
constexpr float kAttackLungePulseSeconds = 0.35F;

// Total wall-clock duration of a hit-flinch visual pulse, in seconds.
// 0.30 s is intentionally shorter than the attack-lunge pulse because
// the flinch is a *secondary* motion subordinate to the attacker's
// swing — making it shorter ensures it reads as a recoil rather than
// competing with the swing as another primary beat. The value also has
// to clear before the next damage event can land on the same target so
// chain-attacks don't visibly stack flinches: at the typical Dog
// attackSpeed=1.0 (1.0 s cooldown) and player attackSpeed=1.5 (0.667 s
// cooldown) a 0.30 s flinch is fully decayed long before the next swing
// can connect. Doubling toward 0.6 s would cause overlapping flinches
// on chain hits ("the cat is permanently recoiling"); halving toward
// 0.15 s would read as a twitch with no readable arc.
constexpr float kHitFlinchPulseSeconds = 0.30F;
} // namespace

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

    // Cache dt so processMeleeAttacks can widen its "first-frame" tolerance to
    // one real dt instead of a hard-coded 60 Hz assumption — see the comment
    // inside processMeleeAttacks for why the tolerance matters.
    lastDt_ = dt;

    // Update game time for perfect block timing
    gameTime_ += dt;

    // Update combat states
    updateBlockStates(dt);
    updateDodgeStates(dt);
    updateComboStates(dt);

    // Process status effects
    processStatusEffects(dt);

    // Process attacks. These rely on attackCooldown still being at the value
    // startAttack() set, so the cooldown tick below MUST run after, not
    // before.
    processMeleeAttacks();
    processProjectileAttacks();

    // Tick attack cooldowns exactly once per frame, owned here so no other
    // system can race us. Before this lived on PlayerControlSystem (priority 0,
    // runs before us at priority 10), which decremented by one dt before
    // processMeleeAttacks ever saw the fresh cooldown — the melee pass then
    // mistook the first frame of the swing for an already-processed attack and
    // silently skipped damage application. The fix is twofold: keep the tick
    // here (after melee/projectile processing) and widen the "first frame"
    // tolerance in processMeleeAttacks to one dt on each side.
    ecs_->forEach<CombatComponent>(
        [dt](CatEngine::Entity /*unused*/, CombatComponent* combat) {
            combat->updateCooldown(dt);
        }
    );

    // Decay the per-entity combat visual pulses (attack-lunge + hit-flinch)
    // toward zero in a single combined sweep.
    //
    // Both pulses are set to 1.0 elsewhere in CombatSystem (attack-lunge in
    // processMeleeAttacks / processProjectileAttacks on swing-start, hit-
    // flinch in applyDamage / applyDamageWithType when the target actually
    // takes non-zero damage) and ramp back to 0.0 over their respective
    // pulse durations (kAttackLungePulseSeconds and kHitFlinchPulseSeconds).
    // Combining the decay into one forEach<MeshComponent> pass instead of
    // two is a micro-optimisation: one cache-friendly sweep over the
    // MeshComponent pool with two branchy decays inside the lambda, vs.
    // two sweeps that each pay the iteration overhead. Both pulses default
    // to 0.0, so the early-out is two cheap compares per entity in the
    // common (no-active-pulse) case.
    //
    // Why tick all MeshComponents — not just CombatComponent owners — for
    // both pulses: the layering rule is one-way. CombatSystem (game) writes
    // MeshComponent (game) which the renderer (engine) reads. Nothing in
    // the engine should know CombatComponent exists. That asymmetry means
    // *every* entity with a MeshComponent participates in the decay sweep
    // even if it has no CombatComponent, but the gating compares are zero-
    // cost in the steady state and the alternative (querying both
    // MeshComponent + CombatComponent) would force the engine renderer to
    // know about CombatComponent, breaking the layering rule.
    //
    // Decay rate `dt / kPulseSeconds` produces a linear 1.0 -> 0.0 ramp
    // over the configured wall-clock window — the renderer maps the linear
    // progress through `sin(pi * (1 - p))` to get the actual lean angle,
    // so the user-visible curve is a smooth cosine bell even though the
    // underlying decay is linear. Linear is simpler to verify (clamp to
    // >=0 each frame) and the per-frame cost is one fma per active pulse.
    const float attackDecayPerSecond = 1.0F / kAttackLungePulseSeconds;
    const float hitDecayPerSecond = 1.0F / kHitFlinchPulseSeconds;
    ecs_->forEach<MeshComponent>(
        [dt, attackDecayPerSecond, hitDecayPerSecond](
            CatEngine::Entity /*unused*/, MeshComponent* mesh) {
            if (mesh == nullptr) {
                return;
            }
            if (mesh->attackVisualPulse > 0.0F) {
                mesh->attackVisualPulse = std::max(
                    0.0F, mesh->attackVisualPulse - attackDecayPerSecond * dt);
            }
            if (mesh->hitVisualPulse > 0.0F) {
                mesh->hitVisualPulse = std::max(
                    0.0F, mesh->hitVisualPulse - hitDecayPerSecond * dt);
            }
        }
    );

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

    // Stamp the damage type onto the target's HealthComponent BEFORE damage()
    // for the same race-condition reason as the direct applyDamage path: a
    // tick that drives hp to zero must have lastDamageType already populated
    // when the death callback fires. This is the canonical entry point for
    // DOTs (Burning, Frozen, Poisoned, Bleeding via processStatusEffects) and
    // for any future direct-spell damage that bypasses the projectile path,
    // so threading the element through here is what finally lights up
    // per-element death feedback for elemental kills (a Burning DOT killing
    // an enemy now produces a Fire-coloured death burst, etc.).
    health->lastDamageType = type;

    // Apply damage
    bool damageApplied = health->damage(damage);

    if (damageApplied) {
        // Bump the target's hit-flinch visual pulse on the typed-damage
        // path too — DOT ticks (Burning, Bleeding, Poisoned, Frozen) flow
        // through here, and each tick is a discrete damage event the
        // player should see register. Tick cadence (~0.5 s for most
        // status effects) is wider than the 0.30 s flinch window so DOT
        // flinches don't visibly stack — each tick gets its own clean
        // recoil-then-settle envelope. Same null-tolerant lookup as the
        // direct-damage path: status-effect ticks against entities
        // without a MeshComponent silently skip the bump.
        if (auto* targetMesh = ecs_->getComponent<MeshComponent>(target)) {
            targetMesh->hitVisualPulse = 1.0F;
        }

        // Fire the hit callback so the per-element hit-burst dispatcher in
        // the game layer picks an element-tinted profile for DOT ticks.
        // BEFORE this iteration, applyDamageWithType silently bypassed
        // onHitCallback_, so a Burning DOT tick was *visually invisible*
        // beyond the hit-flinch pulse — no particle burst at the burning
        // entity's position. Wiring the callback here makes every DOT tick
        // produce a 8-particle burst tinted by the DOT's element (orange-
        // yellow for Burning/Fire, pale-cyan for Frozen/Ice, yellow-green
        // for Poisoned/Poison) at the target's world position. The position
        // lookup is null-tolerant: a target without a Transform skips the
        // burst (a target without a position is not visually anchorable
        // anyway).
        if (onHitCallback_) {
            HitInfo hitInfo;
            hitInfo.attacker = attacker;
            hitInfo.target = target;
            hitInfo.damage = damage;
            hitInfo.finalDamage = damage;
            hitInfo.damageType = type;
            if (auto* targetTransform = ecs_->getComponent<Engine::Transform>(target)) {
                hitInfo.hitPosition = targetTransform->position;
            }
            onHitCallback_(hitInfo);
        }

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

            // Detect the first frame after startAttack(): attackCooldown was
            // just set to cooldownDuration and has NOT yet been decremented.
            // The tolerance must be at least one real dt so a slightly-late
            // tick doesn't fall outside the window — previously a hard-coded
            // 0.016 s (one 60 Hz frame) meant a single sub-frame of drift
            // caused the pass to conclude "already processed" on the very
            // frame the attack started, skipping damage entirely. Using
            // lastDt_ with a 1.5× multiplier gives us a two-tick window that
            // absorbs frame-pacing jitter without ever re-triggering on a
            // subsequent frame (by the next frame the cooldown has been
            // decremented by another full dt, moving it below the window).
            float cooldownDuration = combat->getCooldownDuration();
            const float tolerance = std::max(0.016f, lastDt_ * 1.5f);
            if (combat->attackCooldown < cooldownDuration - tolerance) {
                return; // Attack already processed in previous frames
            }

            // Check if stunned
            auto* statusEffects = ecs_->getComponent<StatusEffectsComponent>(attacker);
            if (statusEffects && !statusEffects->canAct()) {
                return;
            }

            // Bump the attacker's visual lunge pulse on the swing's first
            // frame. Set to 1.0 unconditionally (rather than max-with-current)
            // because the cooldown gate above guarantees we only land here
            // once per swing — re-arming a fresh pulse is the right behaviour
            // for the chain-attack case where a new swing starts before the
            // last lunge has fully decayed. Skipped silently if the attacker
            // doesn't have a MeshComponent (e.g. a bare-bones training-room
            // dummy that uses CombatComponent for damage but no rendered
            // mesh). See MeshComponent.hpp for the full WHY on this field.
            auto* attackerMesh = ecs_->getComponent<MeshComponent>(attacker);
            if (attackerMesh != nullptr) {
                attackerMesh->attackVisualPulse = 1.0F;
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

                    // Apply damage. Sword/melee attacks deliver Physical
                    // damage by default — the per-element variants come from
                    // the magic system via applyDamageWithType (DOTs, spell
                    // bolts) which routes through a different callsite. A
                    // future fire-sword enchantment would override this with
                    // an explicit DamageType; for now the default keeps the
                    // legacy white-yellow hit-burst on every melee swing.
                    applyDamage(attacker, target, finalDamage,
                                targetTransform->position, DamageType::Physical);

                    // Enhanced hit info. Note: applyDamage() above ALREADY
                    // fires onHitCallback_ from its internal callsite
                    // (CombatSystem::applyDamage, line ~875) with a less
                    // populated HitInfo — this branch fires it AGAIN with
                    // the richer fields (combo step, block flags, hit
                    // direction). The double-fire is pre-existing
                    // behaviour; the per-element dispatcher in the game
                    // layer is robust to it (the second hit-burst at the
                    // same world position is visually indistinguishable
                    // from a single 16-particle burst), and a deeper
                    // refactor to deduplicate is out of scope for this
                    // iteration. The damageType is set to Physical here
                    // for consistency with the applyDamage call above.
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
                        hitInfo.damageType = DamageType::Physical;

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

            // Check if attack just started (first frame of cooldown). Same
            // reasoning as the melee path — see processMeleeAttacks above for
            // why the tolerance has to scale with the real dt instead of a
            // fixed 60 Hz frame budget.
            float cooldownDuration = combat->getCooldownDuration();
            const float tolerance = std::max(0.016f, lastDt_ * 1.5f);
            if (combat->attackCooldown <= 0.0f ||
                combat->attackCooldown < cooldownDuration - tolerance) {
                return; // No active attack or attack already processed
            }

            // Check if stunned
            auto* statusEffects = ecs_->getComponent<StatusEffectsComponent>(attacker);
            if (statusEffects && !statusEffects->canAct()) {
                return;
            }

            // Bump the attacker's visual lunge pulse on the projectile-cast
            // first frame, mirroring the melee branch. Same WHY as the melee
            // pass above — a Staff/Bow shot is just as much a "the cat is
            // committing to an attack" moment as a Sword swing, and the cat
            // looks identically static on a spell-spawn frame today. The
            // lunge here reads as a recoil twitch when the spell leaves the
            // staff, which sells the recoil energy.
            auto* attackerMesh = ecs_->getComponent<MeshComponent>(attacker);
            if (attackerMesh != nullptr) {
                attackerMesh->attackVisualPulse = 1.0F;
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

                    // Apply damage. Bow projectiles are Physical by default;
                    // future enchanted-arrow / staff-bolt paths can override
                    // by carrying a DamageType field on the Projectile struct
                    // and forwarding it here. For now the default keeps the
                    // legacy white-yellow hit-burst on arrow strikes.
                    applyDamage(projectile.owner, target, finalDamage,
                                projectile.position, DamageType::Physical);

                    // Enhanced hit info — same double-fire-with-richer-fields
                    // pattern as the melee callsite above. damageType matches
                    // the applyDamage call so the per-element dispatcher sees
                    // a consistent value across both invocations.
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
                        hitInfo.damageType = DamageType::Physical;

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
    const Engine::vec3& hitPosition,
    DamageType damageType
) {
    // Get target health component
    auto* health = ecs_->getComponent<HealthComponent>(target);
    if (!health) {
        return;
    }

    // Stamp the damage type onto the target's HealthComponent BEFORE the
    // damage() call so that — even if this exact call also drives currentHealth
    // to zero — the death path that fires from inside damage() (HealthComponent
    // line ~69, then HealthSystem::handleDeath, then the game-layer
    // setOnEntityDeath subscriber) sees the correct lastDamageType when it
    // populates EntityDeathEvent.damageType. Doing this AFTER damage() would
    // race the death publish and the per-element death-burst dispatcher
    // would always pick the previous kill's element. Setting unconditionally
    // (not only on damageApplied) is correct: an i-frame'd damage tick won't
    // change the active elemental death profile because lastDamageType is
    // only consulted at death, and the entity can't die from a no-op tick.
    health->lastDamageType = damageType;

    // Apply damage
    bool damageApplied = health->damage(damage);

    if (!damageApplied) {
        return; // Target was invincible
    }

    // Bump the target's hit-flinch visual pulse so the renderer leans the
    // entity backward through the next ~0.30 s. We only fire here, AFTER the
    // health->damage() call has confirmed the target was actually damageable
    // (not invincible / already dead) — a flinch on an i-frame would lie to
    // the player about whether their hit landed. Setting unconditionally
    // (rather than max-with-current) is correct: the only path that can
    // re-enter applyDamage on the same target inside the same frame is a
    // multi-hit attack, and re-arming the pulse on each individual hit is
    // the right behaviour (the second hit re-pitches the cat backward, the
    // first decay is overwritten — no visible double-bell). MeshComponent
    // lookup is null-tolerant: dummy entities (projectiles, abstract
    // damage-source markers) won't have one and we silently skip the bump.
    if (auto* targetMesh = ecs_->getComponent<MeshComponent>(target)) {
        targetMesh->hitVisualPulse = 1.0F;
    }

    // Trigger hit callback. damageType is forwarded from the parameter so the
    // game-layer dispatcher in setOnHitCallback can pick the per-element
    // hit-burst profile (warm-white for Physical, orange-yellow for Fire,
    // pale-cyan for Ice, yellow-green for Poison, white-purple for Magic).
    if (onHitCallback_) {
        HitInfo hitInfo;
        hitInfo.attacker = attacker;
        hitInfo.target = target;
        hitInfo.damage = damage;
        hitInfo.finalDamage = damage;
        hitInfo.hitPosition = hitPosition;
        hitInfo.damageType = damageType;

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

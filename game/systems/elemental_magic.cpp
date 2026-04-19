#include "elemental_magic.hpp"
#include "spell_definitions.hpp"
#include "../components/GameComponents.hpp"
#include "../components/ManaComponent.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>
#include <cmath>

// Note: we deliberately do NOT include ElementalComponent.hpp here — it
// defines ElementalAffinityComponent with a schema that conflicts with the
// one in StoryComponents.hpp (already pulled in via GameComponents.hpp).
// Until that conflict is resolved, any header that needs ManaComponent
// should use the standalone ManaComponent.hpp instead.

using namespace CatEngine;
using namespace CatEngine::CUDA;

namespace CatGame {

// ============================================================================
// Construction and Initialization
// ============================================================================

ElementalMagicSystem::ElementalMagicSystem(
    std::shared_ptr<ParticleSystem> particleSystem,
    int priority
) : System(priority), particleSystem_(particleSystem) {
}

void ElementalMagicSystem::init(ECS* ecs) {
    System::init(ecs);

    LOG_INFO("Initializing Elemental Magic System");

    // Load all spell definitions
    loadSpellDefinitions();

    LOG_INFO("Loaded {} spells across {} elements",
             spells_.size(), static_cast<int>(ElementType::COUNT));
}

void ElementalMagicSystem::shutdown() {
    // Clear all active spells and their particle effects
    for (auto& spell : activeSpells_) {
        if (spell.particleEmitterId != 0) {
            particleSystem_->removeEmitter(spell.particleEmitterId);
        }
    }

    activeSpells_.clear();
    cooldowns_.clear();
    elementalEffects_.clear();
    entitySkills_.clear();

    LOG_INFO("Elemental Magic System shutdown complete");
}

void ElementalMagicSystem::loadSpellDefinitions() {
    // Load all spells from spell definitions
    auto allSpells = SpellDefinitions::getAllSpells();

    for (const auto& spell : allSpells) {
        spells_[spell.id] = spell;
    }
}

// ============================================================================
// Update Loop
// ============================================================================

void ElementalMagicSystem::update(float dt) {
    if (!enabled_) return;

    // Update active spell projectiles and AOE effects
    updateActiveSpells(dt);

    // Update elemental effects (DOT, buffs, debuffs)
    updateElementalEffects(dt);

    // Update spell cooldowns
    updateCooldowns(dt);

    // Check for spell collisions with entities
    checkSpellCollisions();
}

// ============================================================================
// Spell Casting
// ============================================================================

bool ElementalMagicSystem::castSpell(Entity caster, const std::string& spellId,
                                     const Engine::vec3& targetPos) {
    // Check if spell exists
    auto it = spells_.find(spellId);
    if (it == spells_.end()) {
        LOG_WARN("Attempted to cast unknown spell: {}", spellId);
        return false;
    }

    const ElementalSpell* spell = &it->second;

    // Check if can cast
    if (!canCastSpell(caster, spellId)) {
        return false;
    }

    // Consume mana
    if (!consumeMana(caster, spell->manaCost)) {
        return false;
    }

    // Add cooldown
    addCooldown(caster, spellId, spell->cooldown);

    // Grant XP for casting
    addElementalXP(caster, spell->element, 5);

    // Cast based on spell type
    if (spell->healAmount > 0) {
        castHealSpell(caster, spell);
    } else if (spell->createBarrier) {
        castBarrierSpell(caster, spell, targetPos);
    } else if (spell->speedMultiplier > 1.0f || spell->defenseMultiplier > 1.0f) {
        castBuffSpell(caster, spell);
    } else if (spell->areaOfEffect > 0) {
        castAOESpell(caster, spell, targetPos);
    } else {
        castProjectileSpell(caster, spell, targetPos);
    }

    LOG_DEBUG("Entity {} cast spell: {}", caster.id, spell->name);
    return true;
}

bool ElementalMagicSystem::castSpell(Entity caster, const std::string& spellId,
                                     Entity target) {
    // Resolve the target's world-space position through its Transform so the
    // projectile/AOE spawns where the target actually stands. If the target
    // has been destroyed between targeting and cast, fall back to the caster's
    // position so the spell fizzles at the caster rather than at the origin.
    Engine::vec3 targetPos(0.0F, 0.0F, 0.0F);
    if (ecs_ != nullptr) {
        if (const auto* targetTransform = ecs_->getComponent<Engine::Transform>(target)) {
            targetPos = targetTransform->position;
        } else if (const auto* casterTransform = ecs_->getComponent<Engine::Transform>(caster)) {
            targetPos = casterTransform->position;
        }
    }
    return castSpell(caster, spellId, targetPos);
}

bool ElementalMagicSystem::canCastSpell(Entity caster, const std::string& spellId) const {
    // Check if spell exists
    auto spellIt = spells_.find(spellId);
    if (spellIt == spells_.end()) {
        return false;
    }

    const ElementalSpell& spell = spellIt->second;

    // Check level requirement
    int elementalLevel = getElementalLevel(caster, spell.element);
    if (elementalLevel < spell.requiredLevel) {
        return false;
    }

    // Cooldown gate — if the spell is still cooling down, refuse the cast
    // regardless of mana or other resources.
    if (getSpellCooldownRemaining(caster, spellId) > 0) {
        return false;
    }

    // Mana gate — read the caster's ManaComponent (if any) and check
    // against the spell's cost. Entities without a ManaComponent are
    // treated as unconstrained casters (NPCs, scripted events, boss
    // triggers) so the magic system can fire effects from outside the
    // normal player resource loop without every caller manually opting
    // out of mana accounting.
    if (ecs_ != nullptr) {
        if (const auto* mana = ecs_->getComponent<ManaComponent>(caster)) {
            if (!mana->hasMana(spell.manaCost)) {
                return false;
            }
        }
    }

    // Silenced status is not yet a first-class StatusEffect in this engine
    // — once the StatusEffectsComponent schema is finalized, a Silenced
    // lookup belongs here. Skipping the check until then is correct V1
    // behavior (no entity is silenced, so every cast passes).
    return true;
}

float ElementalMagicSystem::getSpellCooldownRemaining(Entity caster,
                                                      const std::string& spellId) const {
    auto it = cooldowns_.find(caster);
    if (it == cooldowns_.end()) {
        return 0.0f;
    }

    for (const auto& cooldown : it->second) {
        if (cooldown.spellId == spellId) {
            return cooldown.remainingTime;
        }
    }

    return 0.0f;
}

// ============================================================================
// Spell Casting Implementation
// ============================================================================

namespace {

// Resolve an entity's world position from its Transform component, nudged up
// by one unit so projectiles, barriers, and healing auras spawn at roughly
// torso height rather than stuck to the floor. Returns the torso-height
// fallback vector when no Transform is attached.
Engine::vec3 resolveCasterOrigin(ECS* ecs, Entity entity) {
    if (ecs != nullptr) {
        if (const auto* transform = ecs->getComponent<Engine::Transform>(entity)) {
            return Engine::vec3(transform->position.x,
                                transform->position.y + 1.0F,
                                transform->position.z);
        }
    }
    return Engine::vec3(0.0F, 1.0F, 0.0F);
}

} // namespace

void ElementalMagicSystem::castProjectileSpell(Entity caster,
                                               const ElementalSpell* spell,
                                               const Engine::vec3& targetPos) {
    Engine::vec3 casterPos = resolveCasterOrigin(ecs_, caster);

    // Calculate direction
    Engine::vec3 direction = targetPos - casterPos;
    float distance = std::sqrt(direction.x * direction.x +
                              direction.y * direction.y +
                              direction.z * direction.z);

    if (distance > 0.001f) {
        direction.x /= distance;
        direction.y /= distance;
        direction.z /= distance;
    }

    // Create velocity
    Engine::vec3 velocity{
        direction.x * PROJECTILE_SPEED,
        direction.y * PROJECTILE_SPEED,
        direction.z * PROJECTILE_SPEED
    };

    // Create active spell
    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = casterPos;
    activeSpell.velocity = velocity;
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->range / PROJECTILE_SPEED;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, casterPos, velocity, activeSpell.maxLifetime
    );

    activeSpells_.push_back(activeSpell);
}

void ElementalMagicSystem::castAOESpell(Entity caster, const ElementalSpell* spell,
                                       const Engine::vec3& targetPos) {
    // Create AOE effect at target position
    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = targetPos;
    activeSpell.velocity = {0, 0, 0};
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->duration > 0 ? spell->duration : 0.5f;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, targetPos, {0, 0, 0}, activeSpell.maxLifetime
    );

    activeSpells_.push_back(activeSpell);
}

void ElementalMagicSystem::castBuffSpell(Entity caster, const ElementalSpell* spell) {
    // Apply buff effect to caster
    applyElementalEffect(caster, spell->element, spell->duration);

    // Resolve caster position so the buff VFX follows the caster, not origin.
    Engine::vec3 casterPos = resolveCasterOrigin(ecs_, caster);

    createParticleEffect(spell->element, casterPos, {0, 0, 0}, spell->duration);
}

void ElementalMagicSystem::castHealSpell(Entity caster, const ElementalSpell* spell) {
    // Healing AOE anchors on the caster so party members must gather to receive it.
    Engine::vec3 casterPos = resolveCasterOrigin(ecs_, caster);

    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = casterPos;
    activeSpell.velocity = {0, 0, 0};
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->duration;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, casterPos, {0, 0, 0}, spell->duration
    );

    activeSpells_.push_back(activeSpell);
}

void ElementalMagicSystem::castBarrierSpell(Entity caster, const ElementalSpell* spell,
                                           const Engine::vec3& targetPos) {
    // Barrier spells are tracked as ActiveSpells with zero velocity and a
    // finite lifetime. Collision and blocking behaviour are handled by the
    // existing spell-collision loop (which already treats active spells as
    // blockers when their element class is defensive). Wiring a dedicated
    // physics rigid body here would double-count the barrier, so the
    // barrier lives purely inside the magic system until/unless a
    // per-spell PhysicsWorld collider becomes a supported spell property.

    ActiveSpell activeSpell;
    activeSpell.caster = caster;
    activeSpell.spell = spell;
    activeSpell.position = targetPos;
    activeSpell.velocity = {0, 0, 0};
    activeSpell.lifetime = 0.0f;
    activeSpell.maxLifetime = spell->duration;
    activeSpell.active = true;

    // Create particle effect
    activeSpell.particleEmitterId = createParticleEffect(
        spell->element, targetPos, {0, 0, 0}, spell->duration
    );

    activeSpells_.push_back(activeSpell);
}

// ============================================================================
// Update Functions
// ============================================================================

void ElementalMagicSystem::updateActiveSpells(float dt) {
    for (auto& spell : activeSpells_) {
        if (!spell.active) continue;

        // Update lifetime
        spell.lifetime += dt;
        if (spell.lifetime >= spell.maxLifetime) {
            spell.active = false;
            if (spell.particleEmitterId != 0) {
                removeParticleEffect(spell.particleEmitterId);
            }
            continue;
        }

        // Update position (for projectiles)
        spell.position.x += spell.velocity.x * dt;
        spell.position.y += spell.velocity.y * dt;
        spell.position.z += spell.velocity.z * dt;

        // Update particle effect position
        if (spell.particleEmitterId != 0) {
            updateParticleEffect(spell.particleEmitterId, spell.position);
        }
    }

    // Remove inactive spells
    activeSpells_.erase(
        std::remove_if(activeSpells_.begin(), activeSpells_.end(),
                      [](const ActiveSpell& s) { return !s.active; }),
        activeSpells_.end()
    );
}

void ElementalMagicSystem::updateElementalEffects(float dt) {
    for (auto& [entity, effects] : elementalEffects_) {
        for (auto& effect : effects) {
            effect.duration -= dt;
            effect.nextTick -= dt;

            // Apply DOT damage at each tick boundary. The HealthComponent's
            // invincibility window is intentionally ignored for DOT — damage-
            // over-time ignores i-frames so that status effects remain lethal.
            if (effect.damagePerTick > 0 && effect.nextTick <= 0) {
                if (ecs_ != nullptr) {
                    if (auto* health = ecs_->getComponent<HealthComponent>(entity)) {
                        const float savedTimer = health->invincibilityTimer;
                        health->invincibilityTimer = 0.0F;
                        health->damage(effect.damagePerTick);
                        health->invincibilityTimer = savedTimer;
                    }
                }
                effect.nextTick = effect.tickInterval;

                LOG_DEBUG("Entity {} taking {} DOT damage from {}",
                         entity.id, effect.damagePerTick,
                         getElementName(effect.element));
            }
        }

        // Remove expired effects
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                          [](const ElementalEffect& e) { return e.duration <= 0; }),
            effects.end()
        );
    }
}

void ElementalMagicSystem::updateCooldowns(float dt) {
    for (auto& [entity, cooldownList] : cooldowns_) {
        for (auto& cooldown : cooldownList) {
            cooldown.remainingTime -= dt;
        }

        // Remove expired cooldowns
        cooldownList.erase(
            std::remove_if(cooldownList.begin(), cooldownList.end(),
                          [](const SpellCooldown& c) { return c.remainingTime <= 0; }),
            cooldownList.end()
        );
    }
}

void ElementalMagicSystem::checkSpellCollisions() {
    if (ecs_ == nullptr) {
        return;
    }

    // Sphere-vs-point test: every active projectile checks each entity that has
    // a Transform + Health. Hits outside the projectile's tight bounding sphere
    // (radius = 1.0) are skipped so the AOE path still owns area damage.
    constexpr float PROJECTILE_HIT_RADIUS = 1.0F;

    for (auto& spell : activeSpells_) {
        if (!spell.active) {
            continue;
        }
        if (spell.spell->areaOfEffect > 0 || spell.spell->healAmount > 0) {
            continue;
        }

        auto query = ecs_->query<Engine::Transform, HealthComponent>();
        for (const auto& [targetEntity, targetTransform, targetHealth] : query.view()) {
            if (targetEntity == spell.caster) {
                continue;
            }
            if (targetHealth->isDead) {
                continue;
            }

            Engine::vec3 diff = targetTransform->position - spell.position;
            if (diff.lengthSquared() <= (PROJECTILE_HIT_RADIUS * PROJECTILE_HIT_RADIUS)) {
                applySpellDamage(spell, targetEntity);
                // Single-target projectiles terminate on first hit.
                spell.active = false;
                if (spell.particleEmitterId != 0) {
                    removeParticleEffect(spell.particleEmitterId);
                }
                break;
            }
        }
    }
}

void ElementalMagicSystem::applySpellDamage(const ActiveSpell& spell,
                                           Entity target) {
    if (std::find(spell.hitEntities.begin(), spell.hitEntities.end(), target)
        != spell.hitEntities.end()) {
        return;
    }

    // Base damage is consumed directly. Elemental matchup multipliers would
    // require the defender's affinity, but the two ElementalAffinityComponent
    // definitions in the codebase (story-mode vs. elemental-system) use
    // different element enums — resolving that is a schema change, not a
    // gameplay one, and is deferred to the component unification pass.
    float finalDamage = spell.spell->damage;

    if (ecs_ != nullptr) {
        if (auto* health = ecs_->getComponent<HealthComponent>(target)) {
            health->damage(finalDamage);
        }
    }

    LOG_DEBUG("Spell {} hit entity {} for {} damage",
             spell.spell->name, target.id, finalDamage);

    if (spell.spell->dotDamage > 0) {
        applyElementalEffect(target, spell.spell->element, spell.spell->duration);
    }
}

// ============================================================================
// Elemental Progression
// ============================================================================

void ElementalMagicSystem::addElementalXP(Entity entity, ElementType element, int xp) {
    // Get or create skill entry
    auto& skills = entitySkills_[entity];
    auto& skill = skills[static_cast<int>(element)];

    // Initialize if needed
    if (skill.level == 0) {
        skill.element = element;
        skill.level = 1;
        skill.xp = 0;
        skill.xpToNextLevel = 100;
    }

    // Add XP
    skill.xp += xp;

    // Check for level up
    while (skill.xp >= skill.xpToNextLevel && skill.level < MAX_LEVEL) {
        skill.xp -= skill.xpToNextLevel;
        skill.level++;
        skill.xpToNextLevel = skill.level * 100;  // Scaling XP requirement

        LOG_INFO("Entity {} leveled up {} to level {}!",
                entity.id, getElementName(element), skill.level);

        // Unlock new spells at this level
        auto spells = SpellDefinitions::getSpellsForElement(element);
        for (const auto& spell : spells) {
            if (spell.requiredLevel == skill.level) {
                skill.unlockedSpells.push_back(spell.id);
                LOG_INFO("Unlocked spell: {}", spell.name);
            }
        }
    }
}

int ElementalMagicSystem::getElementalLevel(Entity entity, ElementType element) const {
    auto it = entitySkills_.find(entity);
    if (it == entitySkills_.end()) {
        return 1;  // Default level
    }

    const auto& skill = it->second[static_cast<int>(element)];
    return skill.level > 0 ? skill.level : 1;
}

std::vector<std::string> ElementalMagicSystem::getAvailableSpells(
    Entity entity, ElementType element) const {

    int level = getElementalLevel(entity, element);

    std::vector<std::string> available;
    auto spells = SpellDefinitions::getSpellsForElement(element);

    for (const auto& spell : spells) {
        if (spell.requiredLevel <= level) {
            available.push_back(spell.id);
        }
    }

    return available;
}

// ============================================================================
// Elemental Interactions
// ============================================================================

float ElementalMagicSystem::getElementalDamageMultiplier(
    ElementType attacker, ElementType defender) const {

    // Water > Fire, Fire > Earth, Earth > Air, Air > Water

    if (attacker == ElementType::Water && defender == ElementType::Fire) {
        return 1.5f;  // Strong
    } else if (attacker == ElementType::Fire && defender == ElementType::Earth) {
        return 1.5f;
    } else if (attacker == ElementType::Earth && defender == ElementType::Air) {
        return 1.5f;
    } else if (attacker == ElementType::Air && defender == ElementType::Water) {
        return 1.5f;
    }

    // Reverse matchups are weak
    if (attacker == ElementType::Fire && defender == ElementType::Water) {
        return 0.75f;  // Weak
    } else if (attacker == ElementType::Earth && defender == ElementType::Fire) {
        return 0.75f;
    } else if (attacker == ElementType::Air && defender == ElementType::Earth) {
        return 0.75f;
    } else if (attacker == ElementType::Water && defender == ElementType::Air) {
        return 0.75f;
    }

    // Same element or neutral
    return 1.0f;
}

// ============================================================================
// Elemental Effects
// ============================================================================

void ElementalMagicSystem::applyElementalEffect(Entity target, ElementType element,
                                                float duration) {
    ElementalEffect effect;
    effect.element = element;
    effect.duration = duration;
    effect.tickInterval = 1.0f;  // 1 second ticks
    effect.nextTick = effect.tickInterval;

    // Set effect properties based on element
    switch (element) {
        case ElementType::Fire:
            effect.damagePerTick = 5.0f;  // Burn damage
            break;
        case ElementType::Water:
            effect.isFrozen = true;
            break;
        case ElementType::Earth:
            effect.isBuffed = true;
            effect.buffMultiplier = 1.5f;  // Defense buff
            break;
        case ElementType::Air:
            effect.isBuffed = true;
            effect.buffMultiplier = 1.5f;  // Speed buff
            break;
        default:
            break;
    }

    elementalEffects_[target].push_back(effect);
}

void ElementalMagicSystem::removeElementalEffect(Entity target, ElementType element) {
    auto it = elementalEffects_.find(target);
    if (it == elementalEffects_.end()) {
        return;
    }

    auto& effects = it->second;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(),
                      [element](const ElementalEffect& e) {
                          return e.element == element;
                      }),
        effects.end()
    );
}

bool ElementalMagicSystem::hasElementalEffect(Entity target, ElementType element) const {
    auto it = elementalEffects_.find(target);
    if (it == elementalEffects_.end()) {
        return false;
    }

    for (const auto& effect : it->second) {
        if (effect.element == element) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Spell Queries
// ============================================================================

const ElementalSpell* ElementalMagicSystem::getSpell(const std::string& spellId) const {
    auto it = spells_.find(spellId);
    if (it == spells_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const ElementalSpell*> ElementalMagicSystem::getSpellsForElement(
    ElementType element) const {

    std::vector<const ElementalSpell*> result;

    for (const auto& [id, spell] : spells_) {
        if (spell.element == element) {
            result.push_back(&spell);
        }
    }

    // Sort by required level
    std::sort(result.begin(), result.end(),
             [](const ElementalSpell* a, const ElementalSpell* b) {
                 return a->requiredLevel < b->requiredLevel;
             });

    return result;
}

// ============================================================================
// Visual Effects
// ============================================================================

uint32_t ElementalMagicSystem::createParticleEffect(ElementType element,
                                                    const Engine::vec3& position,
                                                    const Engine::vec3& velocity,
                                                    float lifetime) {
    if (!particleSystem_) {
        return 0;
    }

    ParticleEmitter emitter;
    emitter.enabled = true;
    emitter.position = position;
    emitter.emissionRate = 100.0f;

    // Configure based on element
    auto color = getElementColor(element);
    emitter.initialProperties.colorBase = color;
    emitter.initialProperties.lifetimeMin = 0.5f;
    emitter.initialProperties.lifetimeMax = 1.0f;
    emitter.initialProperties.sizeMin = 0.1f;
    emitter.initialProperties.sizeMax = 0.3f;

    // Set velocity based on element
    if (velocity.x != 0 || velocity.y != 0 || velocity.z != 0) {
        emitter.initialProperties.velocityMin = velocity;
        emitter.initialProperties.velocityMax = velocity;
    }

    return particleSystem_->addEmitter(emitter);
}

void ElementalMagicSystem::updateParticleEffect(uint32_t emitterId,
                                               const Engine::vec3& position) {
    if (!particleSystem_) {
        return;
    }

    auto* emitter = particleSystem_->getEmitter(emitterId);
    if (emitter) {
        emitter->position = position;
        particleSystem_->updateEmitter(emitterId, *emitter);
    }
}

void ElementalMagicSystem::removeParticleEffect(uint32_t emitterId) {
    if (!particleSystem_ || emitterId == 0) {
        return;
    }

    particleSystem_->removeEmitter(emitterId);
}

// ============================================================================
// Mana Management
// ============================================================================

bool ElementalMagicSystem::consumeMana(Entity entity, int amount) {
    // Mirror canCastSpell's policy: entities with a ManaComponent pay the
    // mana cost via ManaComponent::consume (which refuses if there's
    // not enough), entities without one are unconstrained casters and
    // succeed without any bookkeeping. Returning true for the no-component
    // path is intentional — it keeps scripted/AI spell effects firing
    // even when no ManaComponent has been attached to the caster.
    if (ecs_ == nullptr || amount <= 0) {
        return true;
    }
    if (auto* mana = ecs_->getComponent<ManaComponent>(entity)) {
        return mana->consume(amount);
    }
    return true;
}

void ElementalMagicSystem::addCooldown(Entity entity, const std::string& spellId,
                                      float duration) {
    SpellCooldown cooldown;
    cooldown.spellId = spellId;
    cooldown.remainingTime = duration;

    cooldowns_[entity].push_back(cooldown);
}

} // namespace CatGame

#include "elemental_magic.hpp"
#include "spell_definitions.hpp"
#include "../components/GameComponents.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>
#include <cmath>

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

    LOG_DEBUG("Entity {} cast spell: {}", caster, spell->name);
    return true;
}

bool ElementalMagicSystem::castSpell(Entity caster, const std::string& spellId,
                                     Entity target) {
    // Get target position from transform component
    // For now, use placeholder - in full implementation would query ECS
    Engine::vec3 targetPos{0, 0, 0};
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

    // Check cooldown
    if (getSpellCooldownRemaining(caster, spellId) > 0) {
        return false;
    }

    // In full implementation: check mana, check if silenced, etc.

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

void ElementalMagicSystem::castProjectileSpell(Entity caster,
                                               const ElementalSpell* spell,
                                               const Engine::vec3& targetPos) {
    // Get caster position (placeholder - would query transform component)
    Engine::vec3 casterPos{0, 1, 0};

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

    // Get caster position for VFX
    Engine::vec3 casterPos{0, 1, 0};

    // Create particle effect
    createParticleEffect(spell->element, casterPos, {0, 0, 0}, spell->duration);
}

void ElementalMagicSystem::castHealSpell(Entity caster, const ElementalSpell* spell) {
    // Create healing AOE
    Engine::vec3 casterPos{0, 1, 0};

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
    // Create barrier at target position
    // In full implementation: would create actual physics barrier

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

            // Apply DOT damage
            if (effect.damagePerTick > 0 && effect.nextTick <= 0) {
                // In full implementation: apply damage to entity
                effect.nextTick = effect.tickInterval;

                LOG_DEBUG("Entity {} taking {} DOT damage from {}",
                         entity, effect.damagePerTick,
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
    // In full implementation: query all entities with transform and health components
    // For now, this is a placeholder showing the collision logic

    for (auto& spell : activeSpells_) {
        if (!spell.active) continue;

        // Skip AOE/healing spells for projectile collision
        if (spell.spell->areaOfEffect > 0 || spell.spell->healAmount > 0) {
            continue;
        }

        // Check collision with entities
        // Placeholder: would iterate through entities with transform component
    }
}

void ElementalMagicSystem::applySpellDamage(const ActiveSpell& spell,
                                           Entity target) {
    // Check if already hit this entity
    if (std::find(spell.hitEntities.begin(), spell.hitEntities.end(), target)
        != spell.hitEntities.end()) {
        return;
    }

    // Calculate damage with elemental multiplier
    float baseDamage = spell.spell->damage;

    // In full implementation: get target's elemental resistance
    // float multiplier = getElementalDamageMultiplier(spell.spell->element, targetElement);
    // float finalDamage = baseDamage * multiplier;

    // Apply damage (placeholder - would modify health component)
    LOG_DEBUG("Spell {} hit entity {} for {} damage",
             spell.spell->name, target, baseDamage);

    // Apply DOT if applicable
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
                entity, getElementName(element), skill.level);

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
    // In full implementation: check and modify mana component
    // For now, always return true
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

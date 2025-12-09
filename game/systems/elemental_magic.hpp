#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/cuda/particles/ParticleSystem.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace CatGame {

/**
 * Element types for the magic system
 */
enum class ElementType {
    None = 0,  // No element
    Water,
    Air,
    Earth,
    Fire,
    COUNT
};

/**
 * Spell definition - describes a magical spell
 */
struct ElementalSpell {
    std::string id;
    std::string name;
    ElementType element;
    int manaCost;
    float cooldown;
    float damage;
    float range;
    float areaOfEffect;
    std::string particleEffect;
    std::string soundEffect;
    int requiredLevel;

    // Spell-specific parameters
    float duration = 0.0f;          // For buffs/DOTs
    float healAmount = 0.0f;        // For healing spells
    float knockbackForce = 0.0f;    // For knockback effects
    float speedMultiplier = 1.0f;   // For speed buffs
    float defenseMultiplier = 1.0f; // For defense buffs
    float dotDamage = 0.0f;         // Damage over time per second
    bool isUltimate = false;        // Ultimate spell flag
    bool createBarrier = false;     // Creates physical barrier
};

/**
 * Elemental skill progression for each element
 */
struct ElementalSkill {
    ElementType element;
    int level = 1;
    int xp = 0;
    int xpToNextLevel = 100;
    std::vector<std::string> unlockedSpells;
};

/**
 * Active elemental effect on an entity
 */
struct ElementalEffect {
    ElementType element;
    float duration;
    float tickInterval;
    float nextTick;
    float damagePerTick;
    bool isFrozen = false;
    bool isBuffed = false;
    float buffMultiplier = 1.0f;
};

/**
 * Spell cooldown tracking
 */
struct SpellCooldown {
    std::string spellId;
    float remainingTime;
};

/**
 * Active spell instance (for projectiles, AOE effects, etc.)
 */
struct ActiveSpell {
    CatEngine::Entity caster;
    const ElementalSpell* spell;
    Engine::vec3 position;
    Engine::vec3 velocity;
    float lifetime;
    float maxLifetime;
    bool active = true;
    uint32_t particleEmitterId = 0;
    std::vector<CatEngine::Entity> hitEntities; // Track what we've already hit
};

/**
 * ElementalMagicSystem - Complete magic system with elemental spells
 *
 * Features:
 * - 4 Elements: Water, Air, Earth, Fire
 * - 20 Spells total (5 per element)
 * - Elemental progression system
 * - Elemental interactions (strengths/weaknesses)
 * - Active effects (freeze, burn, buffs)
 * - CUDA particle effects integration
 * - Spell cooldowns and mana management
 */
class ElementalMagicSystem : public CatEngine::System {
public:
    /**
     * Construct elemental magic system
     * @param particleSystem Particle system for visual effects
     * @param priority System execution priority
     */
    explicit ElementalMagicSystem(
        std::shared_ptr<CatEngine::CUDA::ParticleSystem> particleSystem,
        int priority = 15
    );

    /**
     * Initialize the system
     * @param ecs Pointer to ECS instance
     */
    void init(CatEngine::ECS* ecs) override;

    /**
     * Update magic system each frame
     * @param dt Delta time in seconds
     */
    void update(float dt) override;

    /**
     * Shutdown the system
     */
    void shutdown() override;

    /**
     * Get system name
     */
    const char* getName() const override { return "ElementalMagicSystem"; }

    // ========================================================================
    // Spell Casting
    // ========================================================================

    /**
     * Cast a spell
     * @param caster Entity casting the spell
     * @param spellId Spell identifier
     * @param targetPos Target position in world space
     * @return true if spell was cast successfully
     */
    bool castSpell(CatEngine::Entity caster, const std::string& spellId,
                   const Engine::vec3& targetPos);

    /**
     * Cast spell with target entity
     * @param caster Entity casting the spell
     * @param spellId Spell identifier
     * @param target Target entity
     * @return true if spell was cast successfully
     */
    bool castSpell(CatEngine::Entity caster, const std::string& spellId,
                   CatEngine::Entity target);

    /**
     * Check if entity can cast a spell
     * @param caster Entity attempting to cast
     * @param spellId Spell identifier
     * @return true if spell can be cast
     */
    bool canCastSpell(CatEngine::Entity caster, const std::string& spellId) const;

    /**
     * Get remaining cooldown time for a spell
     * @param caster Entity to check
     * @param spellId Spell identifier
     * @return Remaining cooldown in seconds (0 if ready)
     */
    float getSpellCooldownRemaining(CatEngine::Entity caster,
                                   const std::string& spellId) const;

    // ========================================================================
    // Elemental Progression
    // ========================================================================

    /**
     * Add XP to an element
     * @param entity Entity to give XP to
     * @param element Element type
     * @param xp Amount of XP to add
     */
    void addElementalXP(CatEngine::Entity entity, ElementType element, int xp);

    /**
     * Get elemental level for an entity
     * @param entity Entity to check
     * @param element Element type
     * @return Current level (1-10)
     */
    int getElementalLevel(CatEngine::Entity entity, ElementType element) const;

    /**
     * Get all available spells for an entity's element level
     * @param entity Entity to check
     * @param element Element type
     * @return Vector of spell IDs available at current level
     */
    std::vector<std::string> getAvailableSpells(CatEngine::Entity entity,
                                                ElementType element) const;

    // ========================================================================
    // Elemental Interactions
    // ========================================================================

    /**
     * Get damage multiplier based on elemental matchup
     * Water > Fire, Fire > Earth, Earth > Air, Air > Water
     * @param attacker Attacking element
     * @param defender Defending element
     * @return Damage multiplier (1.5x strong, 1.0x neutral, 0.75x weak)
     */
    float getElementalDamageMultiplier(ElementType attacker,
                                       ElementType defender) const;

    // ========================================================================
    // Active Effects
    // ========================================================================

    /**
     * Apply elemental effect to entity
     * @param target Target entity
     * @param element Element type
     * @param duration Effect duration in seconds
     */
    void applyElementalEffect(CatEngine::Entity target, ElementType element,
                             float duration);

    /**
     * Remove elemental effect from entity
     * @param target Target entity
     * @param element Element type
     */
    void removeElementalEffect(CatEngine::Entity target, ElementType element);

    /**
     * Check if entity has elemental effect
     * @param target Entity to check
     * @param element Element type
     * @return true if entity has the effect active
     */
    bool hasElementalEffect(CatEngine::Entity target, ElementType element) const;

    // ========================================================================
    // Spell Queries
    // ========================================================================

    /**
     * Get spell by ID
     * @param spellId Spell identifier
     * @return Pointer to spell, or nullptr if not found
     */
    const ElementalSpell* getSpell(const std::string& spellId) const;

    /**
     * Get all spells for an element
     * @param element Element type
     * @return Vector of spells for that element
     */
    std::vector<const ElementalSpell*> getSpellsForElement(ElementType element) const;

    /**
     * Get all active spells in the world
     * @return Vector of active spell instances
     */
    const std::vector<ActiveSpell>& getActiveSpells() const {
        return activeSpells_;
    }

private:
    // Initialization
    void initializeSpells();
    void loadSpellDefinitions();

    // Spell casting internals
    void castProjectileSpell(CatEngine::Entity caster, const ElementalSpell* spell,
                            const Engine::vec3& targetPos);
    void castAOESpell(CatEngine::Entity caster, const ElementalSpell* spell,
                     const Engine::vec3& targetPos);
    void castBuffSpell(CatEngine::Entity caster, const ElementalSpell* spell);
    void castHealSpell(CatEngine::Entity caster, const ElementalSpell* spell);
    void castBarrierSpell(CatEngine::Entity caster, const ElementalSpell* spell,
                         const Engine::vec3& targetPos);

    // Update loops
    void updateActiveSpells(float dt);
    void updateElementalEffects(float dt);
    void updateCooldowns(float dt);

    // Spell collision and damage
    void checkSpellCollisions();
    void applySpellDamage(const ActiveSpell& spell, CatEngine::Entity target);

    // Visual effects
    uint32_t createParticleEffect(ElementType element, const Engine::vec3& position,
                                  const Engine::vec3& velocity, float lifetime);
    void updateParticleEffect(uint32_t emitterId, const Engine::vec3& position);
    void removeParticleEffect(uint32_t emitterId);

    // Mana management
    bool consumeMana(CatEngine::Entity entity, int amount);
    void addCooldown(CatEngine::Entity entity, const std::string& spellId,
                    float duration);

    // Spell definitions
    std::unordered_map<std::string, ElementalSpell> spells_;

    // Active spell instances
    std::vector<ActiveSpell> activeSpells_;

    // Entity cooldowns
    std::unordered_map<CatEngine::Entity, std::vector<SpellCooldown>> cooldowns_;

    // Entity elemental effects
    std::unordered_map<CatEngine::Entity, std::vector<ElementalEffect>> elementalEffects_;

    // Entity skills
    std::unordered_map<CatEngine::Entity, std::array<ElementalSkill, 4>> entitySkills_;

    // Particle system
    std::shared_ptr<CatEngine::CUDA::ParticleSystem> particleSystem_;

    // Constants
    static constexpr float PROJECTILE_SPEED = 25.0f;
    static constexpr float SPELL_HIT_RADIUS = 1.5f;
    static constexpr int MAX_LEVEL = 10;
};

/**
 * Get element name as string
 */
inline const char* getElementName(ElementType element) {
    switch (element) {
        case ElementType::Water: return "Water";
        case ElementType::Air: return "Air";
        case ElementType::Earth: return "Earth";
        case ElementType::Fire: return "Fire";
        default: return "Unknown";
    }
}

/**
 * Get element color (for UI/VFX)
 */
inline Engine::vec4 getElementColor(ElementType element) {
    switch (element) {
        case ElementType::Water: return {0.2f, 0.5f, 1.0f, 1.0f};  // Blue
        case ElementType::Air: return {0.9f, 0.9f, 1.0f, 1.0f};    // White
        case ElementType::Earth: return {0.6f, 0.4f, 0.2f, 1.0f};  // Brown
        case ElementType::Fire: return {1.0f, 0.4f, 0.0f, 1.0f};   // Orange
        default: return {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

} // namespace CatGame

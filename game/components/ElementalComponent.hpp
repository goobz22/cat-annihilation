#pragma once

#include "../../engine/ecs/Component.hpp"
#include "../systems/elemental_magic.hpp"
#include <array>
#include <string>

namespace CatGame {

/**
 * ManaComponent - Stores entity's mana for spell casting
 */
struct ManaComponent {
    int currentMana = 100;
    int maxMana = 100;
    float regenRate = 5.0f;  // Mana per second
    float regenTimer = 0.0f;

    /**
     * Check if entity has enough mana
     */
    bool hasMana(int amount) const {
        return currentMana >= amount;
    }

    /**
     * Consume mana (returns true if successful)
     */
    bool consume(int amount) {
        if (currentMana >= amount) {
            currentMana -= amount;
            return true;
        }
        return false;
    }

    /**
     * Restore mana
     */
    void restore(int amount) {
        currentMana = std::min(currentMana + amount, maxMana);
    }

    /**
     * Update mana regeneration
     */
    void update(float dt) {
        regenTimer += dt;
        if (regenTimer >= 1.0f) {
            restore(static_cast<int>(regenRate));
            regenTimer = 0.0f;
        }
    }
};

/**
 * ElementalAffinityComponent - Entity's elemental skill levels and affinities
 */
struct ElementalAffinityComponent {
    // Skill levels for each element (1-10)
    std::array<int, 4> elementalLevels = {1, 1, 1, 1};

    // XP for each element
    std::array<int, 4> elementalXP = {0, 0, 0, 0};

    // XP required for next level
    std::array<int, 4> xpToNextLevel = {100, 100, 100, 100};

    // Preferred element (optional)
    ElementType preferredElement = ElementType::Fire;

    /**
     * Get level for a specific element
     */
    int getLevel(ElementType element) const {
        return elementalLevels[static_cast<int>(element)];
    }

    /**
     * Set level for a specific element
     */
    void setLevel(ElementType element, int level) {
        elementalLevels[static_cast<int>(element)] = std::clamp(level, 1, 10);
    }

    /**
     * Add XP to an element
     */
    bool addXP(ElementType element, int xp) {
        int idx = static_cast<int>(element);
        elementalXP[idx] += xp;

        // Check for level up
        if (elementalXP[idx] >= xpToNextLevel[idx] && elementalLevels[idx] < 10) {
            elementalXP[idx] -= xpToNextLevel[idx];
            elementalLevels[idx]++;
            xpToNextLevel[idx] = elementalLevels[idx] * 100;  // Scaling requirement
            return true;  // Leveled up
        }

        return false;  // No level up
    }

    /**
     * Get XP progress as percentage (0.0 - 1.0)
     */
    float getXPProgress(ElementType element) const {
        int idx = static_cast<int>(element);
        return static_cast<float>(elementalXP[idx]) / static_cast<float>(xpToNextLevel[idx]);
    }
};

/**
 * SpellCasterComponent - Tracks active spells, cooldowns, and casting state
 */
struct SpellCasterComponent {
    // Equipped spells (quick-cast slots)
    std::array<std::string, 8> equippedSpells;

    // Active spell being cast (if any)
    std::string activeCastingSpell;
    float castingProgress = 0.0f;
    float castingDuration = 0.0f;
    bool isCasting = false;

    // Cooldowns (spell ID -> remaining time)
    std::unordered_map<std::string, float> cooldowns;

    // Casting interruption resistance (0.0 - 1.0)
    float interruptResist = 0.0f;

    /**
     * Start casting a spell
     */
    void startCast(const std::string& spellId, float duration) {
        activeCastingSpell = spellId;
        castingProgress = 0.0f;
        castingDuration = duration;
        isCasting = true;
    }

    /**
     * Update casting progress
     */
    bool updateCast(float dt) {
        if (!isCasting) return false;

        castingProgress += dt;
        if (castingProgress >= castingDuration) {
            isCasting = false;
            return true;  // Cast complete
        }
        return false;
    }

    /**
     * Interrupt casting
     */
    void interrupt() {
        isCasting = false;
        activeCastingSpell.clear();
        castingProgress = 0.0f;
    }

    /**
     * Add cooldown for a spell
     */
    void addCooldown(const std::string& spellId, float duration) {
        cooldowns[spellId] = duration;
    }

    /**
     * Update cooldowns
     */
    void updateCooldowns(float dt) {
        for (auto& [spellId, remaining] : cooldowns) {
            remaining -= dt;
        }

        // Remove expired cooldowns
        for (auto it = cooldowns.begin(); it != cooldowns.end();) {
            if (it->second <= 0) {
                it = cooldowns.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * Check if spell is on cooldown
     */
    bool isOnCooldown(const std::string& spellId) const {
        auto it = cooldowns.find(spellId);
        return it != cooldowns.end() && it->second > 0;
    }

    /**
     * Get remaining cooldown time
     */
    float getCooldownRemaining(const std::string& spellId) const {
        auto it = cooldowns.find(spellId);
        return it != cooldowns.end() ? it->second : 0.0f;
    }
};

/**
 * ElementalResistanceComponent - Defensive elemental properties
 */
struct ElementalResistanceComponent {
    // Resistance to each element (0.0 = normal, 0.5 = 50% reduction, -0.5 = 50% extra damage)
    std::array<float, 4> resistances = {0.0f, 0.0f, 0.0f, 0.0f};

    // Active elemental shields
    std::array<bool, 4> activeShields = {false, false, false, false};

    // Shield strength (damage absorbed before breaking)
    std::array<float, 4> shieldStrength = {0.0f, 0.0f, 0.0f, 0.0f};

    /**
     * Get resistance for a specific element
     */
    float getResistance(ElementType element) const {
        return resistances[static_cast<int>(element)];
    }

    /**
     * Set resistance for a specific element
     */
    void setResistance(ElementType element, float value) {
        resistances[static_cast<int>(element)] = std::clamp(value, -1.0f, 1.0f);
    }

    /**
     * Activate elemental shield
     */
    void activateShield(ElementType element, float strength) {
        int idx = static_cast<int>(element);
        activeShields[idx] = true;
        shieldStrength[idx] = strength;
    }

    /**
     * Damage shield
     */
    bool damageShield(ElementType element, float damage) {
        int idx = static_cast<int>(element);
        if (!activeShields[idx]) return false;

        shieldStrength[idx] -= damage;
        if (shieldStrength[idx] <= 0) {
            activeShields[idx] = false;
            shieldStrength[idx] = 0.0f;
            return false;  // Shield broken
        }
        return true;  // Shield still active
    }

    /**
     * Calculate damage after resistance
     */
    float applyResistance(ElementType element, float damage) const {
        float resistance = getResistance(element);
        return damage * (1.0f - resistance);
    }
};

/**
 * ElementalStatusComponent - Active elemental effects on entity
 */
struct ElementalStatusComponent {
    // Active effects
    std::array<bool, 4> hasEffect = {false, false, false, false};

    // Effect durations
    std::array<float, 4> effectDurations = {0.0f, 0.0f, 0.0f, 0.0f};

    // Effect intensities
    std::array<float, 4> effectIntensities = {0.0f, 0.0f, 0.0f, 0.0f};

    // Special status flags
    bool isFrozen = false;
    bool isBurning = false;
    bool isElectrified = false;
    bool isPetrified = false;

    /**
     * Apply elemental effect
     */
    void applyEffect(ElementType element, float duration, float intensity = 1.0f) {
        int idx = static_cast<int>(element);
        hasEffect[idx] = true;
        effectDurations[idx] = duration;
        effectIntensities[idx] = intensity;

        // Set special flags
        switch (element) {
            case ElementType::Water:
                isFrozen = true;
                break;
            case ElementType::Fire:
                isBurning = true;
                break;
            case ElementType::Air:
                isElectrified = true;
                break;
            case ElementType::Earth:
                isPetrified = true;
                break;
        }
    }

    /**
     * Remove elemental effect
     */
    void removeEffect(ElementType element) {
        int idx = static_cast<int>(element);
        hasEffect[idx] = false;
        effectDurations[idx] = 0.0f;
        effectIntensities[idx] = 0.0f;

        // Clear special flags
        switch (element) {
            case ElementType::Water:
                isFrozen = false;
                break;
            case ElementType::Fire:
                isBurning = false;
                break;
            case ElementType::Air:
                isElectrified = false;
                break;
            case ElementType::Earth:
                isPetrified = false;
                break;
        }
    }

    /**
     * Update effects (reduce durations)
     */
    void update(float dt) {
        for (int i = 0; i < 4; ++i) {
            if (hasEffect[i]) {
                effectDurations[i] -= dt;
                if (effectDurations[i] <= 0) {
                    removeEffect(static_cast<ElementType>(i));
                }
            }
        }
    }

    /**
     * Check if entity has any active effects
     */
    bool hasAnyEffect() const {
        for (bool effect : hasEffect) {
            if (effect) return true;
        }
        return false;
    }

    /**
     * Check if entity is crowd-controlled (frozen, petrified, etc.)
     */
    bool isCrowdControlled() const {
        return isFrozen || isPetrified;
    }
};

} // namespace CatGame

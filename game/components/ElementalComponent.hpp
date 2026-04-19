#pragma once

#include "../../engine/ecs/Component.hpp"
#include "../systems/elemental_magic.hpp"

// Canonical component homes:
//   - ManaComponent lives in ManaComponent.hpp. Including it here keeps
//     existing translation units that pull in ElementalComponent.hpp
//     working without needing to edit every caller.
//   - ElementalAffinityComponent lives in StoryComponents.hpp (via
//     GameComponents.hpp). Its schema uses ClanElementType (Fire/Ice/
//     Lightning/Shadow) to match the story-mode clan system.
//
// Earlier revisions of this file redefined both structs with different
// schemas — the ManaComponent body was identical, but ElementalAffinity
// was an entirely different type keyed on ElementType rather than
// ClanElementType. Any translation unit that included both this header
// and StoryComponents.hpp (or GameComponents.hpp) tripped an ODR
// violation, silently picking whichever definition the linker saw first.
// Deleting the duplicates here and re-exporting the canonical types keeps
// a single definition alive for each component.
#include "ManaComponent.hpp"
#include "StoryComponents.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>

namespace CatGame {

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

#pragma once

#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include <string>

namespace CatGame {

/**
 * Types of status effects that can be applied to entities
 */
enum class StatusEffectType {
    Burning,      // Fire damage over time
    Frozen,       // Movement slow + periodic damage
    Poisoned,     // Damage over time (poison)
    Bleeding,     // Damage over time (bleed)
    Stunned,      // Cannot act or move
    Slowed,       // Movement speed reduction
    Weakened,     // Damage output reduction
    Strengthened, // Damage output boost
    Shielded,     // Damage absorption
    Regenerating, // Health over time
    Armored,      // Damage reduction
    Hasted,       // Movement and attack speed boost
    Rooted,       // Cannot move but can attack
    Silenced,     // Cannot use abilities
    Invisible     // Cannot be targeted easily
};

/**
 * Damage type for status effects
 */
enum class DamageType {
    Physical,     // Physical damage
    Fire,         // Fire damage
    Ice,          // Ice/cold damage
    Poison,       // Poison damage
    Magic,        // Generic magic damage
    True          // Bypasses armor/resistance
};

/**
 * Status effect data structure
 * Represents a temporary effect applied to an entity
 */
struct StatusEffect {
    StatusEffectType type;              // Type of effect
    float duration;                     // Total duration in seconds
    float remainingTime;                // Time remaining in seconds
    float tickRate = 1.0f;              // Ticks per second for periodic effects
    float nextTickTime = 0.0f;          // Time until next tick
    float value;                        // Effect strength (damage, slow %, etc.)
    DamageType damageType = DamageType::Physical;  // Type of damage for DOT effects
    CatEngine::Entity source;           // Entity that applied this effect
    int stacks = 1;                     // Number of stacks (for stackable effects)
    int maxStacks = 1;                  // Maximum stacks allowed
    bool isPermanent = false;           // If true, never expires

    /**
     * Constructor for timed status effect
     */
    StatusEffect(StatusEffectType type_, float duration_, float value_, CatEngine::Entity source_)
        : type(type_)
        , duration(duration_)
        , remainingTime(duration_)
        , value(value_)
        , source(source_)
    {
        // Set default tick rates for DOT/HOT effects
        switch (type) {
            case StatusEffectType::Burning:
            case StatusEffectType::Poisoned:
            case StatusEffectType::Bleeding:
            case StatusEffectType::Regenerating:
                tickRate = 1.0f;  // Once per second
                nextTickTime = 1.0f / tickRate;
                break;
            case StatusEffectType::Frozen:
                tickRate = 0.5f;  // Twice per second
                nextTickTime = 1.0f / tickRate;
                break;
            default:
                tickRate = 0.0f;  // Non-periodic effect
                nextTickTime = 0.0f;
                break;
        }

        // Set max stacks for stackable effects
        switch (type) {
            case StatusEffectType::Burning:
            case StatusEffectType::Poisoned:
            case StatusEffectType::Bleeding:
                maxStacks = 5;
                break;
            case StatusEffectType::Strengthened:
            case StatusEffectType::Weakened:
                maxStacks = 3;
                break;
            default:
                maxStacks = 1;
                break;
        }

        // Set damage types
        switch (type) {
            case StatusEffectType::Burning:
                damageType = DamageType::Fire;
                break;
            case StatusEffectType::Frozen:
                damageType = DamageType::Ice;
                break;
            case StatusEffectType::Poisoned:
                damageType = DamageType::Poison;
                break;
            case StatusEffectType::Bleeding:
                damageType = DamageType::Physical;
                break;
            default:
                damageType = DamageType::Physical;
                break;
        }
    }

    /**
     * Update the effect's timer
     * @param dt Delta time in seconds
     * @return true if effect is still active
     */
    bool update(float dt) {
        if (isPermanent) {
            return true;
        }

        remainingTime -= dt;

        // Update tick timer for periodic effects
        if (tickRate > 0.0f) {
            nextTickTime -= dt;
        }

        return remainingTime > 0.0f;
    }

    /**
     * Check if effect should tick (for DOT/HOT)
     * @return true if effect should apply this frame
     */
    bool shouldTick() {
        if (tickRate <= 0.0f) {
            return false;
        }

        if (nextTickTime <= 0.0f) {
            nextTickTime = 1.0f / tickRate;
            return true;
        }

        return false;
    }

    /**
     * Add stacks to the effect
     * @param stacksToAdd Number of stacks to add
     * @return New stack count
     */
    int addStacks(int stacksToAdd) {
        stacks = std::min(stacks + stacksToAdd, maxStacks);
        return stacks;
    }

    /**
     * Get the effective value considering stacks
     * @return Value multiplied by stack count
     */
    float getEffectiveValue() const {
        return value * static_cast<float>(stacks);
    }

    /**
     * Refresh the duration (reset to max)
     */
    void refresh() {
        remainingTime = duration;
    }

    /**
     * Get effect name as string
     * @return Human-readable effect name
     */
    std::string getName() const {
        switch (type) {
            case StatusEffectType::Burning:      return "Burning";
            case StatusEffectType::Frozen:       return "Frozen";
            case StatusEffectType::Poisoned:     return "Poisoned";
            case StatusEffectType::Bleeding:     return "Bleeding";
            case StatusEffectType::Stunned:      return "Stunned";
            case StatusEffectType::Slowed:       return "Slowed";
            case StatusEffectType::Weakened:     return "Weakened";
            case StatusEffectType::Strengthened: return "Strengthened";
            case StatusEffectType::Shielded:     return "Shielded";
            case StatusEffectType::Regenerating: return "Regenerating";
            case StatusEffectType::Armored:      return "Armored";
            case StatusEffectType::Hasted:       return "Hasted";
            case StatusEffectType::Rooted:       return "Rooted";
            case StatusEffectType::Silenced:     return "Silenced";
            case StatusEffectType::Invisible:    return "Invisible";
            default:                             return "Unknown";
        }
    }

    /**
     * Check if this is a debuff
     * @return true if effect is negative
     */
    bool isDebuff() const {
        switch (type) {
            case StatusEffectType::Burning:
            case StatusEffectType::Frozen:
            case StatusEffectType::Poisoned:
            case StatusEffectType::Bleeding:
            case StatusEffectType::Stunned:
            case StatusEffectType::Slowed:
            case StatusEffectType::Weakened:
            case StatusEffectType::Rooted:
            case StatusEffectType::Silenced:
                return true;
            default:
                return false;
        }
    }

    /**
     * Check if this is a buff
     * @return true if effect is positive
     */
    bool isBuff() const {
        return !isDebuff();
    }
};

/**
 * Attack data for damage calculations
 */
struct AttackData {
    float baseDamage = 0.0f;
    DamageType damageType = DamageType::Physical;
    float critChance = 0.0f;
    float critMultiplier = 2.0f;
    float armorPenetration = 0.0f;
    bool canBeBlocked = true;
    bool canBeDodged = true;
    Engine::vec3 knockbackDirection = Engine::vec3(0.0f, 0.0f, 0.0f);
    float knockbackForce = 0.0f;
    float stunDuration = 0.0f;

    // Status effect on hit
    bool appliesStatusEffect = false;
    StatusEffectType statusEffectType = StatusEffectType::Burning;
    float statusEffectDuration = 0.0f;
    float statusEffectValue = 0.0f;
    float statusEffectChance = 1.0f;  // 1.0 = 100% chance
};

} // namespace CatGame

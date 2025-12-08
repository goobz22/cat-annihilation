#pragma once

#include <cmath>

namespace CatGame {

/**
 * Weapon types available in the game
 */
enum class WeaponType {
    Sword,      // Melee weapon with swing attacks
    Staff,      // Magic weapon for spell casting
    Bow         // Ranged weapon with projectile attacks
};

/**
 * Combat component for entities that can attack
 *
 * Features:
 * - Base attack damage
 * - Attack speed and cooldown management
 * - Attack range for melee combat
 * - Weapon type system
 * - Attack validation
 */
struct CombatComponent {
    // Attack parameters
    float attackDamage = 25.0f;           // Base damage per attack
    float attackSpeed = 1.0f;             // Attacks per second
    float attackRange = 3.0f;             // Range for melee attacks (units)

    // Current attack state
    float attackCooldown = 0.0f;          // Current cooldown timer (seconds)
    WeaponType equippedWeapon = WeaponType::Sword;  // Currently equipped weapon

    // Attack modifiers
    float damageMultiplier = 1.0f;        // Multiplicative damage modifier (buffs/debuffs)
    float attackSpeedMultiplier = 1.0f;   // Attack speed modifier
    bool canAttack = true;                // Can the entity attack? (disarmed, stunned, etc.)

    /**
     * Get the cooldown duration for the current attack speed
     * @return Cooldown duration in seconds
     */
    float getCooldownDuration() const {
        float effectiveAttackSpeed = attackSpeed * attackSpeedMultiplier;
        if (effectiveAttackSpeed <= 0.0f) {
            return 9999.0f; // Effectively infinite cooldown
        }
        return 1.0f / effectiveAttackSpeed;
    }

    /**
     * Check if the entity can currently attack
     * @return true if attack cooldown is ready and entity can attack
     */
    bool canPerformAttack() const {
        return canAttack && attackCooldown <= 0.0f;
    }

    /**
     * Start an attack, setting the cooldown timer
     * @return true if attack was started
     */
    bool startAttack() {
        if (!canPerformAttack()) {
            return false;
        }

        attackCooldown = getCooldownDuration();
        return true;
    }

    /**
     * Update attack cooldown (call each frame)
     * @param dt Delta time in seconds
     */
    void updateCooldown(float dt) {
        if (attackCooldown > 0.0f) {
            attackCooldown = std::max(0.0f, attackCooldown - dt);
        }
    }

    /**
     * Get the final attack damage with modifiers applied
     * @return Effective damage amount
     */
    float getEffectiveDamage() const {
        return attackDamage * damageMultiplier;
    }

    /**
     * Get cooldown progress as a percentage (0.0 to 1.0)
     * @return Cooldown progress (1.0 = ready, 0.0 = just attacked)
     */
    float getCooldownProgress() const {
        float cooldownDuration = getCooldownDuration();
        if (cooldownDuration <= 0.0f) {
            return 1.0f;
        }
        return 1.0f - (attackCooldown / cooldownDuration);
    }

    /**
     * Reset attack cooldown (make attack immediately available)
     */
    void resetCooldown() {
        attackCooldown = 0.0f;
    }

    /**
     * Switch equipped weapon
     * @param newWeapon The weapon type to equip
     */
    void equipWeapon(WeaponType newWeapon) {
        equippedWeapon = newWeapon;

        // Adjust attack parameters based on weapon type
        switch (newWeapon) {
            case WeaponType::Sword:
                attackRange = 3.0f;
                attackSpeed = 1.5f;
                attackDamage = 25.0f;
                break;

            case WeaponType::Staff:
                attackRange = 10.0f;   // Spell projectile spawns at this range
                attackSpeed = 0.8f;    // Slower but powerful
                attackDamage = 40.0f;
                break;

            case WeaponType::Bow:
                attackRange = 15.0f;   // Arrow projectile spawn range
                attackSpeed = 1.2f;
                attackDamage = 30.0f;
                break;
        }
    }

    /**
     * Check if currently using a melee weapon
     * @return true if weapon is melee (Sword)
     */
    bool isMeleeWeapon() const {
        return equippedWeapon == WeaponType::Sword;
    }

    /**
     * Check if currently using a ranged weapon
     * @return true if weapon is ranged (Staff or Bow)
     */
    bool isRangedWeapon() const {
        return equippedWeapon == WeaponType::Staff ||
               equippedWeapon == WeaponType::Bow;
    }
};

} // namespace CatGame

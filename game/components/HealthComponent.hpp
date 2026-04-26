#pragma once

#include <functional>
#include <algorithm>

// status_effects.hpp owns DamageType (Physical, Fire, Ice, Poison, Magic, True).
// We pull it in here so HealthComponent can record the *type* of the damage
// that brought the entity to zero hp — that field is read back by the game
// layer's setOnEntityDeath subscriber when constructing EntityDeathEvent so the
// per-element death-burst dispatcher can pick the right particle profile. The
// components→systems direction is unusual but already established in this
// codebase (game/components/combat_components.hpp:4 includes the same header).
#include "../systems/status_effects.hpp"

namespace CatGame {

/**
 * Health component for entities that can take damage and die
 *
 * Features:
 * - Current and max health tracking
 * - Damage and healing with clamping
 * - Invincibility frames for temporary damage immunity
 * - Death state checking
 * - Callbacks for damage and death events
 */
struct HealthComponent {
    float currentHealth = 100.0f;
    float maxHealth = 100.0f;
    float shield = 0.0f;               // Shield absorbs damage before health
    float maxShield = 0.0f;

    // Invincibility frames
    float invincibilityTimer = 0.0f;
    float invincibilityDuration = 0.5f;  // Default 0.5 seconds after taking damage

    // Regeneration
    bool canRegenerate = false;        // Whether health regenerates over time
    float regenerationRate = 0.0f;     // Health per second when regenerating
    float regenerationDelay = 3.0f;    // Seconds after damage before regen starts
    float timeSinceLastDamage = 0.0f;  // Time since last damage was taken

    // Death state
    bool isDead = false;               // Whether entity has died
    float deathTimer = 0.0f;           // Time since death
    float deathAnimationDuration = 1.0f; // How long death animation lasts

    // Most recent damage type applied to this entity. CombatSystem::applyDamage
    // and applyDamageWithType both write this BEFORE calling damage() so the
    // value reflects the killing-blow type if the same call also reduces hp to
    // zero. The game layer's setOnEntityDeath subscriber reads it when
    // populating EntityDeathEvent.damageType so the per-element death-burst
    // dispatcher (CatAnnihilation::spawnDeathParticles) picks the right
    // visual profile (orange-red for Physical, orange-yellow rising for Fire,
    // pale-cyan drifting for Ice, yellow-green lingering for Poison,
    // white-purple radial for Magic, white-yellow for True). Defaults to
    // Physical so an unwired damage path still produces the original
    // orange-red death burst — backwards-compatible by construction.
    DamageType lastDamageType = DamageType::Physical;

    // Callbacks
    std::function<void(float)> onDamage = nullptr;     // Called when damage is taken (amount)
    std::function<void()> onDeath = nullptr;            // Called when health reaches 0

    /**
     * Apply damage to the entity
     * @param amount Amount of damage to apply
     * @return true if damage was applied (not invincible)
     */
    bool damage(float amount) {
        // Check invincibility
        if (invincibilityTimer > 0.0f) {
            return false;
        }

        if (amount <= 0.0f) {
            return false;
        }

        currentHealth = std::max(0.0f, currentHealth - amount);

        // Start invincibility frames
        invincibilityTimer = invincibilityDuration;

        // Trigger damage callback
        if (onDamage) {
            onDamage(amount);
        }

        // Check for death
        if (currentHealth <= 0.0f && onDeath) {
            isDead = true;
            onDeath();
        }

        return true;
    }

    /**
     * Heal the entity
     * @param amount Amount of health to restore
     */
    void heal(float amount) {
        if (amount <= 0.0f) {
            return;
        }

        currentHealth = std::min(maxHealth, currentHealth + amount);
    }

    /**
     * Check if entity is dead (based on health value)
     * @return true if health is 0 or less
     */
    bool checkIsDead() const {
        return currentHealth <= 0.0f || isDead;
    }

    /**
     * Check if entity is alive
     * @return true if entity is not dead
     */
    bool isAlive() const {
        return !checkIsDead();
    }

    /**
     * Check if entity is at full health
     * @return true if health equals max health
     */
    bool isFullHealth() const {
        return currentHealth >= maxHealth;
    }

    /**
     * Get health as a percentage (0.0 to 1.0)
     * @return Health ratio
     */
    float getHealthPercentage() const {
        if (maxHealth <= 0.0f) {
            return 0.0f;
        }
        return currentHealth / maxHealth;
    }

    /**
     * Update invincibility timer (call each frame)
     * @param dt Delta time in seconds
     */
    void updateInvincibility(float dt) {
        if (invincibilityTimer > 0.0f) {
            invincibilityTimer = std::max(0.0f, invincibilityTimer - dt);
        }
    }

    /**
     * Check if currently invincible
     * @return true if invincibility frames are active
     */
    bool isInvincible() const {
        return invincibilityTimer > 0.0f;
    }

    /**
     * Reset health to maximum
     */
    void resetHealth() {
        currentHealth = maxHealth;
        invincibilityTimer = 0.0f;
    }

    /**
     * Set max health and optionally fill current health
     * @param newMaxHealth New maximum health value
     * @param fillHealth If true, set current health to max
     */
    void setMaxHealth(float newMaxHealth, bool fillHealth = true) {
        maxHealth = std::max(1.0f, newMaxHealth);
        if (fillHealth) {
            currentHealth = maxHealth;
        } else {
            currentHealth = std::min(currentHealth, maxHealth);
        }
    }
};

} // namespace CatGame

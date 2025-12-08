#pragma once

#include <functional>
#include <algorithm>

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

    // Invincibility frames
    float invincibilityTimer = 0.0f;
    float invincibilityDuration = 0.5f;  // Default 0.5 seconds after taking damage

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
        if (isDead() && onDeath) {
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
     * Check if entity is dead
     * @return true if health is 0 or less
     */
    bool isDead() const {
        return currentHealth <= 0.0f;
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

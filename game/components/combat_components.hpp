#pragma once

#include "../../engine/math/Vector.hpp"
#include "../systems/status_effects.hpp"
#include <vector>
#include <string>

namespace CatGame {

/**
 * Combat state enumeration
 */
enum class CombatState {
    Idle,       // Not in combat
    Attacking,  // Performing attack
    Blocking,   // Actively blocking
    Dodging,    // Dodge rolling (i-frames)
    Stunned,    // Cannot act
    Casting     // Casting ability/spell
};

/**
 * Block state component
 * Handles blocking mechanics and stamina
 */
struct BlockState {
    bool isBlocking = false;
    float blockDuration = 0.0f;              // Current block duration
    float maxBlockDuration = 3.0f;           // Maximum continuous block time
    float blockStamina = 100.0f;             // Current block stamina
    float maxBlockStamina = 100.0f;          // Maximum stamina
    float blockStaminaDrain = 20.0f;         // Stamina drain per second while blocking
    float blockStaminaRegen = 15.0f;         // Stamina regen per second while not blocking
    float damageReduction = 0.7f;            // Block reduces 70% damage
    float perfectBlockWindow = 0.2f;         // Seconds for perfect block timing
    float perfectBlockDamageReduction = 1.0f; // Perfect block = 100% damage reduction
    float lastBlockStartTime = 0.0f;         // Time when block was initiated
    bool canParry = true;                    // Can parry attacks
    float parryStunDuration = 1.5f;          // Stun duration on successful parry

    /**
     * Start blocking
     * @param currentTime Current game time
     * @return true if block started successfully
     */
    bool startBlock(float currentTime) {
        if (blockStamina <= 0.0f) {
            return false;  // No stamina to block
        }

        isBlocking = true;
        blockDuration = 0.0f;
        lastBlockStartTime = currentTime;
        return true;
    }

    /**
     * End blocking
     */
    void endBlock() {
        isBlocking = false;
        blockDuration = 0.0f;
    }

    /**
     * Update block state
     * @param dt Delta time
     */
    void update(float dt) {
        if (isBlocking) {
            // Drain stamina
            blockStamina -= blockStaminaDrain * dt;
            blockDuration += dt;

            // Force end block if stamina depleted or max duration reached
            if (blockStamina <= 0.0f || blockDuration >= maxBlockDuration) {
                endBlock();
                blockStamina = 0.0f;  // Fully depleted
            }
        } else {
            // Regenerate stamina
            blockStamina += blockStaminaRegen * dt;
            blockStamina = std::min(blockStamina, maxBlockStamina);
        }
    }

    /**
     * Check if attack timing is within perfect block window
     * @param attackTime Time when attack hit
     * @return true if perfect block
     */
    bool isPerfectBlock(float attackTime) const {
        if (!isBlocking) {
            return false;
        }

        float timeSinceBlockStart = attackTime - lastBlockStartTime;
        return timeSinceBlockStart <= perfectBlockWindow;
    }

    /**
     * Get stamina percentage
     * @return Stamina as percentage (0.0 to 1.0)
     */
    float getStaminaPercent() const {
        return blockStamina / maxBlockStamina;
    }
};

/**
 * Dodge state component
 * Handles dodge rolling with invincibility frames
 */
struct DodgeState {
    bool isDodging = false;
    float dodgeDuration = 0.3f;              // Total dodge duration
    float currentDodgeTime = 0.0f;           // Current time in dodge
    float dodgeCooldown = 1.0f;              // Cooldown between dodges
    float currentCooldown = 0.0f;            // Current cooldown timer
    float dodgeDistance = 5.0f;              // Distance to travel during dodge
    float iFrameDuration = 0.2f;             // Invincibility frame duration
    Engine::vec3 dodgeDirection = Engine::vec3(0.0f, 0.0f, 0.0f);
    Engine::vec3 dodgeStartPosition = Engine::vec3(0.0f, 0.0f, 0.0f);
    float dodgeSpeed = 0.0f;                 // Current dodge speed

    /**
     * Start dodge in direction
     * @param direction Direction to dodge (will be normalized)
     * @param startPos Starting position
     * @return true if dodge started
     */
    bool startDodge(const Engine::vec3& direction, const Engine::vec3& startPos) {
        if (!canDodge()) {
            return false;
        }

        isDodging = true;
        currentDodgeTime = 0.0f;
        currentCooldown = dodgeCooldown;
        dodgeDirection = direction.normalized();
        dodgeStartPosition = startPos;
        dodgeSpeed = dodgeDistance / dodgeDuration;

        return true;
    }

    /**
     * Update dodge state
     * @param dt Delta time
     */
    void update(float dt) {
        if (isDodging) {
            currentDodgeTime += dt;

            if (currentDodgeTime >= dodgeDuration) {
                isDodging = false;
                currentDodgeTime = 0.0f;
            }
        }

        // Update cooldown
        if (currentCooldown > 0.0f) {
            currentCooldown -= dt;
            if (currentCooldown < 0.0f) {
                currentCooldown = 0.0f;
            }
        }
    }

    /**
     * Check if can dodge
     * @return true if dodge is available
     */
    bool canDodge() const {
        return !isDodging && currentCooldown <= 0.0f;
    }

    /**
     * Check if currently invincible (in i-frames)
     * @return true if invincible
     */
    bool isInvincible() const {
        return isDodging && currentDodgeTime < iFrameDuration;
    }

    /**
     * Get dodge progress (0.0 to 1.0)
     * @return Dodge animation progress
     */
    float getDodgeProgress() const {
        if (!isDodging) {
            return 0.0f;
        }
        return currentDodgeTime / dodgeDuration;
    }

    /**
     * Get cooldown progress (0.0 to 1.0, 1.0 = ready)
     * @return Cooldown progress
     */
    float getCooldownProgress() const {
        if (dodgeCooldown <= 0.0f) {
            return 1.0f;
        }
        return 1.0f - (currentCooldown / dodgeCooldown);
    }

    /**
     * Get current dodge velocity
     * @return Dodge velocity vector
     */
    Engine::vec3 getDodgeVelocity() const {
        if (!isDodging) {
            return Engine::vec3(0.0f, 0.0f, 0.0f);
        }

        // Use ease-out curve for smooth deceleration
        float progress = getDodgeProgress();
        float speedMultiplier = 1.0f - (progress * progress);  // Quadratic ease-out

        return dodgeDirection * dodgeSpeed * speedMultiplier;
    }
};

/**
 * Combo state component
 * Tracks attack combos and damage multipliers
 */
struct ComboState {
    std::string currentCombo = "";           // Current combo string (e.g., "LLL" for 3 light attacks)
    int comboStep = 0;                       // Current step in combo (0-based)
    float comboTimer = 0.0f;                 // Time since last attack
    float comboWindow = 0.5f;                // Seconds to input next attack
    int maxComboLength = 4;                  // Maximum combo chain length
    std::vector<float> comboDamageMultipliers = {1.0f, 1.2f, 1.5f, 2.0f}; // Damage per step

    // Combo names and effects
    std::string lastComboName = "";
    int consecutiveHits = 0;

    /**
     * Start new combo or continue existing
     * @param attackType Type of attack (L=light, H=heavy, S=special)
     * @return true if combo continues, false if reset
     */
    bool addAttack(char attackType) {
        // Reset if timer expired
        if (comboTimer > comboWindow) {
            reset();
        }

        // Add to combo string
        currentCombo += attackType;
        comboStep++;
        consecutiveHits++;

        // Reset timer
        comboTimer = 0.0f;

        // Check if max combo reached
        if (comboStep >= maxComboLength) {
            // Execute finisher
            return false;
        }

        return true;
    }

    /**
     * Update combo timer
     * @param dt Delta time
     */
    void update(float dt) {
        comboTimer += dt;

        // Reset if window expired
        if (comboTimer > comboWindow && comboStep > 0) {
            reset();
        }
    }

    /**
     * Reset combo
     */
    void reset() {
        if (comboStep > 0) {
            lastComboName = currentCombo;
        }
        currentCombo = "";
        comboStep = 0;
        comboTimer = 0.0f;
        consecutiveHits = 0;
    }

    /**
     * Get current damage multiplier
     * @return Multiplier for current combo step
     */
    float getCurrentDamageMultiplier() const {
        if (comboStep <= 0 || comboStep > static_cast<int>(comboDamageMultipliers.size())) {
            return 1.0f;
        }
        return comboDamageMultipliers[comboStep - 1];
    }

    /**
     * Check if in active combo
     * @return true if combo is active
     */
    bool isInCombo() const {
        return comboStep > 0 && comboTimer <= comboWindow;
    }

    /**
     * Get combo progress (0.0 to 1.0, 1.0 = max combo)
     * @return Combo progress
     */
    float getComboProgress() const {
        if (maxComboLength <= 0) {
            return 0.0f;
        }
        return static_cast<float>(comboStep) / static_cast<float>(maxComboLength);
    }

    /**
     * Get time remaining in combo window
     * @return Seconds remaining
     */
    float getTimeRemaining() const {
        return std::max(0.0f, comboWindow - comboTimer);
    }
};

/**
 * Status effects component
 * Manages active status effects on an entity
 */
struct StatusEffectsComponent {
    std::vector<StatusEffect> effects;

    /**
     * Add status effect
     * @param effect Effect to add
     */
    void addEffect(const StatusEffect& effect) {
        // Check if effect already exists
        for (auto& existingEffect : effects) {
            if (existingEffect.type == effect.type) {
                // Stackable effect - add stacks and refresh
                if (existingEffect.maxStacks > 1) {
                    existingEffect.addStacks(1);
                    existingEffect.refresh();
                    return;
                } else {
                    // Non-stackable - just refresh duration
                    existingEffect.refresh();
                    return;
                }
            }
        }

        // New effect
        effects.push_back(effect);
    }

    /**
     * Remove effect by type
     * @param type Type of effect to remove
     */
    void removeEffect(StatusEffectType type) {
        effects.erase(
            std::remove_if(
                effects.begin(),
                effects.end(),
                [type](const StatusEffect& effect) { return effect.type == type; }
            ),
            effects.end()
        );
    }

    /**
     * Update all effects
     * @param dt Delta time
     */
    void update(float dt) {
        // Update all effects
        for (auto& effect : effects) {
            effect.update(dt);
        }

        // Remove expired effects
        effects.erase(
            std::remove_if(
                effects.begin(),
                effects.end(),
                [](const StatusEffect& effect) { return !effect.isPermanent && effect.remainingTime <= 0.0f; }
            ),
            effects.end()
        );
    }

    /**
     * Check if has effect
     * @param type Type to check
     * @return true if effect is active
     */
    bool hasEffect(StatusEffectType type) const {
        for (const auto& effect : effects) {
            if (effect.type == type) {
                return true;
            }
        }
        return false;
    }

    /**
     * Get effect by type
     * @param type Type to find
     * @return Pointer to effect or nullptr
     */
    const StatusEffect* getEffect(StatusEffectType type) const {
        for (const auto& effect : effects) {
            if (effect.type == type) {
                return &effect;
            }
        }
        return nullptr;
    }

    /**
     * Get movement speed multiplier from all effects
     * @return Combined speed multiplier
     */
    float getMovementSpeedMultiplier() const {
        float multiplier = 1.0f;

        for (const auto& effect : effects) {
            switch (effect.type) {
                case StatusEffectType::Slowed:
                    multiplier *= (1.0f - effect.getEffectiveValue());
                    break;
                case StatusEffectType::Frozen:
                    multiplier *= 0.3f;  // 70% slow
                    break;
                case StatusEffectType::Hasted:
                    multiplier *= (1.0f + effect.getEffectiveValue());
                    break;
                case StatusEffectType::Rooted:
                    multiplier = 0.0f;
                    break;
                default:
                    break;
            }
        }

        return std::max(0.0f, multiplier);
    }

    /**
     * Get damage multiplier from all effects
     * @return Combined damage multiplier
     */
    float getDamageMultiplier() const {
        float multiplier = 1.0f;

        for (const auto& effect : effects) {
            switch (effect.type) {
                case StatusEffectType::Weakened:
                    multiplier *= (1.0f - effect.getEffectiveValue());
                    break;
                case StatusEffectType::Strengthened:
                    multiplier *= (1.0f + effect.getEffectiveValue());
                    break;
                default:
                    break;
            }
        }

        return std::max(0.0f, multiplier);
    }

    /**
     * Get damage reduction from all effects
     * @return Total damage reduction (0.0 to 1.0)
     */
    float getDamageReduction() const {
        float reduction = 0.0f;

        for (const auto& effect : effects) {
            if (effect.type == StatusEffectType::Armored) {
                reduction += effect.getEffectiveValue();
            }
        }

        return std::min(0.95f, reduction);  // Cap at 95% reduction
    }

    /**
     * Check if can act (not stunned/frozen)
     * @return true if entity can act
     */
    bool canAct() const {
        return !hasEffect(StatusEffectType::Stunned);
    }

    /**
     * Check if can move
     * @return true if entity can move
     */
    bool canMove() const {
        return !hasEffect(StatusEffectType::Stunned) && !hasEffect(StatusEffectType::Rooted);
    }

    /**
     * Get all active debuffs
     * @return Vector of active debuffs
     */
    std::vector<StatusEffect> getDebuffs() const {
        std::vector<StatusEffect> debuffs;
        for (const auto& effect : effects) {
            if (effect.isDebuff()) {
                debuffs.push_back(effect);
            }
        }
        return debuffs;
    }

    /**
     * Get all active buffs
     * @return Vector of active buffs
     */
    std::vector<StatusEffect> getBuffs() const {
        std::vector<StatusEffect> buffs;
        for (const auto& effect : effects) {
            if (effect.isBuff()) {
                buffs.push_back(effect);
            }
        }
        return buffs;
    }

    /**
     * Clear all effects
     */
    void clear() {
        effects.clear();
    }
};

/**
 * Block component wrapper
 */
struct BlockComponent {
    BlockState state;
};

/**
 * Dodge component wrapper
 */
struct DodgeComponent {
    DodgeState state;
};

/**
 * Combo component wrapper
 */
struct ComboComponent {
    ComboState state;
};

} // namespace CatGame

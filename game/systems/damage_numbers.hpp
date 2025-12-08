#pragma once

#include "../../engine/math/Vector.hpp"
#include "../systems/status_effects.hpp"
#include <vector>
#include <string>

namespace CatGame {

/**
 * Type of damage number to display
 */
enum class DamageNumberType {
    Physical,    // Red - physical damage
    Fire,        // Orange - fire damage
    Ice,         // Cyan - ice damage
    Poison,      // Green - poison damage
    Magic,       // Purple - magic damage
    True,        // White - true damage
    Heal,        // Bright green - healing
    Critical,    // Yellow - critical hit
    Miss,        // Gray - miss/dodge
    Block        // Blue - blocked attack
};

/**
 * Color for damage number types
 */
struct DamageNumberColor {
    float r, g, b, a;

    DamageNumberColor(float r_ = 1.0f, float g_ = 1.0f, float b_ = 1.0f, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    /**
     * Get color for damage type
     */
    static DamageNumberColor forType(DamageNumberType type) {
        switch (type) {
            case DamageNumberType::Physical:
                return DamageNumberColor(1.0f, 0.2f, 0.2f, 1.0f);  // Red
            case DamageNumberType::Fire:
                return DamageNumberColor(1.0f, 0.5f, 0.0f, 1.0f);  // Orange
            case DamageNumberType::Ice:
                return DamageNumberColor(0.3f, 0.8f, 1.0f, 1.0f);  // Cyan
            case DamageNumberType::Poison:
                return DamageNumberColor(0.3f, 0.9f, 0.3f, 1.0f);  // Green
            case DamageNumberType::Magic:
                return DamageNumberColor(0.8f, 0.3f, 1.0f, 1.0f);  // Purple
            case DamageNumberType::True:
                return DamageNumberColor(1.0f, 1.0f, 1.0f, 1.0f);  // White
            case DamageNumberType::Heal:
                return DamageNumberColor(0.2f, 1.0f, 0.2f, 1.0f);  // Bright green
            case DamageNumberType::Critical:
                return DamageNumberColor(1.0f, 1.0f, 0.2f, 1.0f);  // Yellow
            case DamageNumberType::Miss:
                return DamageNumberColor(0.6f, 0.6f, 0.6f, 1.0f);  // Gray
            case DamageNumberType::Block:
                return DamageNumberColor(0.3f, 0.5f, 1.0f, 1.0f);  // Blue
            default:
                return DamageNumberColor(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    /**
     * Convert DamageType to DamageNumberType
     */
    static DamageNumberType fromDamageType(DamageType type, bool isCrit = false) {
        if (isCrit) {
            return DamageNumberType::Critical;
        }

        switch (type) {
            case DamageType::Physical:
                return DamageNumberType::Physical;
            case DamageType::Fire:
                return DamageNumberType::Fire;
            case DamageType::Ice:
                return DamageNumberType::Ice;
            case DamageType::Poison:
                return DamageNumberType::Poison;
            case DamageType::Magic:
                return DamageNumberType::Magic;
            case DamageType::True:
                return DamageNumberType::True;
            default:
                return DamageNumberType::Physical;
        }
    }
};

/**
 * Individual damage number instance
 */
struct DamageNumber {
    Engine::vec3 position;           // World position
    Engine::vec3 velocity;           // Movement velocity (floats upward)
    std::string text;                // Text to display (e.g., "-25", "MISS", "x3")
    DamageNumberType type;           // Type determines color
    DamageNumberColor color;         // Display color
    float lifetime;                  // Current lifetime
    float maxLifetime;               // Max lifetime before fade
    float scale;                     // Size multiplier
    float fadeStartTime;             // When to start fading
    bool active;                     // Is this number active?

    DamageNumber()
        : position(0.0f, 0.0f, 0.0f)
        , velocity(0.0f, 1.0f, 0.0f)
        , text("")
        , type(DamageNumberType::Physical)
        , color(DamageNumberColor::forType(DamageNumberType::Physical))
        , lifetime(0.0f)
        , maxLifetime(1.5f)
        , scale(1.0f)
        , fadeStartTime(1.0f)
        , active(true)
    {}

    /**
     * Update damage number
     * @param dt Delta time
     * @return true if still active
     */
    bool update(float dt) {
        if (!active) {
            return false;
        }

        lifetime += dt;

        // Move upward
        position += velocity * dt;

        // Slow down velocity over time
        velocity *= 0.95f;

        // Check if expired
        if (lifetime >= maxLifetime) {
            active = false;
            return false;
        }

        return true;
    }

    /**
     * Get current alpha (with fade)
     * @return Alpha value (0.0 to 1.0)
     */
    float getAlpha() const {
        if (lifetime < fadeStartTime) {
            return color.a;
        }

        // Fade out
        float fadeProgress = (lifetime - fadeStartTime) / (maxLifetime - fadeStartTime);
        return color.a * (1.0f - fadeProgress);
    }

    /**
     * Get current scale (with animation)
     * @return Scale multiplier
     */
    float getScale() const {
        // Pop in animation (first 0.1 seconds)
        if (lifetime < 0.1f) {
            float progress = lifetime / 0.1f;
            return scale * (0.5f + progress * 0.5f);  // Start at 50%, grow to 100%
        }

        return scale;
    }
};

/**
 * Damage number manager
 * Handles spawning and updating damage numbers
 */
class DamageNumberManager {
public:
    /**
     * Constructor
     */
    DamageNumberManager() = default;

    /**
     * Spawn damage number
     * @param position World position
     * @param damage Damage amount
     * @param type Damage type
     * @param isCrit Is critical hit?
     */
    void spawnDamageNumber(
        const Engine::vec3& position,
        float damage,
        DamageType type,
        bool isCrit = false
    );

    /**
     * Spawn damage number with custom text
     * @param position World position
     * @param text Custom text
     * @param type Number type
     * @param scale Size multiplier
     */
    void spawnCustomNumber(
        const Engine::vec3& position,
        const std::string& text,
        DamageNumberType type,
        float scale = 1.0f
    );

    /**
     * Spawn healing number
     * @param position World position
     * @param healAmount Amount healed
     */
    void spawnHealNumber(const Engine::vec3& position, float healAmount);

    /**
     * Spawn miss indicator
     * @param position World position
     */
    void spawnMissIndicator(const Engine::vec3& position);

    /**
     * Spawn block indicator
     * @param position World position
     * @param damageBlocked Amount blocked
     * @param isPerfectBlock Was it a perfect block?
     */
    void spawnBlockIndicator(
        const Engine::vec3& position,
        float damageBlocked,
        bool isPerfectBlock = false
    );

    /**
     * Spawn combo counter
     * @param position World position
     * @param comboCount Combo number
     */
    void spawnComboCounter(const Engine::vec3& position, int comboCount);

    /**
     * Update all damage numbers
     * @param dt Delta time
     */
    void update(float dt);

    /**
     * Get all active damage numbers
     * @return Vector of active numbers
     */
    const std::vector<DamageNumber>& getActiveNumbers() const {
        return damageNumbers_;
    }

    /**
     * Clear all damage numbers
     */
    void clear();

    /**
     * Get number count
     * @return Number of active damage numbers
     */
    size_t getCount() const {
        return damageNumbers_.size();
    }

private:
    std::vector<DamageNumber> damageNumbers_;

    /**
     * Add random offset to position (spread damage numbers)
     * @param position Base position
     * @return Position with random offset
     */
    Engine::vec3 addRandomOffset(const Engine::vec3& position);

    /**
     * Format damage number text
     * @param damage Damage amount
     * @return Formatted string
     */
    std::string formatDamage(float damage);
};

} // namespace CatGame

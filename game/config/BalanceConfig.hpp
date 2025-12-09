#ifndef GAME_CONFIG_BALANCE_CONFIG_HPP
#define GAME_CONFIG_BALANCE_CONFIG_HPP

#include <cstdint>

namespace Game {

/**
 * @brief Game balance configuration - all tunable gameplay values
 *
 * This is the central location for all game balance parameters.
 * Modify these values to adjust difficulty, progression, and feel.
 */
namespace Balance {

// ============================================================================
// Player Configuration
// ============================================================================

namespace Player {
    constexpr float MAX_HEALTH = 100.0f;
    constexpr float STARTING_HEALTH = 100.0f;
    constexpr float HEALTH_REGEN_RATE = 0.0f;           // HP per second (0 = no regen)
    constexpr float HEALTH_REGEN_DELAY = 5.0f;          // Seconds after taking damage

    constexpr float MOVE_SPEED = 5.0f;                  // Units per second
    constexpr float SPRINT_SPEED_MULTIPLIER = 1.5f;
    constexpr float WALK_SPEED_MULTIPLIER = 0.5f;

    constexpr float TURN_SPEED = 180.0f;                // Degrees per second
    constexpr float MOUSE_SENSITIVITY = 0.1f;

    constexpr float INVINCIBILITY_TIME = 1.0f;          // Seconds after taking damage
}

// ============================================================================
// Combat Configuration
// ============================================================================

namespace Combat {
    // Melee Attack
    constexpr float MELEE_DAMAGE = 25.0f;
    constexpr float MELEE_RANGE = 2.0f;
    constexpr float MELEE_ANGLE = 90.0f;                // Attack cone angle in degrees
    constexpr float MELEE_COOLDOWN = 0.5f;              // Seconds between attacks
    constexpr float MELEE_KNOCKBACK = 5.0f;

    // Sword Swing
    constexpr float SWORD_DAMAGE = 35.0f;
    constexpr float SWORD_RANGE = 2.5f;
    constexpr float SWORD_ANGLE = 120.0f;
    constexpr float SWORD_COOLDOWN = 0.6f;
    constexpr float SWORD_KNOCKBACK = 8.0f;

    // Projectile
    constexpr float PROJECTILE_DAMAGE = 15.0f;
    constexpr float PROJECTILE_SPEED = 20.0f;           // Units per second
    constexpr float PROJECTILE_LIFETIME = 3.0f;         // Seconds before despawning
    constexpr float PROJECTILE_COOLDOWN = 0.3f;
    constexpr float PROJECTILE_SIZE = 0.2f;

    // Hit Detection
    constexpr float HIT_REGISTRATION_TOLERANCE = 0.5f;   // Extra collision padding
}

// ============================================================================
// Enemy Configuration
// ============================================================================

namespace Enemy {
    // Base Enemy Stats
    namespace Base {
        constexpr float HEALTH = 50.0f;
        constexpr float DAMAGE = 10.0f;
        constexpr float MOVE_SPEED = 3.0f;
        constexpr float ATTACK_RANGE = 1.5f;
        constexpr float ATTACK_COOLDOWN = 1.5f;
        constexpr float DETECTION_RANGE = 15.0f;
        constexpr float SCORE_VALUE = 10;
    }

    // Fast Enemy (Smaller, faster, less health)
    namespace Fast {
        constexpr float HEALTH = 30.0f;
        constexpr float DAMAGE = 8.0f;
        constexpr float MOVE_SPEED = 5.0f;
        constexpr float ATTACK_RANGE = 1.2f;
        constexpr float ATTACK_COOLDOWN = 1.0f;
        constexpr float DETECTION_RANGE = 20.0f;
        constexpr float SCORE_VALUE = 15;
    }

    // Tank Enemy (Slow, lots of health, high damage)
    namespace Tank {
        constexpr float HEALTH = 150.0f;
        constexpr float DAMAGE = 25.0f;
        constexpr float MOVE_SPEED = 2.0f;
        constexpr float ATTACK_RANGE = 2.0f;
        constexpr float ATTACK_COOLDOWN = 2.0f;
        constexpr float DETECTION_RANGE = 12.0f;
        constexpr float SCORE_VALUE = 50;
    }

    // Ranged Enemy (Keeps distance, shoots projectiles)
    namespace Ranged {
        constexpr float HEALTH = 40.0f;
        constexpr float DAMAGE = 12.0f;
        constexpr float MOVE_SPEED = 2.5f;
        constexpr float ATTACK_RANGE = 10.0f;
        constexpr float ATTACK_COOLDOWN = 2.5f;
        constexpr float DETECTION_RANGE = 15.0f;
        constexpr float PREFERRED_DISTANCE = 8.0f;      // Tries to stay at this distance
        constexpr float SCORE_VALUE = 20;
    }

    // Elite Enemy (Boss-like, appears in later waves)
    namespace Elite {
        constexpr float HEALTH = 300.0f;
        constexpr float DAMAGE = 40.0f;
        constexpr float MOVE_SPEED = 3.5f;
        constexpr float ATTACK_RANGE = 2.5f;
        constexpr float ATTACK_COOLDOWN = 1.2f;
        constexpr float DETECTION_RANGE = 25.0f;
        constexpr float SCORE_VALUE = 100;
    }

    // Enemy Behavior
    constexpr float WANDER_SPEED_MULTIPLIER = 0.5f;
    constexpr float WANDER_CHANGE_INTERVAL = 3.0f;      // Seconds between direction changes
    constexpr float SEPARATION_RADIUS = 1.0f;           // Min distance between enemies
    constexpr float PATH_UPDATE_INTERVAL = 0.5f;        // Seconds between pathfinding updates
}

// ============================================================================
// Wave System Configuration
// ============================================================================

namespace Waves {
    constexpr uint32_t STARTING_WAVE = 1;

    // Wave Scaling
    constexpr float ENEMY_COUNT_BASE = 5.0f;            // Starting enemies
    constexpr float ENEMY_COUNT_PER_WAVE = 3.0f;        // Additional enemies per wave
    constexpr float ENEMY_COUNT_SCALING = 1.1f;         // Exponential scaling factor

    constexpr float HEALTH_SCALING_PER_WAVE = 1.08f;    // 8% health increase per wave
    constexpr float DAMAGE_SCALING_PER_WAVE = 1.05f;    // 5% damage increase per wave
    constexpr float SPEED_SCALING_PER_WAVE = 1.02f;     // 2% speed increase per wave
    constexpr float MAX_SCALING_MULTIPLIER = 3.0f;      // Cap on scaling

    // Wave Timing
    constexpr float WAVE_START_DELAY = 3.0f;            // Seconds before first wave
    constexpr float WAVE_COMPLETE_DELAY = 5.0f;         // Seconds after clearing wave
    constexpr float NEXT_WAVE_COUNTDOWN = 10.0f;        // Countdown before next wave

    // Wave Composition (percentage of each enemy type)
    constexpr float BASE_ENEMY_PERCENTAGE = 0.6f;       // 60% base enemies
    constexpr float FAST_ENEMY_PERCENTAGE = 0.25f;      // 25% fast enemies
    constexpr float TANK_ENEMY_PERCENTAGE = 0.1f;       // 10% tank enemies
    constexpr float RANGED_ENEMY_PERCENTAGE = 0.05f;    // 5% ranged enemies

    // Elite enemies appear every N waves
    constexpr uint32_t ELITE_WAVE_FREQUENCY = 5;        // Every 5 waves
    constexpr uint32_t ELITES_PER_BOSS_WAVE = 1;        // Number of elites in boss wave

    // Spawn Configuration
    constexpr float SPAWN_DISTANCE_MIN = 15.0f;         // Min distance from player to spawn
    constexpr float SPAWN_DISTANCE_MAX = 25.0f;         // Max distance from player to spawn
    constexpr float SPAWN_INTERVAL = 0.5f;              // Seconds between spawns
    constexpr float SPAWN_HEIGHT_OFFSET = 1.0f;         // Spawn above terrain
}

// ============================================================================
// Scoring Configuration
// ============================================================================

namespace Scoring {
    constexpr int32_t KILL_BASE_SCORE = 10;
    constexpr int32_t HEADSHOT_MULTIPLIER = 2;
    constexpr int32_t COMBO_MULTIPLIER = 1;             // Additional points per combo
    constexpr float COMBO_TIMEOUT = 3.0f;               // Seconds to maintain combo
    constexpr int32_t WAVE_CLEAR_BONUS = 100;
    constexpr int32_t PERFECT_WAVE_MULTIPLIER = 2;      // If no damage taken
    constexpr int32_t SPEED_BONUS_PER_SECOND = 5;       // Bonus for clearing wave quickly
}

// ============================================================================
// Arena Configuration
// ============================================================================

namespace Arena {
    constexpr float ARENA_RADIUS = 50.0f;               // Playable area radius
    constexpr float BOUNDARY_DAMAGE = 10.0f;            // Damage per second outside arena
    constexpr float BOUNDARY_FORCE = 50.0f;             // Push force towards center
    constexpr float TERRAIN_HEIGHT_VARIATION = 2.0f;    // Max height difference
}

// ============================================================================
// Difficulty Presets
// ============================================================================

enum class DifficultyPreset {
    Easy,
    Normal,
    Hard,
    Nightmare
};

struct DifficultyMultipliers {
    float playerHealth;
    float playerDamage;
    float enemyHealth;
    float enemyDamage;
    float enemySpeed;
    float enemyCount;
};

constexpr DifficultyMultipliers getDifficultyMultipliers(DifficultyPreset preset) {
    switch (preset) {
        case DifficultyPreset::Easy:
            return {1.5f, 1.5f, 0.7f, 0.7f, 0.8f, 0.7f};
        case DifficultyPreset::Normal:
            return {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        case DifficultyPreset::Hard:
            return {0.8f, 0.9f, 1.3f, 1.2f, 1.1f, 1.2f};
        case DifficultyPreset::Nightmare:
            return {0.5f, 0.8f, 2.0f, 1.5f, 1.3f, 1.5f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Calculate enemy count for a given wave
 */
constexpr uint32_t getEnemyCountForWave(uint32_t waveNumber) {
    float count = Waves::ENEMY_COUNT_BASE +
                  (waveNumber - 1) * Waves::ENEMY_COUNT_PER_WAVE;

    // Apply exponential scaling for higher waves
    if (waveNumber > 10) {
        float exponentialFactor = std::pow(Waves::ENEMY_COUNT_SCALING,
                                          static_cast<float>(waveNumber - 10));
        count *= exponentialFactor;
    }

    return static_cast<uint32_t>(count);
}

/**
 * @brief Calculate scaling multiplier for a given wave
 */
inline float getScalingMultiplier(uint32_t waveNumber, float scalingPerWave) {
    float multiplier = std::pow(scalingPerWave, static_cast<float>(waveNumber - 1));
    return std::min(multiplier, Waves::MAX_SCALING_MULTIPLIER);
}

/**
 * @brief Check if wave is a boss wave (with elite enemies)
 */
constexpr bool isBossWave(uint32_t waveNumber) {
    return waveNumber % Waves::ELITE_WAVE_FREQUENCY == 0;
}

} // namespace Balance
} // namespace Game

#endif // GAME_CONFIG_BALANCE_CONFIG_HPP

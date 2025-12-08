#pragma once

#include "../../engine/math/Vector.hpp"
#include <cstdint>

/**
 * ============================================================================
 * CENTRAL GAME CONFIGURATION
 * All gameplay constants and balance parameters in one place
 * ============================================================================
 */

namespace CatGame {
namespace GameplayConfig {

// ============================================================================
// PLAYER CONFIGURATION
// ============================================================================

namespace Player {
    // Movement
    constexpr float BASE_SPEED = 8.0f;                  // Base movement speed (m/s)
    constexpr float SPRINT_MULTIPLIER = 1.5f;           // Speed boost when sprinting
    constexpr float CROUCH_MULTIPLIER = 0.5f;           // Speed penalty when crouching
    constexpr float JUMP_FORCE = 10.0f;                 // Jump impulse force
    constexpr float AIR_CONTROL = 0.3f;                 // Movement control while airborne
    constexpr float ROTATION_SPEED = 180.0f;            // Degrees per second

    // Combat
    constexpr float BASE_HEALTH = 100.0f;               // Starting health
    constexpr float BASE_MANA = 100.0f;                 // Starting mana
    constexpr float BASE_STAMINA = 100.0f;              // Starting stamina
    constexpr float HEALTH_PER_LEVEL = 10.0f;           // Health gain per level
    constexpr float MANA_PER_LEVEL = 5.0f;              // Mana gain per level
    constexpr float BASE_ATTACK_DAMAGE = 10.0f;         // Base melee damage
    constexpr float BASE_DEFENSE = 5.0f;                // Base damage reduction
    constexpr float CRIT_CHANCE = 0.05f;                // 5% base crit chance
    constexpr float CRIT_MULTIPLIER = 2.0f;             // Critical hit damage multiplier

    // Regeneration
    constexpr float HEALTH_REGEN_RATE = 1.0f;           // HP per second (out of combat)
    constexpr float MANA_REGEN_RATE = 5.0f;             // MP per second
    constexpr float STAMINA_REGEN_RATE = 20.0f;         // Stamina per second
    constexpr float OUT_OF_COMBAT_DELAY = 5.0f;         // Seconds before health regen starts

    // Leveling
    constexpr int MAX_LEVEL = 50;                       // Maximum player level
    constexpr int BASE_XP_PER_LEVEL = 100;              // XP required for level 2
    constexpr float XP_SCALING = 1.5f;                  // XP curve multiplier per level
    constexpr int SKILL_POINTS_PER_LEVEL = 1;           // Skill points gained per level
}

// ============================================================================
// COMBAT CONFIGURATION
// ============================================================================

namespace Combat {
    // Timing
    constexpr float ATTACK_COOLDOWN = 0.5f;             // Seconds between basic attacks
    constexpr float HEAVY_ATTACK_COOLDOWN = 1.5f;       // Seconds between heavy attacks
    constexpr float BLOCK_STAMINA_DRAIN = 20.0f;        // Stamina per second while blocking
    constexpr float DODGE_STAMINA_COST = 25.0f;         // Stamina consumed per dodge
    constexpr float DODGE_COOLDOWN = 1.0f;              // Seconds between dodges
    constexpr float PARRY_WINDOW = 0.3f;                // Seconds of parry timing window

    // Damage
    constexpr float DAMAGE_VARIANCE = 0.1f;             // +/- 10% damage variation
    constexpr float BLOCK_DAMAGE_REDUCTION = 0.5f;      // 50% damage reduction when blocking
    constexpr float BACKSTAB_MULTIPLIER = 1.5f;         // Damage bonus from behind
    constexpr float HEADSHOT_MULTIPLIER = 2.0f;         // Damage bonus for headshots
    constexpr float ELEMENTAL_BONUS = 1.25f;            // Bonus vs weak element
    constexpr float ELEMENTAL_RESIST = 0.75f;           // Damage vs resistant element

    // Combo System
    constexpr float COMBO_WINDOW = 0.5f;                // Seconds to continue combo
    constexpr int MAX_COMBO_CHAIN = 10;                 // Maximum combo count
    constexpr float COMBO_DAMAGE_PER_HIT = 0.05f;       // 5% damage increase per hit
    constexpr float COMBO_DECAY_RATE = 1.0f;            // Combo count decay per second

    // Knockback
    constexpr float LIGHT_KNOCKBACK_FORCE = 5.0f;       // Light attacks
    constexpr float HEAVY_KNOCKBACK_FORCE = 15.0f;      // Heavy attacks
    constexpr float KNOCKBACK_RESISTANCE = 0.5f;        // Player knockback resistance
}

// ============================================================================
// ENEMY CONFIGURATION
// ============================================================================

namespace Enemy {
    // Basic Stats
    constexpr float BASE_HEALTH = 50.0f;                // Base enemy health
    constexpr float HEALTH_PER_WAVE = 10.0f;            // Health increase per wave
    constexpr float BASE_DAMAGE = 15.0f;                // Base enemy damage
    constexpr float DAMAGE_PER_WAVE = 2.0f;             // Damage increase per wave
    constexpr float BASE_SPEED = 5.0f;                  // Base movement speed

    // AI Behavior
    constexpr float AGGRO_RANGE = 20.0f;                // Detection range
    constexpr float ATTACK_RANGE = 2.0f;                // Melee attack range
    constexpr float CHASE_RANGE = 30.0f;                // How far they chase
    constexpr float GIVE_UP_TIME = 5.0f;                // Seconds before giving up chase
    constexpr float ATTACK_COOLDOWN = 1.5f;             // Seconds between attacks
    constexpr float CIRCLE_RADIUS = 5.0f;               // Radius for circling player

    // XP Rewards
    constexpr int BASE_XP_REWARD = 10;                  // Base XP per kill
    constexpr float XP_PER_WAVE_MULTIPLIER = 1.1f;      // XP scaling per wave
    constexpr float ELITE_XP_MULTIPLIER = 3.0f;         // Bonus XP for elite enemies
    constexpr float BOSS_XP_MULTIPLIER = 10.0f;         // Bonus XP for bosses
}

// ============================================================================
// WAVE SYSTEM CONFIGURATION
// ============================================================================

namespace Waves {
    constexpr int INITIAL_ENEMY_COUNT = 5;              // Enemies in first wave
    constexpr float ENEMY_COUNT_SCALING = 1.3f;         // Multiplier per wave
    constexpr int MAX_ENEMIES_PER_WAVE = 50;            // Hard cap on enemies
    constexpr float WAVE_BREAK_DURATION = 15.0f;        // Seconds between waves
    constexpr float SPAWN_INTERVAL = 2.0f;              // Seconds between spawns
    constexpr float SPAWN_RADIUS = 25.0f;               // Distance from player to spawn
    constexpr int ELITE_SPAWN_WAVE = 5;                 // First wave with elite enemies
    constexpr int BOSS_SPAWN_WAVE = 10;                 // First wave with boss
    constexpr int BOSS_FREQUENCY = 10;                  // Boss every N waves
}

// ============================================================================
// DAY/NIGHT CYCLE CONFIGURATION
// ============================================================================

namespace DayNight {
    constexpr float DAY_LENGTH_SECONDS = 600.0f;        // 10 minutes = 1 full day
    constexpr float DAWN_TIME = 0.25f;                  // Dawn at 6 AM (0.25 of day)
    constexpr float DUSK_TIME = 0.75f;                  // Dusk at 6 PM (0.75 of day)
    constexpr float TRANSITION_DURATION = 0.05f;        // 5% of day for dawn/dusk transition

    // Night Effects
    constexpr float NIGHT_ENEMY_MULTIPLIER = 1.5f;      // 50% more enemies at night
    constexpr float NIGHT_DAMAGE_MULTIPLIER = 1.2f;     // 20% more enemy damage
    constexpr float NIGHT_VISIBILITY_REDUCTION = 0.6f;  // 60% visibility
    constexpr float NIGHT_STEALTH_BONUS = 1.5f;         // Easier to hide at night

    // Lighting
    inline Engine::vec3 getDaySkyColor() { return Engine::vec3(0.5f, 0.7f, 1.0f); }
    inline Engine::vec3 getNightSkyColor() { return Engine::vec3(0.05f, 0.05f, 0.15f); }
    inline Engine::vec3 getSunColor() { return Engine::vec3(1.0f, 0.95f, 0.8f); }
    inline Engine::vec3 getMoonColor() { return Engine::vec3(0.7f, 0.7f, 1.0f); }
}

// ============================================================================
// MAGIC SYSTEM CONFIGURATION
// ============================================================================

namespace Magic {
    // Mana Costs
    constexpr float BASIC_SPELL_COST = 10.0f;           // Basic spell mana cost
    constexpr float ADVANCED_SPELL_COST = 25.0f;        // Advanced spell cost
    constexpr float ULTIMATE_SPELL_COST = 50.0f;        // Ultimate spell cost

    // Cooldowns
    constexpr float BASIC_SPELL_COOLDOWN = 2.0f;        // Seconds
    constexpr float ADVANCED_SPELL_COOLDOWN = 5.0f;     // Seconds
    constexpr float ULTIMATE_SPELL_COOLDOWN = 30.0f;    // Seconds

    // Damage
    constexpr float SPELL_DAMAGE_MULTIPLIER = 1.5f;     // Spell vs base attack damage
    constexpr float AOE_DAMAGE_FALLOFF = 0.5f;          // Damage at edge of AOE
    constexpr float DOT_TICK_INTERVAL = 1.0f;           // Damage over time tick rate

    // Elements
    constexpr int MAX_ELEMENT_LEVEL = 10;               // Maximum mastery level per element
    constexpr int XP_PER_ELEMENT_LEVEL = 500;           // XP to level element
}

// ============================================================================
// QUEST SYSTEM CONFIGURATION
// ============================================================================

namespace Quests {
    constexpr int MAX_ACTIVE_QUESTS = 10;               // Maximum concurrent quests
    constexpr int MAX_DAILY_QUESTS = 5;                 // Maximum daily quests
    constexpr float QUEST_MARKER_DISTANCE = 100.0f;     // Max distance to show markers

    // XP Rewards
    constexpr int MAIN_QUEST_XP_MULTIPLIER = 3;         // Main quest XP multiplier
    constexpr int SIDE_QUEST_XP_MULTIPLIER = 1;         // Side quest XP multiplier
    constexpr int DAILY_QUEST_XP_MULTIPLIER = 2;        // Daily quest XP multiplier
}

// ============================================================================
// CLAN SYSTEM CONFIGURATION
// ============================================================================

namespace Clans {
    constexpr int MAX_RANK = 5;                         // Leader rank
    constexpr int QUESTS_FOR_RANK_UP = 5;               // Quests needed to rank up
    constexpr int ENEMIES_FOR_RANK_UP = 50;             // Kills needed to rank up

    // Rank Bonuses (per rank level)
    constexpr float ATTACK_BONUS_PER_RANK = 0.05f;      // 5% per rank
    constexpr float DEFENSE_BONUS_PER_RANK = 0.05f;     // 5% per rank
    constexpr float SPEED_BONUS_PER_RANK = 0.03f;       // 3% per rank

    // Clan-specific bonuses
    namespace MistClan {
        constexpr float STEALTH_BONUS = 0.3f;           // 30% stealth
        constexpr float SPEED_BONUS = 0.1f;             // 10% speed
    }
    namespace StormClan {
        constexpr float SPEED_BONUS = 0.3f;             // 30% speed
        constexpr float ATTACK_SPEED_BONUS = 0.2f;      // 20% attack speed
    }
    namespace EmberClan {
        constexpr float ATTACK_BONUS = 0.3f;            // 30% attack
        constexpr float FIRE_DAMAGE_BONUS = 0.5f;       // 50% fire damage
    }
    namespace FrostClan {
        constexpr float DEFENSE_BONUS = 0.3f;           // 30% defense
        constexpr float ICE_RESISTANCE = 0.5f;          // 50% ice resistance
    }
}

// ============================================================================
// WEAPON SYSTEM CONFIGURATION
// ============================================================================

namespace Weapons {
    constexpr int MAX_WEAPON_LEVEL = 10;                // Maximum weapon skill level

    // Weapon Types
    namespace Melee {
        constexpr float DAMAGE = 15.0f;                 // Base melee damage
        constexpr float RANGE = 2.0f;                   // Attack range
        constexpr float SPEED = 1.0f;                   // Attacks per second
    }
    namespace Ranged {
        constexpr float DAMAGE = 12.0f;                 // Base projectile damage
        constexpr float RANGE = 30.0f;                  // Max effective range
        constexpr float SPEED = 0.8f;                   // Attacks per second
        constexpr float PROJECTILE_SPEED = 40.0f;       // Projectile velocity
    }
    namespace Magic {
        constexpr float DAMAGE = 20.0f;                 // Base magic damage
        constexpr float RANGE = 25.0f;                  // Spell range
        constexpr float SPEED = 0.5f;                   // Casts per second
    }
}

// ============================================================================
// INVENTORY CONFIGURATION
// ============================================================================

namespace Inventory {
    constexpr int MAX_INVENTORY_SIZE = 30;              // Number of slots
    constexpr int MAX_STACK_SIZE = 99;                  // Items per stack
    constexpr int QUICK_SLOT_COUNT = 8;                 // Hotbar slots
    constexpr float PICKUP_RADIUS = 2.0f;               // Auto-pickup range
}

// ============================================================================
// SAVE SYSTEM CONFIGURATION
// ============================================================================

namespace SaveSystem {
    constexpr int MAX_SAVE_SLOTS = 5;                   // Number of save slots
    constexpr float AUTO_SAVE_INTERVAL = 300.0f;        // Auto-save every 5 minutes
    constexpr bool ENABLE_AUTO_SAVE = true;             // Auto-save on/off
    constexpr int MAX_AUTOSAVE_FILES = 3;               // Rotating autosaves
}

// ============================================================================
// UI CONFIGURATION
// ============================================================================

namespace UI {
    constexpr float DAMAGE_NUMBER_DURATION = 1.5f;      // How long damage numbers float
    constexpr float DAMAGE_NUMBER_RISE_SPEED = 2.0f;    // Upward velocity
    constexpr float NOTIFICATION_DURATION = 3.0f;       // Notification display time
    constexpr float QUEST_POPUP_DURATION = 5.0f;        // Quest update popup time
    constexpr float WAVE_POPUP_DURATION = 3.0f;         // Wave start popup time
    constexpr float XP_BAR_ANIMATION_SPEED = 2.0f;      // XP bar fill speed
}

// ============================================================================
// PHYSICS CONFIGURATION
// ============================================================================

namespace Physics {
    constexpr float GRAVITY = -20.0f;                   // Gravity acceleration
    constexpr float GROUND_DRAG = 8.0f;                 // Ground friction
    constexpr float AIR_DRAG = 0.5f;                    // Air resistance
    constexpr float MAX_FALL_SPEED = 50.0f;             // Terminal velocity
    constexpr float SLOPE_LIMIT = 45.0f;                // Max walkable slope (degrees)
}

// ============================================================================
// AUDIO CONFIGURATION
// ============================================================================

namespace Audio {
    constexpr float MUSIC_FADE_DURATION = 2.0f;         // Seconds to fade music
    constexpr float FOOTSTEP_INTERVAL = 0.5f;           // Seconds between footsteps
    constexpr float MAX_AUDIO_DISTANCE = 50.0f;         // 3D audio cutoff distance
    constexpr float DOPPLER_SCALE = 0.1f;               // Doppler effect strength
}

// ============================================================================
// PERFORMANCE CONFIGURATION
// ============================================================================

namespace Performance {
    constexpr int MAX_PARTICLES = 10000;                // Maximum particle count
    constexpr int MAX_ENTITIES = 500;                   // Maximum active entities
    constexpr int MAX_LIGHTS = 32;                      // Maximum dynamic lights
    constexpr float ENTITY_CULLING_DISTANCE = 100.0f;   // Don't update far entities
    constexpr float LOD_DISTANCE_1 = 20.0f;             // First LOD switch
    constexpr float LOD_DISTANCE_2 = 50.0f;             // Second LOD switch
}

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================

namespace Debug {
    constexpr bool ENABLE_GOD_MODE = false;             // Invincibility
    constexpr bool ENABLE_INFINITE_MANA = false;        // No mana cost
    constexpr bool ENABLE_INFINITE_STAMINA = false;     // No stamina cost
    constexpr bool SHOW_COLLISION_SHAPES = false;       // Debug collision
    constexpr bool SHOW_AI_DEBUG = false;               // Show AI state
    constexpr bool SHOW_PERFORMANCE_STATS = false;      // FPS, frame time, etc.
    constexpr float TIME_SCALE = 1.0f;                  // Game speed multiplier
}

} // namespace GameplayConfig
} // namespace CatGame

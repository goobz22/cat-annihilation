#pragma once

#include <map>
#include <string>

namespace CatGame {

/**
 * XP Tables and Stat Growth Configuration
 *
 * This file contains all the leveling formulas, XP requirements,
 * and stat growth curves for the cat and weapon skill systems.
 */

// ============================================================================
// CAT LEVELING
// ============================================================================

/**
 * XP required to reach each level (1-50)
 * Formula: XP = 100 * (1.5 ^ (level - 1))
 * Provides exponential growth that slows endgame grind
 */
const std::map<int, int> CAT_XP_TABLE = {
    {1, 100}, {2, 150}, {3, 225}, {4, 340}, {5, 500},
    {6, 750}, {7, 1125}, {8, 1700}, {9, 2500}, {10, 3750},
    {11, 5625}, {12, 8440}, {13, 12660}, {14, 18990}, {15, 28485},
    {16, 42728}, {17, 64092}, {18, 96138}, {19, 144207}, {20, 216311},
    {21, 324467}, {22, 486701}, {23, 730052}, {24, 1095078}, {25, 1642617},
    {26, 2463926}, {27, 3695889}, {28, 5543834}, {29, 8315751}, {30, 12473627},
    {31, 18710441}, {32, 28065662}, {33, 42098493}, {34, 63147740}, {35, 94721610},
    {36, 142082415}, {37, 213123623}, {38, 319685435}, {39, 479528153}, {40, 719292230},
    {41, 1078938345}, {42, 1618407518}, {43, 2427611277}, {44, 3641416916}, {45, 5462125374},
    {46, 8193188061}, {47, 12289782092}, {48, 18434673138}, {49, 27652009707}, {50, 41478014561}
};

/**
 * Stat growth per level
 */
struct StatGrowth {
    int healthPerLevel = 10;      // HP increases by 10 per level
    int attackPerLevel = 2;       // Attack increases by 2 per level
    int defensePerLevel = 1;      // Defense increases by 1 per level
    int speedPerLevel = 1;        // Speed increases by 1 per level

    // Derived stat growth
    float critChancePerLevel = 0.001f;     // +0.1% crit chance per level
    float dodgeChancePerLevel = 0.001f;    // +0.1% dodge chance per level
};

/**
 * Ability unlock levels
 */
struct AbilityUnlockLevels {
    int regeneration = 5;       // Level 5: Passive HP regen
    int agility = 10;           // Level 10: Double jump, faster dodge
    int nineLives = 15;         // Level 15: Revive once per battle
    int predatorInstinct = 20;  // Level 20: See enemy HP, weaknesses
    int alphaStrike = 25;       // Level 25: Critical hits deal 3x damage
};

// ============================================================================
// WEAPON SKILL LEVELING
// ============================================================================

/**
 * Weapon skill XP requirements (1-20)
 * Formula: XP = 50 * (1.4 ^ (level - 1))
 * Faster progression than cat level to encourage weapon variety
 */
const std::map<int, int> WEAPON_SKILL_XP_TABLE = {
    {1, 50}, {2, 70}, {3, 98}, {4, 137}, {5, 192},
    {6, 269}, {7, 377}, {8, 528}, {9, 739}, {10, 1035},
    {11, 1449}, {12, 2029}, {13, 2841}, {14, 3977}, {15, 5568},
    {16, 7795}, {17, 10913}, {18, 15278}, {19, 21389}, {20, 29945}
};

/**
 * Weapon skill stat bonuses per level
 */
struct WeaponSkillGrowth {
    float damagePerLevel = 0.05f;      // +5% damage per level
    float speedPerLevel = 0.02f;       // +2% attack speed per level
    float critBonusPerLevel = 0.002f;  // +0.2% crit chance per level
};

// ============================================================================
// ELEMENTAL MAGIC LEVELING
// ============================================================================

/**
 * Elemental magic skill XP requirements (1-15)
 * Same as weapon skills but capped at 15
 */
const std::map<int, int> ELEMENTAL_XP_TABLE = {
    {1, 50}, {2, 70}, {3, 98}, {4, 137}, {5, 192},
    {6, 269}, {7, 377}, {8, 528}, {9, 739}, {10, 1035},
    {11, 1449}, {12, 2029}, {13, 2841}, {14, 3977}, {15, 5568}
};

/**
 * Elemental magic bonuses per level
 */
struct ElementalSkillGrowth {
    float damagePerLevel = 0.08f;       // +8% elemental damage per level
    float durationPerLevel = 0.05f;     // +5% effect duration per level
    float radiusPerLevel = 0.03f;       // +3% AoE radius per level
};

// ============================================================================
// XP REWARDS
// ============================================================================

/**
 * Enemy kill XP rewards based on enemy type
 */
struct EnemyXPRewards {
    static const int SMALL_DOG = 10;        // Small basic enemy
    static const int MEDIUM_DOG = 25;       // Medium strength enemy
    static const int LARGE_DOG = 50;        // Large enemy
    static const int ELITE_DOG = 100;       // Elite/boss enemy
    static const int BOSS = 500;            // Major boss
};

/**
 * Weapon XP per hit (scales with damage dealt)
 */
struct WeaponXPRewards {
    static constexpr float XP_PER_DAMAGE = 0.1f;  // 0.1 XP per 1 damage dealt
    static const int MIN_XP = 5;                   // Minimum XP per hit
    static const int MAX_XP = 20;                  // Maximum XP per hit
};

/**
 * Quest XP rewards
 */
struct QuestXPRewards {
    static const int SIMPLE_QUEST = 50;      // Simple task
    static const int MEDIUM_QUEST = 150;     // Medium complexity
    static const int HARD_QUEST = 500;       // Difficult quest
    static const int EPIC_QUEST = 1000;      // Epic storyline quest
};

/**
 * Discovery XP rewards
 */
struct DiscoveryXPRewards {
    static const int LOCATION = 25;          // Found new location
    static const int SECRET_AREA = 100;      // Found secret area
    static const int TREASURE = 50;          // Found treasure
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Get XP required for cat to reach next level
 * @param currentLevel Current cat level (1-50)
 * @return XP required to level up, or -1 if max level
 */
inline int getCatXPToNextLevel(int currentLevel) {
    auto it = CAT_XP_TABLE.find(currentLevel);
    if (it != CAT_XP_TABLE.end()) {
        return it->second;
    }
    return -1; // Max level reached
}

/**
 * Get XP required for weapon skill to reach next level
 * @param currentLevel Current skill level (1-20)
 * @return XP required to level up, or -1 if max level
 */
inline int getWeaponSkillXPToNextLevel(int currentLevel) {
    auto it = WEAPON_SKILL_XP_TABLE.find(currentLevel);
    if (it != WEAPON_SKILL_XP_TABLE.end()) {
        return it->second;
    }
    return -1; // Max level reached
}

/**
 * Get XP required for elemental skill to reach next level
 * @param currentLevel Current skill level (1-15)
 * @return XP required to level up, or -1 if max level
 */
inline int getElementalXPToNextLevel(int currentLevel) {
    auto it = ELEMENTAL_XP_TABLE.find(currentLevel);
    if (it != ELEMENTAL_XP_TABLE.end()) {
        return it->second;
    }
    return -1; // Max level reached
}

/**
 * Calculate weapon XP reward based on damage dealt
 * @param damageDealt Amount of damage dealt with the weapon
 * @return XP to award (clamped between MIN_XP and MAX_XP)
 */
inline int calculateWeaponXP(float damageDealt) {
    int xp = static_cast<int>(damageDealt * WeaponXPRewards::XP_PER_DAMAGE);
    return std::max(WeaponXPRewards::MIN_XP,
                    std::min(WeaponXPRewards::MAX_XP, xp));
}

} // namespace CatGame

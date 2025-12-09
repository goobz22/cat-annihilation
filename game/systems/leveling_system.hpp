#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include "xp_tables.hpp"
#include "elemental_magic.hpp"  // For ElementType enum

namespace CatGame {

// Forward declarations
class ComboSystem;

// ElementType is now defined in elemental_magic.hpp
// Values: Water, Air, Earth, Fire, COUNT

/**
 * Cat abilities unlocked at specific levels
 */
struct CatAbilities {
    bool regeneration = false;     // Level 5: Passive HP regen (1% max HP per second out of combat)
    bool agility = false;          // Level 10: Double jump, 50% faster dodge
    bool nineLives = false;        // Level 15: Revive once per battle at 50% HP
    bool predatorInstinct = false; // Level 20: See enemy HP bars, levels, weaknesses
    bool alphaStrike = false;      // Level 25: Critical hits deal 3x damage instead of 1.5x

    // Nine Lives state (resets each battle)
    bool nineLivesUsed = false;
};

/**
 * Cat stats that scale with level
 */
struct CatStats {
    // Leveling
    int level = 1;
    int xp = 0;
    int xpToNextLevel = 100;

    // Base stats (increase with level)
    int maxHealth = 100;
    int attack = 10;
    int defense = 5;
    int speed = 10;

    // Derived stats
    float critChance = 0.05f;        // 5% base crit chance
    float critMultiplier = 1.5f;     // 1.5x damage on crit (3x with Alpha Strike)
    float dodgeChance = 0.05f;       // 5% base dodge chance

    // Abilities
    CatAbilities abilities;
};

/**
 * Individual weapon skill progression
 */
struct WeaponSkill {
    int level = 1;
    int xp = 0;
    int xpToNextLevel = 50;

    // Skill bonuses
    float damageMultiplier = 1.0f;   // Starts at 1.0, increases with level
    float speedMultiplier = 1.0f;    // Starts at 1.0, increases with level
    float critBonus = 0.0f;          // Additional crit chance from skill

    // Total hits with this weapon (for statistics)
    int totalHits = 0;
    int totalDamageDealt = 0;
};

/**
 * All weapon skills for the player
 */
struct WeaponSkills {
    WeaponSkill sword;
    WeaponSkill bow;
    WeaponSkill staff;

    // Elemental magic skills (used with staff)
    std::map<ElementType, WeaponSkill> elementalMagic;

    /**
     * Get weapon skill by name
     */
    WeaponSkill* getSkill(const std::string& weaponType) {
        if (weaponType == "sword") return &sword;
        if (weaponType == "bow") return &bow;
        if (weaponType == "staff") return &staff;
        return nullptr;
    }

    const WeaponSkill* getSkill(const std::string& weaponType) const {
        if (weaponType == "sword") return &sword;
        if (weaponType == "bow") return &bow;
        if (weaponType == "staff") return &staff;
        return nullptr;
    }
};

/**
 * Leveling System - Manages cat and weapon skill progression
 *
 * Features:
 * - Cat leveling with stat growth and ability unlocks
 * - Individual weapon skill progression
 * - Elemental magic skill progression
 * - XP gain from combat, quests, and discoveries
 * - Level-up notifications and callbacks
 * - Regeneration, Nine Lives, and other ability implementations
 */
class LevelingSystem {
public:
    LevelingSystem();
    ~LevelingSystem() = default;

    /**
     * Initialize the leveling system
     */
    void initialize();

    /**
     * Update per frame (handles regeneration, etc.)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    // ========================================================================
    // CAT LEVELING
    // ========================================================================

    /**
     * Add XP to the cat and check for level up
     * @param amount XP amount to add
     * @return True if cat leveled up
     */
    bool addXP(int amount);

    /**
     * Force level up (internal use)
     */
    void levelUp();

    /**
     * Get current cat level
     */
    int getLevel() const { return stats_.level; }

    /**
     * Get current XP amount
     */
    int getXP() const { return stats_.xp; }

    /**
     * Get XP required to reach next level
     */
    int getXPToNextLevel() const { return stats_.xpToNextLevel; }

    /**
     * Get XP progress as a percentage (0.0 - 1.0)
     */
    float getXPProgress() const;

    /**
     * Set level directly (for save loading)
     * @param level Level to set
     */
    void setLevel(int level);

    /**
     * Set XP directly (for save loading)
     * @param xp XP amount to set
     */
    void setXP(int xp);

    // ========================================================================
    // CAT STATS
    // ========================================================================

    /**
     * Get current cat stats
     */
    const CatStats& getStats() const { return stats_; }

    /**
     * Get mutable cat stats (for external modification)
     */
    CatStats& getStatsRef() { return stats_; }

    /**
     * Recalculate stats based on current level
     * Called automatically on level up
     */
    void recalculateStats();

    /**
     * Get effective attack with all bonuses
     */
    int getEffectiveAttack() const;

    /**
     * Get effective defense with all bonuses
     */
    int getEffectiveDefense() const;

    /**
     * Get effective speed with all bonuses
     */
    int getEffectiveSpeed() const;

    /**
     * Get effective crit chance (base + bonuses)
     */
    float getEffectiveCritChance() const;

    // ========================================================================
    // ABILITIES
    // ========================================================================

    /**
     * Get cat abilities
     */
    const CatAbilities& getAbilities() const { return stats_.abilities; }

    /**
     * Check if a specific ability is unlocked
     * @param abilityName Name of ability (regeneration, agility, etc.)
     */
    bool hasAbility(const std::string& abilityName) const;

    /**
     * Check for newly unlocked abilities after level up
     * Triggers callbacks for each new ability
     */
    void checkAbilityUnlocks();

    // Regeneration ability (Level 5)
    void applyRegeneration(float deltaTime, float& currentHealth, float maxHealth);

    // Nine Lives ability (Level 15)
    bool canRevive() const;
    void useRevive(float& currentHealth, float maxHealth);
    void resetRevive(); // Call at start of each battle

    // Predator Instinct (Level 20)
    bool canSeeEnemyDetails() const { return stats_.abilities.predatorInstinct; }

    // Alpha Strike (Level 25)
    float getCritMultiplier() const;

    // Agility (Level 10)
    bool hasDoubleJump() const { return stats_.abilities.agility; }
    float getDodgeSpeedMultiplier() const { return stats_.abilities.agility ? 1.5f : 1.0f; }

    // ========================================================================
    // WEAPON SKILLS
    // ========================================================================

    /**
     * Add XP to a weapon skill
     * @param weaponType "sword", "bow", or "staff"
     * @param amount XP amount to add
     * @return True if weapon skill leveled up
     */
    bool addWeaponXP(const std::string& weaponType, int amount);

    /**
     * Add weapon XP based on damage dealt (automatic calculation)
     * @param weaponType Weapon that dealt the damage
     * @param damageDealt Amount of damage dealt
     * @return True if weapon skill leveled up
     */
    bool addWeaponXPFromDamage(const std::string& weaponType, float damageDealt);

    /**
     * Get weapon skill level
     * @param weaponType Weapon type name
     * @return Weapon skill level (1-20)
     */
    int getWeaponLevel(const std::string& weaponType) const;

    /**
     * Get weapon damage multiplier from skill level
     * @param weaponType Weapon type name
     * @return Damage multiplier (1.0 + level * 0.05)
     */
    float getWeaponDamageMultiplier(const std::string& weaponType) const;

    /**
     * Get weapon attack speed multiplier from skill level
     * @param weaponType Weapon type name
     * @return Speed multiplier (1.0 + level * 0.02)
     */
    float getWeaponSpeedMultiplier(const std::string& weaponType) const;

    /**
     * Get weapon crit bonus from skill level
     * @param weaponType Weapon type name
     * @return Additional crit chance (level * 0.002)
     */
    float getWeaponCritBonus(const std::string& weaponType) const;

    /**
     * Get all weapon skills
     */
    const WeaponSkills& getWeaponSkills() const { return weaponSkills_; }

    // ========================================================================
    // ELEMENTAL MAGIC SKILLS
    // ========================================================================

    /**
     * Add XP to an elemental magic skill
     * @param element Element type
     * @param amount XP amount to add
     * @return True if elemental skill leveled up
     */
    bool addElementalXP(ElementType element, int amount);

    /**
     * Get elemental skill level
     * @param element Element type
     * @return Skill level (1-15)
     */
    int getElementalLevel(ElementType element) const;

    /**
     * Get elemental damage multiplier
     * @param element Element type
     * @return Damage multiplier (1.0 + level * 0.08)
     */
    float getElementalDamageMultiplier(ElementType element) const;

    /**
     * Get elemental effect duration multiplier
     * @param element Element type
     * @return Duration multiplier (1.0 + level * 0.05)
     */
    float getElementalDurationMultiplier(ElementType element) const;

    /**
     * Get elemental AoE radius multiplier
     * @param element Element type
     * @return Radius multiplier (1.0 + level * 0.03)
     */
    float getElementalRadiusMultiplier(ElementType element) const;

    // ========================================================================
    // COMBO SYSTEM INTEGRATION
    // ========================================================================

    /**
     * Set combo system reference (optional)
     * Allows leveling system to notify combo system of skill unlocks
     */
    void setComboSystem(ComboSystem* comboSystem) { comboSystem_ = comboSystem; }

    /**
     * Get unlocked combos for a weapon type
     * @param weaponType Weapon type name
     * @return List of unlocked combo names
     */
    std::vector<std::string> getUnlockedCombos(const std::string& weaponType) const;

    // ========================================================================
    // CALLBACKS
    // ========================================================================

    /**
     * Set callback for cat level up events
     * @param callback Function called with (newLevel)
     */
    void setLevelUpCallback(std::function<void(int)> callback) {
        onLevelUp_ = callback;
    }

    /**
     * Set callback for ability unlock events
     * @param callback Function called with (abilityName, level)
     */
    void setAbilityUnlockCallback(std::function<void(const std::string&, int)> callback) {
        onAbilityUnlock_ = callback;
    }

    /**
     * Set callback for weapon skill level up events
     * @param callback Function called with (weaponType, newLevel)
     */
    void setWeaponLevelUpCallback(std::function<void(const std::string&, int)> callback) {
        onWeaponLevelUp_ = callback;
    }

    // ========================================================================
    // COMBAT STATE (for regeneration)
    // ========================================================================

    /**
     * Notify system that combat has started (disables regeneration)
     */
    void enterCombat() { inCombat_ = true; }

    /**
     * Notify system that combat has ended (enables regeneration after delay)
     */
    void exitCombat() {
        inCombat_ = false;
        timeSinceCombat_ = 0.0f;
    }

    /**
     * Check if currently in combat
     */
    bool isInCombat() const { return inCombat_; }

private:
    // Cat stats and progression
    CatStats stats_;

    // Weapon skills
    WeaponSkills weaponSkills_;

    // Combat state (for regeneration)
    bool inCombat_ = false;
    float timeSinceCombat_ = 0.0f;
    static constexpr float REGEN_DELAY = 3.0f; // 3 seconds out of combat before regen starts

    // Stat growth configuration
    StatGrowth statGrowth_;
    AbilityUnlockLevels abilityUnlocks_;

    // Combo system reference (optional)
    ComboSystem* comboSystem_ = nullptr;

    // Callbacks
    std::function<void(int)> onLevelUp_;
    std::function<void(const std::string&, int)> onAbilityUnlock_;
    std::function<void(const std::string&, int)> onWeaponLevelUp_;

    // Internal helpers
    void levelUpWeaponSkill(WeaponSkill& skill, const std::string& weaponType);
    void levelUpElementalSkill(WeaponSkill& skill, ElementType element);
    ElementType stringToElementType(const std::string& elementName) const;
    std::string elementTypeToString(ElementType element) const;
};

} // namespace CatGame

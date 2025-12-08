#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace CatGame {

/**
 * Story-specific skills that progress throughout the campaign
 * These skills affect gameplay mechanics and unlock new abilities
 */
struct StorySkills {
    // Core survival skills
    int hunting = 0;      // Affects food gathering and attack damage bonus
    int fighting = 0;     // Combat effectiveness and combo unlocks
    int healing = 0;      // Self-heal capability and potion effectiveness
    int tracking = 0;     // Enemy detection range and minimap visibility
    int swimming = 0;     // Water traversal speed and stamina consumption
    int climbing = 0;     // Vertical mobility and wall-running duration

    // Advanced skills
    int stealth = 0;      // Enemy aggro range reduction
    int leadership = 0;   // NPC ally bonuses and recruitment
    int mysticism = 0;    // Elemental power effectiveness

    // Skill caps
    static constexpr int MAX_SKILL_LEVEL = 100;
    static constexpr int MASTER_THRESHOLD = 80;
    static constexpr int EXPERT_THRESHOLD = 60;
    static constexpr int ADEPT_THRESHOLD = 40;
    static constexpr int NOVICE_THRESHOLD = 20;

    /**
     * Add experience to a specific skill
     * @param skillName Name of the skill to level up
     * @param experience Amount of XP to add
     * @return New skill level
     */
    int addSkillExperience(const std::string& skillName, int experience);

    /**
     * Get skill level by name
     * @param skillName Name of the skill
     * @return Current skill level
     */
    int getSkillLevel(const std::string& skillName) const;

    /**
     * Set skill level directly (for cheats/debug)
     * @param skillName Name of the skill
     * @param level Level to set
     */
    void setSkillLevel(const std::string& skillName, int level);

    /**
     * Check if player is a master of a skill
     */
    bool isMaster(const std::string& skillName) const;

    /**
     * Get total skill points allocated
     */
    int getTotalSkillPoints() const;

    /**
     * Get skill tier name for display
     */
    static const char* getSkillTier(int level);

    /**
     * Calculate hunting damage bonus
     * @return Multiplicative damage bonus (1.0 = no bonus)
     */
    float getHuntingDamageBonus() const;

    /**
     * Calculate fighting combo multiplier
     * @return Combo damage multiplier
     */
    float getFightingComboMultiplier() const;

    /**
     * Calculate healing effectiveness
     * @return Health restoration multiplier
     */
    float getHealingEffectiveness() const;

    /**
     * Calculate tracking range
     * @return Enemy detection radius in world units
     */
    float getTrackingRange() const;

    /**
     * Calculate swimming speed multiplier
     * @return Speed multiplier in water
     */
    float getSwimmingSpeedMultiplier() const;

    /**
     * Calculate climbing stamina cost reduction
     * @return Percentage reduction (0.0 to 1.0)
     */
    float getClimbingStaminaReduction() const;

    /**
     * Calculate stealth aggro range reduction
     * @return Percentage reduction (0.0 to 1.0)
     */
    float getStealthAggroReduction() const;

    /**
     * Calculate leadership ally bonus
     * @return Ally damage/defense multiplier
     */
    float getLeadershipAllyBonus() const;

    /**
     * Calculate mysticism elemental bonus
     * @return Elemental damage multiplier
     */
    float getMysticismElementalBonus() const;

    /**
     * Reset all skills to zero (for new game+)
     */
    void reset();

    /**
     * Serialize skills for save system
     */
    std::string serialize() const;

    /**
     * Deserialize skills from save data
     */
    bool deserialize(const std::string& data);
};

/**
 * Skill progression event data
 */
struct SkillProgressionEvent {
    std::string skillName;
    int previousLevel;
    int newLevel;
    bool reachedNewTier;
    const char* tierName;
};

/**
 * Skill unlock requirements
 */
struct SkillUnlock {
    std::string skillName;
    int requiredLevel;
    std::string unlockDescription;
    bool unlocked = false;
};

/**
 * Manages skill progression and unlocks
 */
class SkillProgressionManager {
public:
    SkillProgressionManager();

    /**
     * Register a callback for skill level-up events
     */
    void onSkillLevelUp(void (*callback)(const SkillProgressionEvent&));

    /**
     * Process skill gain and trigger events
     */
    SkillProgressionEvent processSkillGain(StorySkills& skills, const std::string& skillName, int xp);

    /**
     * Check if any unlocks are available
     */
    bool checkUnlocks(const StorySkills& skills);

    /**
     * Get list of available unlocks
     */
    const std::vector<SkillUnlock>& getUnlocks() const { return unlocks_; }

    /**
     * Register a new skill unlock
     */
    void registerUnlock(const std::string& skillName, int requiredLevel, const std::string& description);

private:
    std::vector<SkillUnlock> unlocks_;
    void (*levelUpCallback_)(const SkillProgressionEvent&) = nullptr;

    void initializeDefaultUnlocks();
};

} // namespace CatGame

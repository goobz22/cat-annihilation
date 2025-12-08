#pragma once

#include "../systems/story_mode.hpp"
#include "../systems/story_skills.hpp"

namespace CatGame {

/**
 * Story Player Component
 * Attached to the player entity in story mode
 */
struct StoryPlayerComponent {
    Clan clan = Clan::MistClan;
    ClanRank rank = ClanRank::Outsider;
    int level = 1;
    int experience = 0;
    int skillPoints = 0;

    // Active story state
    bool inStoryMode = false;
    int currentChapter = 0;
    std::string currentObjective = "";

    // Stats tracking
    int enemiesDefeatedThisSession = 0;
    int questsCompletedThisSession = 0;
    float sessionPlayTime = 0.0f;

    /**
     * Check if player can level up
     */
    bool canLevelUp() const {
        return experience >= StoryModeHelpers::calculateExperienceForLevel(level + 1);
    }

    /**
     * Get experience progress (0.0 to 1.0)
     */
    float getExperienceProgress() const {
        int current = experience;
        int required = StoryModeHelpers::calculateExperienceForLevel(level + 1);
        return static_cast<float>(current) / static_cast<float>(required);
    }
};

/**
 * Clan Member Component
 * Marks an entity as belonging to a specific clan
 */
struct ClanMemberComponent {
    Clan clan;
    ClanRank rank = ClanRank::Warrior;
    std::string clanMemberName;
    bool isHostileToPlayer = false;

    // Behavior flags
    bool canBeRecruited = false;
    bool isQuestGiver = false;
    bool isMentor = false;
    bool isTrainer = false;

    /**
     * Check if this entity is friendly to another clan
     */
    bool isFriendlyTo(Clan otherClan) const {
        return clan == otherClan;
    }

    /**
     * Get display title
     */
    std::string getTitle() const {
        return std::string(StoryModeState::getRankName(rank)) + " of " +
               StoryModeState::getClanName(clan);
    }
};

/**
 * Quest Giver Component
 * Entities that can provide quests to the player
 */
struct QuestGiverComponent {
    std::vector<std::string> availableQuests;
    std::vector<std::string> completedQuests;
    int questsGiven = 0;

    // Requirements
    ClanRank minimumRank = ClanRank::Outsider;
    Clan requiredClan = Clan::MistClan;
    bool anyPlayerClan = true; // If true, ignores requiredClan

    /**
     * Check if quest is available
     */
    bool hasQuest(const std::string& questId) const {
        return std::find(availableQuests.begin(), availableQuests.end(), questId)
               != availableQuests.end();
    }

    /**
     * Check if quest was completed
     */
    bool isQuestCompleted(const std::string& questId) const {
        return std::find(completedQuests.begin(), completedQuests.end(), questId)
               != completedQuests.end();
    }

    /**
     * Mark quest as completed
     */
    void completeQuest(const std::string& questId) {
        // Remove from available
        auto it = std::find(availableQuests.begin(), availableQuests.end(), questId);
        if (it != availableQuests.end()) {
            availableQuests.erase(it);
        }

        // Add to completed
        if (!isQuestCompleted(questId)) {
            completedQuests.push_back(questId);
            questsGiven++;
        }
    }
};

/**
 * Elemental Affinity Component
 * Entities with elemental powers
 */
struct ElementalAffinityComponent {
    ElementType primaryElement;
    float elementalPower = 1.0f;      // Multiplier for elemental damage
    float elementalResistance = 0.0f; // Resistance to same element (0.0 to 1.0)

    // Status effect durations (in seconds)
    float burnDuration = 0.0f;        // Fire DoT
    float freezeDuration = 0.0f;      // Ice slow/stun
    float shockDuration = 0.0f;       // Lightning chain damage
    float shadowDuration = 0.0f;      // Shadow stealth bonus

    /**
     * Apply elemental effect
     */
    void applyElementalEffect(ElementType element, float duration) {
        switch (element) {
            case ElementType::Fire:
                burnDuration = std::max(burnDuration, duration);
                break;
            case ElementType::Ice:
                freezeDuration = std::max(freezeDuration, duration);
                break;
            case ElementType::Lightning:
                shockDuration = std::max(shockDuration, duration);
                break;
            case ElementType::Shadow:
                shadowDuration = std::max(shadowDuration, duration);
                break;
        }
    }

    /**
     * Update elemental effects
     */
    void update(float dt) {
        if (burnDuration > 0.0f) burnDuration -= dt;
        if (freezeDuration > 0.0f) freezeDuration -= dt;
        if (shockDuration > 0.0f) shockDuration -= dt;
        if (shadowDuration > 0.0f) shadowDuration -= dt;

        burnDuration = std::max(0.0f, burnDuration);
        freezeDuration = std::max(0.0f, freezeDuration);
        shockDuration = std::max(0.0f, shockDuration);
        shadowDuration = std::max(0.0f, shadowDuration);
    }

    /**
     * Check if burning
     */
    bool isBurning() const { return burnDuration > 0.0f; }

    /**
     * Check if frozen
     */
    bool isFrozen() const { return freezeDuration > 0.0f; }

    /**
     * Check if shocked
     */
    bool isShocked() const { return shockDuration > 0.0f; }

    /**
     * Check if shadowed
     */
    bool isShadowed() const { return shadowDuration > 0.0f; }

    /**
     * Calculate damage reduction from resistance
     */
    float getResistanceMultiplier(ElementType attackElement) const {
        if (attackElement == primaryElement) {
            return 1.0f - elementalResistance;
        }
        return 1.0f;
    }
};

/**
 * Territory Occupant Component
 * Tracks which territory an entity is currently in
 */
struct TerritoryOccupantComponent {
    Territory* currentTerritory = nullptr;
    Territory* previousTerritory = nullptr;
    float timeInTerritory = 0.0f;

    // Territory bonuses (cached)
    float territoryDamageBonus = 1.0f;
    float territoryDefenseBonus = 1.0f;
    float territorySpeedBonus = 1.0f;

    /**
     * Update current territory
     */
    void updateTerritory(Territory* newTerritory, Clan playerClan) {
        if (newTerritory != currentTerritory) {
            previousTerritory = currentTerritory;
            currentTerritory = newTerritory;
            timeInTerritory = 0.0f;

            // Update bonuses
            if (currentTerritory) {
                float damage = 1.0f, defense = 1.0f, speed = 1.0f;
                currentTerritory->applyTerritoryEffects(damage, defense, speed, playerClan);
                territoryDamageBonus = damage;
                territoryDefenseBonus = defense;
                territorySpeedBonus = speed;
            } else {
                territoryDamageBonus = 1.0f;
                territoryDefenseBonus = 1.0f;
                territorySpeedBonus = 1.0f;
            }
        }
    }

    /**
     * Check if in a specific territory type
     */
    bool isInTerritoryType(TerritoryType type) const {
        return currentTerritory && currentTerritory->type == type;
    }

    /**
     * Check if in hostile territory
     */
    bool isInHostileTerritory(Clan playerClan) const {
        return currentTerritory &&
               currentTerritory->controllingClan != playerClan &&
               currentTerritory->controlStatus == ControlStatus::Controlled;
    }
};

/**
 * Story Skills Component
 * Attached to entities that have skill progression
 */
struct StorySkillsComponent {
    StorySkills skills;
    int unallocatedSkillPoints = 0;

    // Skill XP multipliers
    float huntingXPMultiplier = 1.0f;
    float fightingXPMultiplier = 1.0f;
    float healingXPMultiplier = 1.0f;
    float trackingXPMultiplier = 1.0f;

    /**
     * Allocate a skill point
     */
    bool allocateSkillPoint(const std::string& skillName) {
        if (unallocatedSkillPoints > 0) {
            skills.addSkillExperience(skillName, 1);
            unallocatedSkillPoints--;
            return true;
        }
        return false;
    }

    /**
     * Get total allocated skill points
     */
    int getTotalAllocatedPoints() const {
        return skills.getTotalSkillPoints();
    }
};

/**
 * Mystical Connection Component
 * Entities with mystical power connections
 */
struct MysticalConnectionComponent {
    int connectionLevel = 0;        // 0-100
    float mysticalPower = 1.0f;     // Power multiplier
    bool canAccessSacredSites = false;

    // Mystical abilities unlocked
    bool hasElementalBurst = false;
    bool hasMysticShield = false;
    bool canSenseEnemies = false;
    bool hasAscensionForm = false;

    /**
     * Increase mystical connection
     */
    void increaseConnection(int amount) {
        connectionLevel = std::min(100, connectionLevel + amount);
        mysticalPower = 1.0f + (connectionLevel / 100.0f) * 2.0f; // Up to 3x at max

        // Unlock abilities at thresholds
        if (connectionLevel >= 25) hasElementalBurst = true;
        if (connectionLevel >= 50) hasMysticShield = true;
        if (connectionLevel >= 75) canSenseEnemies = true;
        if (connectionLevel >= 100) hasAscensionForm = true;
    }

    /**
     * Check if can access sacred territories
     */
    bool canAccessSacred() const {
        return connectionLevel >= 50;
    }
};

/**
 * Mentor Component
 * NPCs that can teach and train the player
 */
struct MentorComponent {
    std::string mentorName;
    std::string specialization; // "Combat", "Stealth", "Mysticism", etc.
    Clan clan;

    // Training bonuses
    float trainingEfficiency = 1.0f; // XP multiplier for taught skills
    int maxTrainingLevel = 50;       // Maximum skill level this mentor can teach

    // Trainable skills
    std::vector<std::string> trainableSkills;

    // Mentorship state
    bool isCurrentMentor = false;
    int lessonsGiven = 0;
    float relationshipLevel = 0.0f; // 0.0 to 100.0

    /**
     * Check if can train a specific skill
     */
    bool canTrain(const std::string& skillName, int currentLevel) const {
        bool skillMatches = std::find(trainableSkills.begin(), trainableSkills.end(), skillName)
                           != trainableSkills.end();
        return skillMatches && currentLevel < maxTrainingLevel;
    }

    /**
     * Improve relationship
     */
    void improveRelationship(float amount) {
        relationshipLevel = std::min(100.0f, relationshipLevel + amount);
        trainingEfficiency = 1.0f + (relationshipLevel / 100.0f) * 0.5f; // Up to 1.5x at max
    }

    /**
     * Get relationship tier
     */
    const char* getRelationshipTier() const {
        if (relationshipLevel >= 80.0f) return "Trusted Mentor";
        if (relationshipLevel >= 60.0f) return "Respected Teacher";
        if (relationshipLevel >= 40.0f) return "Friendly Guide";
        if (relationshipLevel >= 20.0f) return "Acquaintance";
        return "Stranger";
    }
};

} // namespace CatGame

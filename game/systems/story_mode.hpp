#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "story_skills.hpp"
#include "clan_territory.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace CatGame {

/**
 * Clan types in the game world
 */
enum class Clan {
    None = 0,   // No clan affiliation
    MistClan,   // Forest dwellers, stealth specialists
    StormClan,  // Mountain warriors, speed and agility
    EmberClan,  // Volcanic fighters, raw attack power
    FrostClan   // Tundra survivors, defense and endurance
};

/**
 * Player ranks in clan hierarchy
 */
enum class ClanRank {
    Outsider = 0,       // Not part of any clan
    Apprentice = 1,     // New recruit, learning the ways
    Warrior = 2,        // Full clan member
    SeniorWarrior = 3,  // Veteran warrior
    Deputy = 4,         // Second-in-command
    Leader = 5          // Clan leader
};

/**
 * Clan relationship status
 */
enum class ClanRelation {
    Ally,      // Friendly, will help in combat
    Neutral,   // Indifferent, won't attack unprovoked
    Hostile    // Enemies, will attack on sight
};

/**
 * Elemental affinities for each clan
 */
enum class ClanElementType {
    Shadow,    // MistClan - Stealth and darkness
    Lightning, // StormClan - Speed and electric attacks
    Fire,      // EmberClan - Burning damage over time
    Ice        // FrostClan - Slowing and freezing effects
};

/**
 * Story chapter progression
 */
struct StoryChapter {
    int chapterNumber;
    std::string title;
    std::string description;
    bool completed = false;
    bool unlocked = false;

    // Requirements to unlock
    ClanRank requiredRank = ClanRank::Outsider;
    int requiredLevel = 0;
    std::vector<int> prerequisiteChapters;

    // Rewards
    int experienceReward = 0;
    std::vector<std::string> skillRewards;
    std::vector<std::string> territoryUnlocks;
};

/**
 * Story event data
 */
struct StoryEvent {
    std::string eventId;
    std::string title;
    std::string description;
    std::function<void()> onTrigger;
    bool triggered = false;
    bool repeatable = false;

    // Trigger conditions
    Engine::vec3 triggerLocation;
    float triggerRadius = 10.0f;
    ClanRank minimumRank = ClanRank::Outsider;
};

/**
 * Main story mode state
 */
struct StoryModeState {
    bool isActive = false;
    Clan playerClan = Clan::MistClan;
    ClanRank playerRank = ClanRank::Outsider;
    std::string mentorName = "";
    std::unordered_map<Clan, ClanRelation> clanRelationships;
    std::vector<std::string> territoryAccess;
    int mysticalConnections = 0;
    float storyProgress = 0.0f;  // 0.0 to 100.0
    int currentChapter = 0;
    int currentMission = 0;
    int playerLevel = 1;
    int playerExperience = 0;

    // Progression tracking
    int enemiesDefeated = 0;
    int questsCompleted = 0;
    int territoriesCaptured = 0;
    float totalPlayTime = 0.0f;

    /**
     * Get experience required for next level
     */
    int getExperienceForNextLevel() const;

    /**
     * Add experience and handle level ups
     */
    bool addExperience(int xp);

    /**
     * Check if player can rank up
     */
    bool canRankUp() const;

    /**
     * Get rank name as string
     */
    static const char* getRankName(ClanRank rank);

    /**
     * Get clan name as string
     */
    static const char* getClanName(Clan clan);

    /**
     * Get relation name as string
     */
    static const char* getRelationName(ClanRelation relation);
};

/**
 * Story Mode System - Manages campaign progression
 *
 * Integrates with:
 * - Clan system (ranks, bonuses, relations)
 * - Territory system (unlocks, capture)
 * - Skill system (progression, abilities)
 * - Story events and chapters
 */
class StoryModeSystem : public CatEngine::System {
public:
    /**
     * Construct story mode system
     * @param priority System execution priority
     */
    explicit StoryModeSystem(int priority = 5);

    /**
     * Initialize the system
     */
    void init(CatEngine::ECS* ecs) override;

    /**
     * Update story mode each frame
     * @param dt Delta time in seconds
     */
    void update(float dt) override;

    /**
     * Shutdown the system
     */
    void shutdown() override;

    /**
     * Get system name
     */
    const char* getName() const override { return "StoryModeSystem"; }

    // ========================================================================
    // Story Mode Control
    // ========================================================================

    /**
     * Start story mode campaign
     */
    void startStoryMode(Clan initialClan);

    /**
     * End story mode
     */
    void endStoryMode();

    /**
     * Check if story mode is active
     */
    bool isActive() const { return state_.isActive; }

    /**
     * Get current state
     */
    const StoryModeState& getState() const { return state_; }

    // ========================================================================
    // Clan Management
    // ========================================================================

    /**
     * Join a clan (initial or switch)
     */
    void joinClan(Clan clan);

    /**
     * Promote to next rank (if eligible)
     */
    bool promoteRank();

    /**
     * Demote to previous rank (punishment/testing)
     */
    void demoteRank();

    /**
     * Set clan relationship
     */
    void updateClanRelation(Clan clan, ClanRelation relation);

    /**
     * Get relationship with clan
     */
    ClanRelation getClanRelation(Clan clan) const;

    /**
     * Set player's mentor
     */
    void setMentor(const std::string& mentorName);

    // ========================================================================
    // Progression
    // ========================================================================

    /**
     * Add experience to player
     */
    void addExperience(int xp);

    /**
     * Add mystical connection
     */
    void addMysticalConnection();

    /**
     * Unlock territory access
     */
    void unlockTerritory(const std::string& territory);

    /**
     * Check if territory is unlocked
     */
    bool isTerritoryUnlocked(const std::string& territory) const;

    // ========================================================================
    // Story Events & Chapters
    // ========================================================================

    /**
     * Register a story event
     */
    void registerStoryEvent(const StoryEvent& event);

    /**
     * Trigger story event manually
     */
    void triggerStoryEvent(const std::string& eventId);

    /**
     * Check for nearby story events
     */
    void checkStoryEvents(const Engine::vec3& playerPosition);

    /**
     * Add story chapter
     */
    void addStoryChapter(const StoryChapter& chapter);

    /**
     * Complete story chapter
     */
    void completeStoryChapter(int chapterNumber);

    /**
     * Get current chapter
     */
    const StoryChapter* getCurrentChapter() const;

    /**
     * Get all chapters
     */
    const std::vector<StoryChapter>& getAllChapters() const { return chapters_; }

    // ========================================================================
    // Clan Bonuses
    // ========================================================================

    /**
     * Get attack bonus from clan
     */
    float getClanAttackBonus() const;

    /**
     * Get defense bonus from clan
     */
    float getClanDefenseBonus() const;

    /**
     * Get speed bonus from clan
     */
    float getClanSpeedBonus() const;

    /**
     * Get stealth bonus from clan
     */
    float getClanStealthBonus() const;

    /**
     * Get clan elemental affinity
     */
    ClanElementType getClanElement() const;

    /**
     * Get total stat multiplier (clan + rank + skills)
     */
    void getTotalBonuses(float& attack, float& defense, float& speed, float& stealth) const;

    // ========================================================================
    // Skills
    // ========================================================================

    /**
     * Get player skills
     */
    StorySkills& getSkills() { return skills_; }
    const StorySkills& getSkills() const { return skills_; }

    /**
     * Get skill progression manager
     */
    SkillProgressionManager& getSkillManager() { return skillManager_; }

    // ========================================================================
    // Territory
    // ========================================================================

    /**
     * Get territory system
     */
    ClanTerritorySystem& getTerritorySystem() { return territorySystem_; }
    const ClanTerritorySystem& getTerritorySystem() const { return territorySystem_; }

    // ========================================================================
    // Callbacks
    // ========================================================================

    /**
     * Register callback for rank promotion
     */
    void onRankPromotion(void (*callback)(ClanRank oldRank, ClanRank newRank));

    /**
     * Register callback for level up
     */
    void onLevelUp(void (*callback)(int newLevel));

    /**
     * Register callback for chapter completion
     */
    void onChapterComplete(void (*callback)(int chapterNumber));

    /**
     * Register callback for clan join
     */
    void onClanJoin(void (*callback)(Clan clan));

    // ========================================================================
    // Serialization
    // ========================================================================

    /**
     * Serialize story mode state for save system
     */
    std::string serialize() const;

    /**
     * Deserialize story mode state from save data
     */
    bool deserialize(const std::string& data);

    // ========================================================================
    // Save/Load Helper Methods
    // ========================================================================

    /**
     * Check if the story is complete (all chapters finished)
     */
    [[nodiscard]] bool isStoryComplete() const;

    /**
     * Get the current chapter number
     */
    [[nodiscard]] int getCurrentChapterNumber() const { return state_.currentChapter; }

    /**
     * Get the current mission number
     */
    [[nodiscard]] int getCurrentMission() const { return state_.currentMission; }

    /**
     * Set the current chapter (for save loading)
     * @param chapterIndex Index of the chapter to set as current
     */
    void setChapter(int chapterIndex);

    /**
     * Set the current mission within the current chapter (for save loading)
     * @param missionIndex Index of the mission within the current chapter
     */
    void setMission(int missionIndex);

    /**
     * Advance to the next story beat (mission or chapter)
     */
    void advanceStory();

private:
    StoryModeState state_;
    StorySkills skills_;
    SkillProgressionManager skillManager_;
    ClanTerritorySystem territorySystem_;

    // Story content
    std::vector<StoryChapter> chapters_;
    std::unordered_map<std::string, StoryEvent> events_;
    std::vector<std::string> completedEvents_;

    // Callbacks
    void (*rankPromotionCallback_)(ClanRank, ClanRank) = nullptr;
    void (*levelUpCallback_)(int) = nullptr;
    void (*chapterCompleteCallback_)(int) = nullptr;
    void (*clanJoinCallback_)(Clan) = nullptr;

    // Player entity tracking
    CatEngine::Entity playerEntity_;

    // Helper methods
    void initializeDefaultRelationships();
    void initializeStoryChapters();
    void checkRankPromotion();
    void applyRankBonuses();
    float getRankBonus() const;

    /**
     * Calculate clan-specific bonuses
     */
    void calculateClanBonuses(Clan clan, float& attack, float& defense, float& speed, float& stealth) const;
};

/**
 * Helper functions for story mode
 */
namespace StoryModeHelpers {
    /**
     * Get element type name
     */
    const char* getElementName(ClanElementType element);

    /**
     * Get element color (for UI)
     */
    Engine::vec3 getElementColor(ClanElementType element);

    /**
     * Calculate experience curve
     */
    int calculateExperienceForLevel(int level);

    /**
     * Get rank promotion requirements
     */
    struct RankRequirements {
        int requiredLevel;
        int requiredQuestsCompleted;
        int requiredEnemiesDefeated;
        std::string description;
    };

    RankRequirements getRankRequirements(ClanRank rank);
}

} // namespace CatGame

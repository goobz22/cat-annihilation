#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Vector.hpp"
#include "story_mode.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <memory>

namespace CatGame {

/**
 * Quest type categorization
 */
enum class QuestType {
    MainStory,      // Required for story progression
    SideQuest,      // Optional content
    Daily,          // Repeatable daily
    ClanMission,    // Clan-specific
    Bounty          // Kill target enemies
};

/**
 * Objective type for quest goals
 */
enum class ObjectiveType {
    Kill,           // Kill X enemies of type Y
    Collect,        // Collect X items
    Escort,         // Escort NPC to location
    Explore,        // Visit location
    Talk,           // Talk to NPC
    Survive,        // Survive for X seconds
    Defend          // Defend location
};

/**
 * Quest objective structure
 * Represents a single goal within a quest
 */
struct QuestObjective {
    ObjectiveType type;
    std::string description;
    std::string targetId;      // Enemy type, item type, NPC id, location id, etc.
    int requiredCount;
    int currentCount = 0;
    bool completed = false;
    Engine::vec3 waypointLocation = Engine::vec3::zero(); // For objectives with location

    /**
     * Check if objective is complete
     */
    bool isComplete() const {
        return currentCount >= requiredCount;
    }

    /**
     * Update objective progress
     */
    void updateProgress(int amount = 1) {
        currentCount += amount;
        if (currentCount >= requiredCount) {
            currentCount = requiredCount;
            completed = true;
        }
    }

    /**
     * Get progress percentage
     */
    float getProgressPercent() const {
        return requiredCount > 0 ? (static_cast<float>(currentCount) / requiredCount) * 100.0f : 0.0f;
    }

    /**
     * Get progress text
     */
    std::string getProgressText() const {
        return std::to_string(currentCount) + "/" + std::to_string(requiredCount);
    }
};

/**
 * Quest rewards structure
 */
struct QuestReward {
    int xp = 0;
    int currency = 0;
    std::vector<std::string> items;
    std::optional<std::string> abilityUnlock;
    std::optional<std::string> territoryUnlock;

    /**
     * Check if reward has any items
     */
    bool hasItems() const {
        return !items.empty();
    }

    /**
     * Get total reward value (for sorting/prioritization)
     */
    int getTotalValue() const {
        int value = xp + currency * 10;
        value += items.size() * 50;
        if (abilityUnlock.has_value()) value += 500;
        if (territoryUnlock.has_value()) value += 1000;
        return value;
    }
};

/**
 * Quest structure
 * Represents a complete quest with objectives and rewards
 */
struct Quest {
    std::string id;
    std::string title;
    std::string description;
    std::string loreText;          // Story/narrative text
    QuestType type;
    int requiredLevel = 1;
    std::optional<Clan> requiredClan;
    std::vector<std::string> prerequisites;  // Quest IDs that must be completed first
    std::vector<QuestObjective> objectives;
    QuestReward rewards;

    // Quest state
    bool isActive = false;
    bool isCompleted = false;
    bool isFailed = false;
    bool canRepeat = false;        // For daily quests
    float timeLimit = 0.0f;        // 0 = no time limit
    float timeRemaining = 0.0f;

    // Quest giver
    std::string questGiverId;      // NPC who gives quest
    std::string turnInNpcId;       // NPC to turn in quest (empty = same as giver)

    /**
     * Check if all objectives are complete
     */
    bool areAllObjectivesComplete() const {
        for (const auto& obj : objectives) {
            if (!obj.completed) {
                return false;
            }
        }
        return !objectives.empty();
    }

    /**
     * Get total progress percentage
     */
    float getTotalProgress() const {
        if (objectives.empty()) return 0.0f;

        float totalProgress = 0.0f;
        for (const auto& obj : objectives) {
            totalProgress += obj.getProgressPercent();
        }
        return totalProgress / objectives.size();
    }

    /**
     * Get active objective (first incomplete one)
     */
    const QuestObjective* getActiveObjective() const {
        for (const auto& obj : objectives) {
            if (!obj.completed) {
                return &obj;
            }
        }
        return nullptr;
    }

    /**
     * Check if quest is available for player
     */
    bool isAvailableFor(int playerLevel, Clan playerClan) const {
        // Check level requirement
        if (playerLevel < requiredLevel) {
            return false;
        }

        // Check clan requirement
        if (requiredClan.has_value() && requiredClan.value() != playerClan) {
            return false;
        }

        // Can't be active or completed (unless repeatable)
        if (isActive || (isCompleted && !canRepeat)) {
            return false;
        }

        return true;
    }
};

/**
 * Quest System
 * Manages all quests, objectives, and progression
 */
class QuestSystem : public CatEngine::System {
public:
    using QuestCallback = std::function<void(const Quest&)>;
    using ObjectiveCallback = std::function<void(const Quest&, const QuestObjective&)>;

    explicit QuestSystem(int priority = 150);
    ~QuestSystem() override = default;

    void init(CatEngine::ECS* ecs) override;
    void update(float dt) override;
    const char* getName() const override { return "QuestSystem"; }

    /**
     * Initialize with player information
     */
    void setPlayerInfo(int level, Clan clan);

    /**
     * Quest management
     */
    void loadQuestsFromFile(const std::string& path);
    void loadQuestsFromData();  // Load from quest_data.hpp
    bool activateQuest(const std::string& questId);
    bool abandonQuest(const std::string& questId);
    bool completeQuest(const std::string& questId);
    bool failQuest(const std::string& questId);
    void resetDailyQuests();

    /**
     * Objective tracking - called by other systems
     */
    void onEnemyKilled(const std::string& enemyType);
    void onItemCollected(const std::string& itemType);
    void onLocationVisited(const std::string& locationId);
    void onNPCTalkedTo(const std::string& npcId);
    void onSurviveTime(float seconds);
    void onDefendComplete(const std::string& locationId);

    /**
     * Queries
     */
    std::vector<const Quest*> getActiveQuests() const;
    std::vector<const Quest*> getAvailableQuests() const;
    std::vector<const Quest*> getCompletedQuests() const;
    std::vector<const Quest*> getQuestsByType(QuestType type) const;
    const Quest* getQuest(const std::string& questId) const;
    Quest* getQuestMutable(const std::string& questId);

    /**
     * Quest availability checks
     */
    bool canActivateQuest(const std::string& questId) const;
    bool arePrerequisitesMet(const std::string& questId) const;
    int getActiveQuestCount() const;
    int getMaxActiveQuests() const { return maxActiveQuests_; }
    void setMaxActiveQuests(int max) { maxActiveQuests_ = max; }

    /**
     * UI helpers
     */
    std::string getQuestTrackerText() const;
    Engine::vec3 getActiveQuestWaypoint() const;
    std::vector<std::string> getQuestLog() const;

    /**
     * Callbacks
     */
    using RewardCallback = std::function<void(const QuestReward&)>;

    void setOnQuestActivated(const QuestCallback& callback) { onQuestActivated_ = callback; }
    void setOnQuestCompleted(const QuestCallback& callback) { onQuestCompleted_ = callback; }
    void setOnQuestFailed(const QuestCallback& callback) { onQuestFailed_ = callback; }
    void setOnObjectiveCompleted(const ObjectiveCallback& callback) { onObjectiveCompleted_ = callback; }
    void setOnRewardGranted(const RewardCallback& callback) { onRewardGranted_ = callback; }

    /**
     * Statistics
     */
    int getTotalQuestsCompleted() const { return totalQuestsCompleted_; }
    int getTotalXPEarned() const { return totalXPEarned_; }
    int getTotalCurrencyEarned() const { return totalCurrencyEarned_; }

    /**
     * Save/Load helpers - get quest IDs for serialization
     */
    [[nodiscard]] const std::vector<std::string>& getActiveQuestIds() const { return activeQuestIds_; }
    [[nodiscard]] const std::vector<std::string>& getCompletedQuestIds() const { return completedQuestIds_; }

    /**
     * Load quest state from save data
     */
    void loadQuestState(const std::vector<std::string>& activeIds,
                        const std::vector<std::string>& completedIds);

private:
    /**
     * Update quest timers
     */
    void updateQuestTimers(float dt);

    /**
     * Check if quest objectives are completed
     */
    void checkQuestCompletion(Quest& quest);

    /**
     * Update objective for active quests
     */
    void updateObjective(const std::string& targetId, ObjectiveType type, int amount = 1);

    /**
     * Grant quest rewards to player
     */
    void grantRewards(const QuestReward& rewards);

    /**
     * Notify quest callbacks
     */
    void notifyQuestActivated(const Quest& quest);
    void notifyQuestCompleted(const Quest& quest);
    void notifyQuestFailed(const Quest& quest);
    void notifyObjectiveCompleted(const Quest& quest, const QuestObjective& objective);

    /**
     * Check prerequisites
     */
    bool checkPrerequisites(const Quest& quest) const;

    // Quest storage
    std::unordered_map<std::string, Quest> quests_;
    std::vector<std::string> activeQuestIds_;
    std::vector<std::string> completedQuestIds_;

    // Player information
    int playerLevel_ = 1;
    Clan playerClan_ = Clan::MistClan;

    // System configuration
    int maxActiveQuests_ = 5;

    // Statistics
    int totalQuestsCompleted_ = 0;
    int totalXPEarned_ = 0;
    int totalCurrencyEarned_ = 0;

    // Callbacks
    QuestCallback onQuestActivated_;
    QuestCallback onQuestCompleted_;
    QuestCallback onQuestFailed_;
    ObjectiveCallback onObjectiveCompleted_;
    RewardCallback onRewardGranted_;

    // System state
    bool initialized_ = false;
};

/**
 * Helper functions for quest system
 */
namespace QuestHelpers {
    /**
     * Convert clan to string
     */
    inline std::string clanToString(Clan clan) {
        switch (clan) {
            case Clan::MistClan: return "MistClan";
            case Clan::StormClan: return "StormClan";
            case Clan::EmberClan: return "EmberClan";
            case Clan::FrostClan: return "FrostClan";
            default: return "Unknown";
        }
    }

    /**
     * Convert string to clan
     */
    inline std::optional<Clan> stringToClan(const std::string& str) {
        if (str == "MistClan") return Clan::MistClan;
        if (str == "StormClan") return Clan::StormClan;
        if (str == "EmberClan") return Clan::EmberClan;
        if (str == "FrostClan") return Clan::FrostClan;
        return std::nullopt;
    }

    /**
     * Convert quest type to string
     */
    inline std::string questTypeToString(QuestType type) {
        switch (type) {
            case QuestType::MainStory: return "Main Story";
            case QuestType::SideQuest: return "Side Quest";
            case QuestType::Daily: return "Daily";
            case QuestType::ClanMission: return "Clan Mission";
            case QuestType::Bounty: return "Bounty";
            default: return "Unknown";
        }
    }

    /**
     * Convert objective type to string
     */
    inline std::string objectiveTypeToString(ObjectiveType type) {
        switch (type) {
            case ObjectiveType::Kill: return "Kill";
            case ObjectiveType::Collect: return "Collect";
            case ObjectiveType::Escort: return "Escort";
            case ObjectiveType::Explore: return "Explore";
            case ObjectiveType::Talk: return "Talk";
            case ObjectiveType::Survive: return "Survive";
            case ObjectiveType::Defend: return "Defend";
            default: return "Unknown";
        }
    }
}

} // namespace CatGame

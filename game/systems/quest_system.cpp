#include "quest_system.hpp"
#include "quest_data.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <algorithm>
#include <sstream>
#include <fstream>

// WHY: Every log call in this file previously passed "QuestSystem" as the
// first argument, expecting a printf-style (tag, fmt, args...) signature.
// The underlying logger actually binds the first arg to
// std::format_string<Args...> (C++20 std::format), so the tag *became* the
// format string, all printf specifiers (%s, %d, %zu) were ignored, and the
// intended message was silently discarded — output was literally just the
// word "QuestSystem". That masked the "quests.json missing, using defaults"
// WARN that fires every startup because CatAnnihilation.cpp points at the
// wrong path. Fix: embed the tag as a "[QuestSystem] " prefix in the
// format string and convert all specifiers to std::format's `{}` syntax.
// std::format's formatter<std::string> means the old `.c_str()` calls on
// std::string arguments are no longer needed.

namespace CatGame {

QuestSystem::QuestSystem(int priority)
    : System(priority)
{
}

void QuestSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);

    // Load quests from data
    loadQuestsFromData();

    initialized_ = true;
    Engine::Logger::info("[QuestSystem] Quest system initialized with {} quests", quests_.size());
}

void QuestSystem::update(float dt) {
    if (!initialized_) return;

    // Update quest timers
    updateQuestTimers(dt);

    // Check for quest completions
    for (const auto& questId : activeQuestIds_) {
        auto* quest = getQuestMutable(questId);
        if (quest && !quest->isCompleted) {
            checkQuestCompletion(*quest);
        }
    }
}

void QuestSystem::setPlayerInfo(int level, Clan clan) {
    playerLevel_ = level;
    playerClan_ = clan;
    Engine::Logger::info("[QuestSystem] Player info updated: Level {}, Clan {}",
                        level, QuestHelpers::clanToString(clan));
}

void QuestSystem::loadQuestsFromFile(const std::string& path) {
    Engine::Logger::info("[QuestSystem] Loading quests from file: {}", path);

    std::ifstream file(path);
    if (!file.is_open()) {
        Engine::Logger::warn("[QuestSystem] Failed to open quest file: {}, using default quests", path);
        loadQuestsFromData();
        return;
    }

    try {
        nlohmann::json questsJson = nlohmann::json::parse(file);

        for (const auto& questJson : questsJson["quests"]) {
            Quest quest;
            quest.id = questJson.value("id", "");
            quest.title = questJson.value("title", "");
            quest.description = questJson.value("description", "");
            quest.loreText = questJson.value("lore", "");
            quest.requiredLevel = questJson.value("requiredLevel", 1);
            quest.canRepeat = questJson.value("repeatable", false);
            quest.timeLimit = questJson.value("timeLimit", 0.0F);
            quest.questGiverId = questJson.value("questGiver", "");
            quest.turnInNpcId = questJson.value("turnInNpc", "");

            // Parse quest type
            std::string typeStr = questJson.value("type", "main");
            if (typeStr == "main") quest.type = QuestType::MainStory;
            else if (typeStr == "side") quest.type = QuestType::SideQuest;
            else if (typeStr == "daily") quest.type = QuestType::Daily;
            else if (typeStr == "bounty") quest.type = QuestType::Bounty;
            else if (typeStr == "clan") quest.type = QuestType::ClanMission;
            else quest.type = QuestType::SideQuest;

            // Parse objectives
            if (questJson.contains("objectives")) {
                for (const auto& objJson : questJson["objectives"]) {
                    QuestObjective obj;
                    obj.description = objJson.value("description", "");
                    obj.targetId = objJson.value("targetId", "");
                    obj.requiredCount = objJson.value("count", 1);
                    obj.currentCount = 0;
                    obj.completed = false;

                    std::string objType = objJson.value("type", "kill");
                    if (objType == "kill") obj.type = ObjectiveType::Kill;
                    else if (objType == "collect") obj.type = ObjectiveType::Collect;
                    else if (objType == "explore") obj.type = ObjectiveType::Explore;
                    else if (objType == "talk") obj.type = ObjectiveType::Talk;
                    else if (objType == "survive") obj.type = ObjectiveType::Survive;
                    else if (objType == "defend") obj.type = ObjectiveType::Defend;
                    else if (objType == "escort") obj.type = ObjectiveType::Escort;
                    else obj.type = ObjectiveType::Kill;

                    quest.objectives.push_back(obj);
                }
            }

            // Parse rewards
            if (questJson.contains("rewards")) {
                const auto& rewardsJson = questJson["rewards"];
                quest.rewards.xp = rewardsJson.value("xp", 0);
                quest.rewards.currency = rewardsJson.value("currency", 0);
            }

            // Parse prerequisites
            if (questJson.contains("prerequisites")) {
                for (const auto& prereq : questJson["prerequisites"]) {
                    quest.prerequisites.push_back(prereq.get<std::string>());
                }
            }

            if (!quest.id.empty()) {
                quests_[quest.id] = std::move(quest);
            }
        }

        Engine::Logger::info("[QuestSystem] Loaded {} quests from JSON file", quests_.size());
    } catch (const nlohmann::json::exception& e) {
        Engine::Logger::error("[QuestSystem] Failed to parse quest file: {}", e.what());
        loadQuestsFromData();
    }
}

void QuestSystem::loadQuestsFromData() {
    // Load all quests from quest_data.hpp
    auto allQuests = QuestData::getAllQuests();

    for (auto& quest : allQuests) {
        quests_[quest.id] = std::move(quest);
    }

    Engine::Logger::info("[QuestSystem] Loaded {} quests from quest data", quests_.size());
}

bool QuestSystem::activateQuest(const std::string& questId) {
    // Check if quest exists
    auto* quest = getQuestMutable(questId);
    if (!quest) {
        Engine::Logger::warning("[QuestSystem] Cannot activate quest '{}': Quest not found", questId);
        return false;
    }

    // Check if already active
    if (quest->isActive) {
        Engine::Logger::warning("[QuestSystem] Quest '{}' is already active", questId);
        return false;
    }

    // Check if already completed (and not repeatable)
    if (quest->isCompleted && !quest->canRepeat) {
        Engine::Logger::warning("[QuestSystem] Quest '{}' is already completed", questId);
        return false;
    }

    // Check max active quests
    if (static_cast<int>(activeQuestIds_.size()) >= maxActiveQuests_) {
        Engine::Logger::warning("[QuestSystem] Cannot activate quest: Max active quests reached ({})", maxActiveQuests_);
        return false;
    }

    // Check prerequisites
    if (!checkPrerequisites(*quest)) {
        Engine::Logger::warning("[QuestSystem] Cannot activate quest '{}': Prerequisites not met", questId);
        return false;
    }

    // Check level requirement
    if (playerLevel_ < quest->requiredLevel) {
        Engine::Logger::warning("[QuestSystem] Cannot activate quest '{}': Requires level {} (player is {})",
                              questId, quest->requiredLevel, playerLevel_);
        return false;
    }

    // Check clan requirement
    if (quest->requiredClan.has_value() && quest->requiredClan.value() != playerClan_) {
        Engine::Logger::warning("[QuestSystem] Cannot activate quest '{}': Requires clan {}",
                              questId, QuestHelpers::clanToString(quest->requiredClan.value()));
        return false;
    }

    // Activate quest
    quest->isActive = true;
    quest->isCompleted = false;
    quest->isFailed = false;
    quest->timeRemaining = quest->timeLimit;

    // Reset objectives if repeatable
    if (quest->canRepeat) {
        for (auto& obj : quest->objectives) {
            obj.currentCount = 0;
            obj.completed = false;
        }
    }

    activeQuestIds_.push_back(questId);

    notifyQuestActivated(*quest);
    Engine::Logger::info("[QuestSystem] Quest activated: {}", quest->title);

    return true;
}

bool QuestSystem::abandonQuest(const std::string& questId) {
    auto* quest = getQuestMutable(questId);
    if (!quest || !quest->isActive) {
        return false;
    }

    // Remove from active quests
    activeQuestIds_.erase(
        std::remove(activeQuestIds_.begin(), activeQuestIds_.end(), questId),
        activeQuestIds_.end()
    );

    // Reset quest state
    quest->isActive = false;
    for (auto& obj : quest->objectives) {
        obj.currentCount = 0;
        obj.completed = false;
    }

    Engine::Logger::info("[QuestSystem] Quest abandoned: {}", quest->title);
    return true;
}

bool QuestSystem::completeQuest(const std::string& questId) {
    auto* quest = getQuestMutable(questId);
    if (!quest || !quest->isActive) {
        return false;
    }

    // Check if all objectives are complete
    if (!quest->areAllObjectivesComplete()) {
        Engine::Logger::warning("[QuestSystem] Cannot complete quest '{}': Objectives not finished", questId);
        return false;
    }

    // Complete quest
    quest->isActive = false;
    quest->isCompleted = true;

    // Remove from active quests
    activeQuestIds_.erase(
        std::remove(activeQuestIds_.begin(), activeQuestIds_.end(), questId),
        activeQuestIds_.end()
    );

    // Add to completed quests
    if (std::find(completedQuestIds_.begin(), completedQuestIds_.end(), questId) == completedQuestIds_.end()) {
        completedQuestIds_.push_back(questId);
    }

    // Grant rewards
    grantRewards(quest->rewards);

    // Update statistics
    totalQuestsCompleted_++;

    notifyQuestCompleted(*quest);
    Engine::Logger::info("[QuestSystem] Quest completed: {}", quest->title);

    return true;
}

bool QuestSystem::failQuest(const std::string& questId) {
    auto* quest = getQuestMutable(questId);
    if (!quest || !quest->isActive) {
        return false;
    }

    // Fail quest
    quest->isActive = false;
    quest->isFailed = true;

    // Remove from active quests
    activeQuestIds_.erase(
        std::remove(activeQuestIds_.begin(), activeQuestIds_.end(), questId),
        activeQuestIds_.end()
    );

    notifyQuestFailed(*quest);
    Engine::Logger::info("[QuestSystem] Quest failed: {}", quest->title);

    return true;
}

void QuestSystem::resetDailyQuests() {
    for (auto& pair : quests_) {
        auto& quest = pair.second;
        if (quest.type == QuestType::Daily) {
            quest.isCompleted = false;
            quest.isFailed = false;
            for (auto& obj : quest.objectives) {
                obj.currentCount = 0;
                obj.completed = false;
            }
        }
    }
    Engine::Logger::info("[QuestSystem] Daily quests reset");
}

void QuestSystem::onEnemyKilled(const std::string& enemyType) {
    updateObjective(enemyType, ObjectiveType::Kill, 1);
}

void QuestSystem::onItemCollected(const std::string& itemType) {
    updateObjective(itemType, ObjectiveType::Collect, 1);
}

void QuestSystem::onLocationVisited(const std::string& locationId) {
    updateObjective(locationId, ObjectiveType::Explore, 1);
}

void QuestSystem::onNPCTalkedTo(const std::string& npcId) {
    updateObjective(npcId, ObjectiveType::Talk, 1);
}

void QuestSystem::onSurviveTime(float seconds) {
    updateObjective("survive", ObjectiveType::Survive, static_cast<int>(seconds));
}

void QuestSystem::onDefendComplete(const std::string& locationId) {
    updateObjective(locationId, ObjectiveType::Defend, 1);
}

std::vector<const Quest*> QuestSystem::getActiveQuests() const {
    std::vector<const Quest*> active;
    for (const auto& questId : activeQuestIds_) {
        auto* quest = getQuest(questId);
        if (quest) {
            active.push_back(quest);
        }
    }
    return active;
}

std::vector<const Quest*> QuestSystem::getAvailableQuests() const {
    std::vector<const Quest*> available;
    for (const auto& pair : quests_) {
        const auto& quest = pair.second;
        if (quest.isAvailableFor(playerLevel_, playerClan_) && checkPrerequisites(quest)) {
            available.push_back(&quest);
        }
    }
    return available;
}

std::vector<const Quest*> QuestSystem::getCompletedQuests() const {
    std::vector<const Quest*> completed;
    for (const auto& questId : completedQuestIds_) {
        auto* quest = getQuest(questId);
        if (quest) {
            completed.push_back(quest);
        }
    }
    return completed;
}

std::vector<const Quest*> QuestSystem::getQuestsByType(QuestType type) const {
    std::vector<const Quest*> quests;
    for (const auto& pair : quests_) {
        if (pair.second.type == type) {
            quests.push_back(&pair.second);
        }
    }
    return quests;
}

const Quest* QuestSystem::getQuest(const std::string& questId) const {
    auto it = quests_.find(questId);
    return (it != quests_.end()) ? &it->second : nullptr;
}

Quest* QuestSystem::getQuestMutable(const std::string& questId) {
    auto it = quests_.find(questId);
    return (it != quests_.end()) ? &it->second : nullptr;
}

bool QuestSystem::canActivateQuest(const std::string& questId) const {
    auto* quest = getQuest(questId);
    if (!quest) return false;

    if (quest->isActive) return false;
    if (quest->isCompleted && !quest->canRepeat) return false;
    if (static_cast<int>(activeQuestIds_.size()) >= maxActiveQuests_) return false;
    if (!checkPrerequisites(*quest)) return false;
    if (playerLevel_ < quest->requiredLevel) return false;
    if (quest->requiredClan.has_value() && quest->requiredClan.value() != playerClan_) return false;

    return true;
}

bool QuestSystem::arePrerequisitesMet(const std::string& questId) const {
    auto* quest = getQuest(questId);
    return quest ? checkPrerequisites(*quest) : false;
}

int QuestSystem::getActiveQuestCount() const {
    return static_cast<int>(activeQuestIds_.size());
}

std::string QuestSystem::getQuestTrackerText() const {
    std::stringstream ss;

    auto activeQuests = getActiveQuests();
    if (activeQuests.empty()) {
        ss << "No active quests\n";
        return ss.str();
    }

    ss << "=== ACTIVE QUESTS ===\n\n";

    for (const auto* quest : activeQuests) {
        ss << "[" << QuestHelpers::questTypeToString(quest->type) << "] " << quest->title << "\n";

        for (const auto& obj : quest->objectives) {
            ss << "  ";
            if (obj.completed) {
                ss << "[X] ";
            } else {
                ss << "[ ] ";
            }
            ss << obj.description << " (" << obj.getProgressText() << ")\n";
        }

        if (quest->timeLimit > 0.0f) {
            int minutes = static_cast<int>(quest->timeRemaining / 60.0f);
            int seconds = static_cast<int>(quest->timeRemaining) % 60;
            ss << "  Time Remaining: " << minutes << ":" << (seconds < 10 ? "0" : "") << seconds << "\n";
        }

        ss << "\n";
    }

    return ss.str();
}

Engine::vec3 QuestSystem::getActiveQuestWaypoint() const {
    // Return waypoint for first active quest with a waypoint
    for (const auto& questId : activeQuestIds_) {
        auto* quest = getQuest(questId);
        if (quest) {
            auto* activeObj = quest->getActiveObjective();
            if (activeObj && activeObj->waypointLocation != Engine::vec3::zero()) {
                return activeObj->waypointLocation;
            }
        }
    }
    return Engine::vec3::zero();
}

std::vector<std::string> QuestSystem::getQuestLog() const {
    std::vector<std::string> log;

    // Add active quests
    for (const auto* quest : getActiveQuests()) {
        log.push_back("[ACTIVE] " + quest->title + " - " +
                     std::to_string(static_cast<int>(quest->getTotalProgress())) + "% complete");
    }

    // Add completed quests
    for (const auto* quest : getCompletedQuests()) {
        log.push_back("[COMPLETED] " + quest->title);
    }

    return log;
}

void QuestSystem::updateQuestTimers(float dt) {
    for (const auto& questId : activeQuestIds_) {
        auto* quest = getQuestMutable(questId);
        if (quest && quest->timeLimit > 0.0f) {
            quest->timeRemaining -= dt;

            // Check for time expiration
            if (quest->timeRemaining <= 0.0f) {
                quest->timeRemaining = 0.0f;
                failQuest(questId);
            }
        }
    }
}

void QuestSystem::checkQuestCompletion(Quest& quest) {
    if (quest.areAllObjectivesComplete() && !quest.isCompleted) {
        // Auto-complete quests that don't require turn-in
        if (quest.turnInNpcId.empty()) {
            completeQuest(quest.id);
        }
    }
}

void QuestSystem::updateObjective(const std::string& targetId, ObjectiveType type, int amount) {
    for (const auto& questId : activeQuestIds_) {
        auto* quest = getQuestMutable(questId);
        if (!quest) continue;

        for (auto& obj : quest->objectives) {
            // Check if this objective matches
            if (obj.type == type && obj.targetId == targetId && !obj.completed) {
                int previousCount = obj.currentCount;
                obj.updateProgress(amount);

                // Check if objective just completed
                if (obj.completed && previousCount < obj.requiredCount) {
                    notifyObjectiveCompleted(*quest, obj);
                    Engine::Logger::info("[QuestSystem] Objective completed: {}", obj.description);
                }
            }
        }
    }
}

void QuestSystem::grantRewards(const QuestReward& rewards) {
    // Update statistics
    totalXPEarned_ += rewards.xp;
    totalCurrencyEarned_ += rewards.currency;

    // Log rewards
    Engine::Logger::info("[QuestSystem] Granting rewards: {} XP, {} currency, {} items",
                        rewards.xp, rewards.currency, rewards.items.size());

    if (rewards.abilityUnlock.has_value()) {
        Engine::Logger::info("[QuestSystem] Ability unlocked: {}", rewards.abilityUnlock.value());
    }

    if (rewards.territoryUnlock.has_value()) {
        Engine::Logger::info("[QuestSystem] Territory unlocked: {}", rewards.territoryUnlock.value());
    }

    // Delegate reward granting to the game layer via callback
    // The CatAnnihilation class registers a callback that integrates with:
    // - LevelingSystem for XP rewards
    // - MerchantSystem for currency rewards
    // - InventorySystem for item rewards
    // - StoryModeSystem for ability and territory unlocks
    if (onRewardGranted_) {
        onRewardGranted_(rewards);
    }
}

void QuestSystem::notifyQuestActivated(const Quest& quest) {
    if (onQuestActivated_) {
        onQuestActivated_(quest);
    }
}

void QuestSystem::notifyQuestCompleted(const Quest& quest) {
    if (onQuestCompleted_) {
        onQuestCompleted_(quest);
    }
}

void QuestSystem::notifyQuestFailed(const Quest& quest) {
    if (onQuestFailed_) {
        onQuestFailed_(quest);
    }
}

void QuestSystem::notifyObjectiveCompleted(const Quest& quest, const QuestObjective& objective) {
    if (onObjectiveCompleted_) {
        onObjectiveCompleted_(quest, objective);
    }
}

bool QuestSystem::checkPrerequisites(const Quest& quest) const {
    for (const auto& prereqId : quest.prerequisites) {
        // Check if prerequisite quest is completed
        if (std::find(completedQuestIds_.begin(), completedQuestIds_.end(), prereqId) == completedQuestIds_.end()) {
            return false;
        }
    }
    return true;
}

void QuestSystem::loadQuestState(const std::vector<std::string>& activeIds,
                                  const std::vector<std::string>& completedIds) {
    // Clear current quest state
    activeQuestIds_.clear();
    completedQuestIds_ = completedIds;

    // Reset all quest states first
    for (auto& [id, quest] : quests_) {
        quest.isActive = false;
        quest.isCompleted = false;
        quest.isFailed = false;
    }

    // Mark completed quests
    for (const auto& id : completedIds) {
        auto it = quests_.find(id);
        if (it != quests_.end()) {
            it->second.isCompleted = true;
        }
    }

    // Reactivate active quests
    for (const auto& id : activeIds) {
        auto it = quests_.find(id);
        if (it != quests_.end()) {
            it->second.isActive = true;
            activeQuestIds_.push_back(id);
        }
    }
}

} // namespace CatGame

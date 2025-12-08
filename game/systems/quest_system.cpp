#include "quest_system.hpp"
#include "quest_data.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>
#include <sstream>
#include <fstream>

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
    Engine::Logger::info("QuestSystem", "Quest system initialized with %zu quests", quests_.size());
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
    Engine::Logger::info("QuestSystem", "Player info updated: Level %d, Clan %s",
                        level, QuestHelpers::clanToString(clan).c_str());
}

void QuestSystem::loadQuestsFromFile(const std::string& path) {
    // TODO: Implement JSON parsing for quest loading
    // For now, we'll use the hardcoded quest data
    Engine::Logger::info("QuestSystem", "Loading quests from file: %s", path.c_str());

    // This would parse JSON and populate quests_
    // For now, using loadQuestsFromData() instead
}

void QuestSystem::loadQuestsFromData() {
    // Load all quests from quest_data.hpp
    auto allQuests = QuestData::getAllQuests();

    for (auto& quest : allQuests) {
        quests_[quest.id] = std::move(quest);
    }

    Engine::Logger::info("QuestSystem", "Loaded %zu quests from quest data", quests_.size());
}

bool QuestSystem::activateQuest(const std::string& questId) {
    // Check if quest exists
    auto* quest = getQuestMutable(questId);
    if (!quest) {
        Engine::Logger::warning("QuestSystem", "Cannot activate quest '%s': Quest not found", questId.c_str());
        return false;
    }

    // Check if already active
    if (quest->isActive) {
        Engine::Logger::warning("QuestSystem", "Quest '%s' is already active", questId.c_str());
        return false;
    }

    // Check if already completed (and not repeatable)
    if (quest->isCompleted && !quest->canRepeat) {
        Engine::Logger::warning("QuestSystem", "Quest '%s' is already completed", questId.c_str());
        return false;
    }

    // Check max active quests
    if (static_cast<int>(activeQuestIds_.size()) >= maxActiveQuests_) {
        Engine::Logger::warning("QuestSystem", "Cannot activate quest: Max active quests reached (%d)", maxActiveQuests_);
        return false;
    }

    // Check prerequisites
    if (!checkPrerequisites(*quest)) {
        Engine::Logger::warning("QuestSystem", "Cannot activate quest '%s': Prerequisites not met", questId.c_str());
        return false;
    }

    // Check level requirement
    if (playerLevel_ < quest->requiredLevel) {
        Engine::Logger::warning("QuestSystem", "Cannot activate quest '%s': Requires level %d (player is %d)",
                              questId.c_str(), quest->requiredLevel, playerLevel_);
        return false;
    }

    // Check clan requirement
    if (quest->requiredClan.has_value() && quest->requiredClan.value() != playerClan_) {
        Engine::Logger::warning("QuestSystem", "Cannot activate quest '%s': Requires clan %s",
                              questId.c_str(), QuestHelpers::clanToString(quest->requiredClan.value()).c_str());
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
    Engine::Logger::info("QuestSystem", "Quest activated: %s", quest->title.c_str());

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

    Engine::Logger::info("QuestSystem", "Quest abandoned: %s", quest->title.c_str());
    return true;
}

bool QuestSystem::completeQuest(const std::string& questId) {
    auto* quest = getQuestMutable(questId);
    if (!quest || !quest->isActive) {
        return false;
    }

    // Check if all objectives are complete
    if (!quest->areAllObjectivesComplete()) {
        Engine::Logger::warning("QuestSystem", "Cannot complete quest '%s': Objectives not finished", questId.c_str());
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
    Engine::Logger::info("QuestSystem", "Quest completed: %s", quest->title.c_str());

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
    Engine::Logger::info("QuestSystem", "Quest failed: %s", quest->title.c_str());

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
    Engine::Logger::info("QuestSystem", "Daily quests reset");
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
                    Engine::Logger::info("QuestSystem", "Objective completed: %s", obj.description.c_str());
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
    Engine::Logger::info("QuestSystem", "Granting rewards: %d XP, %d currency, %zu items",
                        rewards.xp, rewards.currency, rewards.items.size());

    if (rewards.abilityUnlock.has_value()) {
        Engine::Logger::info("QuestSystem", "Ability unlocked: %s", rewards.abilityUnlock.value().c_str());
    }

    if (rewards.territoryUnlock.has_value()) {
        Engine::Logger::info("QuestSystem", "Territory unlocked: %s", rewards.territoryUnlock.value().c_str());
    }

    // TODO: Integrate with actual player systems
    // - PlayerSystem::addXP(rewards.xp)
    // - InventorySystem::addCurrency(rewards.currency)
    // - InventorySystem::addItems(rewards.items)
    // - AbilitySystem::unlockAbility(rewards.abilityUnlock)
    // - TerritorySystem::unlockTerritory(rewards.territoryUnlock)
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

} // namespace CatGame

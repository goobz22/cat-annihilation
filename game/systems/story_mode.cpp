#include "story_mode.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Vector.hpp"
#include "../components/GameComponents.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace CatGame {

// ============================================================================
// StoryModeState Implementation
// ============================================================================

int StoryModeState::getExperienceForNextLevel() const {
    return StoryModeHelpers::calculateExperienceForLevel(playerLevel + 1);
}

bool StoryModeState::addExperience(int xp) {
    playerExperience += xp;
    int requiredXP = getExperienceForNextLevel();

    if (playerExperience >= requiredXP) {
        playerLevel++;
        playerExperience -= requiredXP;
        return true; // Leveled up
    }

    return false;
}

bool StoryModeState::canRankUp() const {
    auto requirements = StoryModeHelpers::getRankRequirements(
        static_cast<ClanRank>(static_cast<int>(playerRank) + 1)
    );

    return playerLevel >= requirements.requiredLevel &&
           questsCompleted >= requirements.requiredQuestsCompleted &&
           enemiesDefeated >= requirements.requiredEnemiesDefeated;
}

const char* StoryModeState::getRankName(ClanRank rank) {
    switch (rank) {
        case ClanRank::Outsider: return "Outsider";
        case ClanRank::Apprentice: return "Apprentice";
        case ClanRank::Warrior: return "Warrior";
        case ClanRank::SeniorWarrior: return "Senior Warrior";
        case ClanRank::Deputy: return "Deputy";
        case ClanRank::Leader: return "Leader";
        default: return "Unknown";
    }
}

const char* StoryModeState::getClanName(Clan clan) {
    switch (clan) {
        case Clan::MistClan: return "MistClan";
        case Clan::StormClan: return "StormClan";
        case Clan::EmberClan: return "EmberClan";
        case Clan::FrostClan: return "FrostClan";
        default: return "Unknown";
    }
}

const char* StoryModeState::getRelationName(ClanRelation relation) {
    switch (relation) {
        case ClanRelation::Ally: return "Ally";
        case ClanRelation::Neutral: return "Neutral";
        case ClanRelation::Hostile: return "Hostile";
        default: return "Unknown";
    }
}

// ============================================================================
// StoryModeSystem Implementation
// ============================================================================

StoryModeSystem::StoryModeSystem(int priority)
    : System(priority) {
}

void StoryModeSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);

    // Initialize subsystems
    territorySystem_.initialize();
    initializeDefaultRelationships();
    initializeStoryChapters();
}

void StoryModeSystem::update(float dt) {
    if (!state_.isActive) {
        return;
    }

    // Update playtime
    state_.totalPlayTime += dt;

    // Update territory system
    territorySystem_.update(dt);

    // Check for rank promotion opportunities
    checkRankPromotion();

    // Update story progress (0-100%)
    if (chapters_.size() > 0) {
        int completedChapters = 0;
        for (const auto& chapter : chapters_) {
            if (chapter.completed) {
                completedChapters++;
            }
        }
        state_.storyProgress = (completedChapters / static_cast<float>(chapters_.size())) * 100.0f;
    }

    // Check for nearby story events if we have player entity
    if (ecs_ && ecs_->isAlive(playerEntity_)) {
        auto* transform = ecs_->getComponent<Engine::Transform>(playerEntity_);
        if (transform) {
            checkStoryEvents(transform->position);
        }
    }
}

void StoryModeSystem::shutdown() {
    // Cleanup
}

// ========================================================================
// Story Mode Control
// ========================================================================

void StoryModeSystem::startStoryMode(Clan initialClan) {
    state_.isActive = true;
    state_.playerClan = initialClan;
    state_.playerRank = ClanRank::Outsider;
    state_.playerLevel = 1;
    state_.playerExperience = 0;
    state_.currentChapter = 0;
    state_.storyProgress = 0.0f;
    state_.enemiesDefeated = 0;
    state_.questsCompleted = 0;
    state_.territoriesCaptured = 0;
    state_.totalPlayTime = 0.0f;
    state_.mysticalConnections = 0;

    // Reset skills
    skills_.reset();

    // Initialize clan relationships
    initializeDefaultRelationships();

    // Set default mentor based on clan
    switch (initialClan) {
        case Clan::MistClan:
            state_.mentorName = "Shadowwhisker";
            break;
        case Clan::StormClan:
            state_.mentorName = "Thunderstrike";
            break;
        case Clan::EmberClan:
            state_.mentorName = "Blazeclaw";
            break;
        case Clan::FrostClan:
            state_.mentorName = "Frostfang";
            break;
    }

    // Trigger callback
    if (clanJoinCallback_) {
        clanJoinCallback_(initialClan);
    }
}

void StoryModeSystem::endStoryMode() {
    state_.isActive = false;
}

// ========================================================================
// Clan Management
// ========================================================================

void StoryModeSystem::joinClan(Clan clan) {
    state_.playerClan = clan;
    state_.playerRank = ClanRank::Apprentice; // Start as apprentice when joining

    // Update relationships
    initializeDefaultRelationships();

    if (clanJoinCallback_) {
        clanJoinCallback_(clan);
    }
}

bool StoryModeSystem::promoteRank() {
    if (!state_.canRankUp()) {
        return false;
    }

    if (state_.playerRank >= ClanRank::Leader) {
        return false; // Already max rank
    }

    ClanRank oldRank = state_.playerRank;
    state_.playerRank = static_cast<ClanRank>(static_cast<int>(state_.playerRank) + 1);

    // Apply rank bonuses
    applyRankBonuses();

    // Unlock territories based on rank
    auto newRankInt = static_cast<int>(state_.playerRank);
    if (newRankInt >= 2) { // Warrior
        territorySystem_.unlockTerritory("Contested Battleground", state_.playerClan);
    }
    if (newRankInt >= 3) { // Senior Warrior
        // Unlock advanced territories
    }

    // Trigger callback
    if (rankPromotionCallback_) {
        rankPromotionCallback_(oldRank, state_.playerRank);
    }

    return true;
}

void StoryModeSystem::demoteRank() {
    if (state_.playerRank > ClanRank::Outsider) {
        ClanRank oldRank = state_.playerRank;
        state_.playerRank = static_cast<ClanRank>(static_cast<int>(state_.playerRank) - 1);

        if (rankPromotionCallback_) {
            rankPromotionCallback_(oldRank, state_.playerRank);
        }
    }
}

void StoryModeSystem::updateClanRelation(Clan clan, ClanRelation relation) {
    state_.clanRelationships[clan] = relation;
}

ClanRelation StoryModeSystem::getClanRelation(Clan clan) const {
    auto it = state_.clanRelationships.find(clan);
    if (it != state_.clanRelationships.end()) {
        return it->second;
    }
    return ClanRelation::Neutral;
}

void StoryModeSystem::setMentor(const std::string& mentorName) {
    state_.mentorName = mentorName;
}

// ========================================================================
// Progression
// ========================================================================

void StoryModeSystem::addExperience(int xp) {
    bool leveledUp = state_.addExperience(xp);

    if (leveledUp && levelUpCallback_) {
        levelUpCallback_(state_.playerLevel);
    }
}

void StoryModeSystem::addMysticalConnection() {
    state_.mysticalConnections++;
}

void StoryModeSystem::unlockTerritory(const std::string& territory) {
    if (std::find(state_.territoryAccess.begin(), state_.territoryAccess.end(), territory)
        == state_.territoryAccess.end()) {
        state_.territoryAccess.push_back(territory);
        territorySystem_.unlockTerritory(territory, state_.playerClan);
    }
}

bool StoryModeSystem::isTerritoryUnlocked(const std::string& territory) const {
    return std::find(state_.territoryAccess.begin(), state_.territoryAccess.end(), territory)
           != state_.territoryAccess.end();
}

// ========================================================================
// Story Events & Chapters
// ========================================================================

void StoryModeSystem::registerStoryEvent(const StoryEvent& event) {
    events_[event.eventId] = event;
}

void StoryModeSystem::triggerStoryEvent(const std::string& eventId) {
    auto it = events_.find(eventId);
    if (it != events_.end()) {
        StoryEvent& event = it->second;

        // Check if already triggered and not repeatable
        if (event.triggered && !event.repeatable) {
            return;
        }

        // Check rank requirement
        if (state_.playerRank < event.minimumRank) {
            return;
        }

        // Trigger event
        event.triggered = true;
        if (event.onTrigger) {
            event.onTrigger();
        }

        // Track completion
        if (!event.repeatable) {
            completedEvents_.push_back(eventId);
        }
    }
}

void StoryModeSystem::checkStoryEvents(const Engine::vec3& playerPosition) {
    for (auto& [eventId, event] : events_) {
        // Skip if already triggered and not repeatable
        if (event.triggered && !event.repeatable) {
            continue;
        }

        // Check distance to trigger location
        float distance = (playerPosition - event.triggerLocation).length();
        if (distance <= event.triggerRadius) {
            triggerStoryEvent(eventId);
        }
    }
}

void StoryModeSystem::addStoryChapter(const StoryChapter& chapter) {
    chapters_.push_back(chapter);

    // Auto-unlock first chapter
    if (chapters_.size() == 1) {
        chapters_[0].unlocked = true;
    }
}

void StoryModeSystem::completeStoryChapter(int chapterNumber) {
    for (auto& chapter : chapters_) {
        if (chapter.chapterNumber == chapterNumber && !chapter.completed) {
            chapter.completed = true;

            // Grant rewards
            addExperience(chapter.experienceReward);

            for (const auto& skillReward : chapter.skillRewards) {
                // Parse skill rewards (format: "skillname:amount")
                size_t colonPos = skillReward.find(':');
                if (colonPos != std::string::npos) {
                    std::string skillName = skillReward.substr(0, colonPos);
                    int amount = std::stoi(skillReward.substr(colonPos + 1));
                    skills_.addSkillExperience(skillName, amount);
                }
            }

            for (const auto& territory : chapter.territoryUnlocks) {
                unlockTerritory(territory);
            }

            // Unlock next chapters that have this as prerequisite
            for (auto& nextChapter : chapters_) {
                bool allPrereqsMet = true;
                for (int prereq : nextChapter.prerequisiteChapters) {
                    bool found = false;
                    for (const auto& ch : chapters_) {
                        if (ch.chapterNumber == prereq && ch.completed) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        allPrereqsMet = false;
                        break;
                    }
                }

                if (allPrereqsMet && state_.playerRank >= nextChapter.requiredRank) {
                    nextChapter.unlocked = true;
                }
            }

            // Update current chapter
            state_.currentChapter = chapterNumber + 1;
            state_.questsCompleted++;

            // Trigger callback
            if (chapterCompleteCallback_) {
                chapterCompleteCallback_(chapterNumber);
            }

            break;
        }
    }
}

const StoryChapter* StoryModeSystem::getCurrentChapter() const {
    for (const auto& chapter : chapters_) {
        if (chapter.chapterNumber == state_.currentChapter && !chapter.completed) {
            return &chapter;
        }
    }
    return nullptr;
}

// ========================================================================
// Clan Bonuses
// ========================================================================

float StoryModeSystem::getClanAttackBonus() const {
    float bonus = 1.0f;
    calculateClanBonuses(state_.playerClan, bonus, bonus, bonus, bonus);
    return bonus;
}

float StoryModeSystem::getClanDefenseBonus() const {
    float attack, defense, speed, stealth;
    attack = defense = speed = stealth = 1.0f;
    calculateClanBonuses(state_.playerClan, attack, defense, speed, stealth);
    return defense;
}

float StoryModeSystem::getClanSpeedBonus() const {
    float attack, defense, speed, stealth;
    attack = defense = speed = stealth = 1.0f;
    calculateClanBonuses(state_.playerClan, attack, defense, speed, stealth);
    return speed;
}

float StoryModeSystem::getClanStealthBonus() const {
    float attack, defense, speed, stealth;
    attack = defense = speed = stealth = 1.0f;
    calculateClanBonuses(state_.playerClan, attack, defense, speed, stealth);
    return stealth;
}

ClanElementType StoryModeSystem::getClanElement() const {
    switch (state_.playerClan) {
        case Clan::MistClan: return ClanElementType::Shadow;
        case Clan::StormClan: return ClanElementType::Lightning;
        case Clan::EmberClan: return ClanElementType::Fire;
        case Clan::FrostClan: return ClanElementType::Ice;
        default: return ClanElementType::Shadow;
    }
}

void StoryModeSystem::getTotalBonuses(
    float& attack,
    float& defense,
    float& speed,
    float& stealth
) const {
    attack = defense = speed = stealth = 1.0f;

    // Clan bonuses
    calculateClanBonuses(state_.playerClan, attack, defense, speed, stealth);

    // Rank bonuses
    float rankBonus = getRankBonus();
    attack *= rankBonus;
    defense *= rankBonus;

    // Skill bonuses
    attack *= skills_.getHuntingDamageBonus();
    attack *= skills_.getFightingComboMultiplier();
    defense *= skills_.getHealingEffectiveness();
    speed *= skills_.getSwimmingSpeedMultiplier();
    stealth *= (1.0f - skills_.getStealthAggroReduction());
}

// ========================================================================
// Callbacks
// ========================================================================

void StoryModeSystem::onRankPromotion(void (*callback)(ClanRank, ClanRank)) {
    rankPromotionCallback_ = callback;
}

void StoryModeSystem::onLevelUp(void (*callback)(int)) {
    levelUpCallback_ = callback;
}

void StoryModeSystem::onChapterComplete(void (*callback)(int)) {
    chapterCompleteCallback_ = callback;
}

void StoryModeSystem::onClanJoin(void (*callback)(Clan)) {
    clanJoinCallback_ = callback;
}

// ========================================================================
// Serialization
// ========================================================================

std::string StoryModeSystem::serialize() const {
    std::stringstream ss;

    // Basic state
    ss << state_.isActive << ","
       << static_cast<int>(state_.playerClan) << ","
       << static_cast<int>(state_.playerRank) << ","
       << state_.mentorName << ","
       << state_.mysticalConnections << ","
       << state_.storyProgress << ","
       << state_.currentChapter << ","
       << state_.playerLevel << ","
       << state_.playerExperience << ","
       << state_.enemiesDefeated << ","
       << state_.questsCompleted << ","
       << state_.territoriesCaptured << ","
       << state_.totalPlayTime << ";";

    // Skills
    ss << skills_.serialize() << ";";

    // Territory system
    ss << territorySystem_.serialize() << ";";

    // Relationships
    ss << state_.clanRelationships.size() << ",";
    for (const auto& [clan, relation] : state_.clanRelationships) {
        ss << static_cast<int>(clan) << ":" << static_cast<int>(relation) << ",";
    }
    ss << ";";

    // Territory access
    ss << state_.territoryAccess.size() << ",";
    for (const auto& territory : state_.territoryAccess) {
        ss << territory << ",";
    }
    ss << ";";

    return ss.str();
}

bool StoryModeSystem::deserialize(const std::string& data) {
    std::stringstream ss(data);
    char delimiter;

    // Basic state
    int clanInt, rankInt;
    ss >> state_.isActive >> delimiter
       >> clanInt >> delimiter
       >> rankInt >> delimiter;

    std::getline(ss, state_.mentorName, ',');

    ss >> state_.mysticalConnections >> delimiter
       >> state_.storyProgress >> delimiter
       >> state_.currentChapter >> delimiter
       >> state_.playerLevel >> delimiter
       >> state_.playerExperience >> delimiter
       >> state_.enemiesDefeated >> delimiter
       >> state_.questsCompleted >> delimiter
       >> state_.territoriesCaptured >> delimiter
       >> state_.totalPlayTime >> delimiter;

    state_.playerClan = static_cast<Clan>(clanInt);
    state_.playerRank = static_cast<ClanRank>(rankInt);

    // Skills
    std::string skillsData;
    std::getline(ss, skillsData, ';');
    skills_.deserialize(skillsData);

    // Territory system
    std::string territoryData;
    std::getline(ss, territoryData, ';');
    territorySystem_.deserialize(territoryData);

    // Relationships
    size_t relationCount;
    ss >> relationCount >> delimiter;
    state_.clanRelationships.clear();
    for (size_t i = 0; i < relationCount; ++i) {
        int clan, relation;
        char colon;
        ss >> clan >> colon >> relation >> delimiter;
        state_.clanRelationships[static_cast<Clan>(clan)] =
            static_cast<ClanRelation>(relation);
    }
    ss >> delimiter;

    // Territory access
    size_t territoryCount;
    ss >> territoryCount >> delimiter;
    state_.territoryAccess.clear();
    for (size_t i = 0; i < territoryCount; ++i) {
        std::string territory;
        std::getline(ss, territory, ',');
        if (!territory.empty()) {
            state_.territoryAccess.push_back(territory);
        }
    }

    return true;
}

// ========================================================================
// Private Helper Methods
// ========================================================================

void StoryModeSystem::initializeDefaultRelationships() {
    // Set default relationships based on player clan
    switch (state_.playerClan) {
        case Clan::MistClan:
            state_.clanRelationships[Clan::MistClan] = ClanRelation::Ally;
            state_.clanRelationships[Clan::StormClan] = ClanRelation::Neutral;
            state_.clanRelationships[Clan::EmberClan] = ClanRelation::Hostile;
            state_.clanRelationships[Clan::FrostClan] = ClanRelation::Neutral;
            break;

        case Clan::StormClan:
            state_.clanRelationships[Clan::MistClan] = ClanRelation::Neutral;
            state_.clanRelationships[Clan::StormClan] = ClanRelation::Ally;
            state_.clanRelationships[Clan::EmberClan] = ClanRelation::Neutral;
            state_.clanRelationships[Clan::FrostClan] = ClanRelation::Hostile;
            break;

        case Clan::EmberClan:
            state_.clanRelationships[Clan::MistClan] = ClanRelation::Hostile;
            state_.clanRelationships[Clan::StormClan] = ClanRelation::Neutral;
            state_.clanRelationships[Clan::EmberClan] = ClanRelation::Ally;
            state_.clanRelationships[Clan::FrostClan] = ClanRelation::Neutral;
            break;

        case Clan::FrostClan:
            state_.clanRelationships[Clan::MistClan] = ClanRelation::Neutral;
            state_.clanRelationships[Clan::StormClan] = ClanRelation::Hostile;
            state_.clanRelationships[Clan::EmberClan] = ClanRelation::Neutral;
            state_.clanRelationships[Clan::FrostClan] = ClanRelation::Ally;
            break;
    }
}

void StoryModeSystem::initializeStoryChapters() {
    // Chapter 1: Awakening
    StoryChapter chapter1;
    chapter1.chapterNumber = 1;
    chapter1.title = "The Awakening";
    chapter1.description = "Begin your journey and prove yourself worthy of joining a clan.";
    chapter1.requiredRank = ClanRank::Outsider;
    chapter1.requiredLevel = 1;
    chapter1.experienceReward = 500;
    chapter1.skillRewards = {"hunting:10", "fighting:10"};
    chapter1.unlocked = true;
    addStoryChapter(chapter1);

    // Chapter 2: Apprenticeship
    StoryChapter chapter2;
    chapter2.chapterNumber = 2;
    chapter2.title = "Trials of the Apprentice";
    chapter2.description = "Learn the ways of your clan under your mentor's guidance.";
    chapter2.requiredRank = ClanRank::Apprentice;
    chapter2.requiredLevel = 5;
    chapter2.prerequisiteChapters = {1};
    chapter2.experienceReward = 1000;
    chapter2.skillRewards = {"hunting:15", "fighting:15", "tracking:10"};
    addStoryChapter(chapter2);

    // Chapter 3: The Warrior's Path
    StoryChapter chapter3;
    chapter3.chapterNumber = 3;
    chapter3.title = "The Warrior's Path";
    chapter3.description = "Prove yourself in battle and earn your warrior name.";
    chapter3.requiredRank = ClanRank::Warrior;
    chapter3.requiredLevel = 10;
    chapter3.prerequisiteChapters = {2};
    chapter3.experienceReward = 2000;
    chapter3.skillRewards = {"fighting:20", "stealth:15", "leadership:10"};
    chapter3.territoryUnlocks = {"Contested Battleground"};
    addStoryChapter(chapter3);

    // Chapter 4: Ancient Mysteries
    StoryChapter chapter4;
    chapter4.chapterNumber = 4;
    chapter4.title = "The Ancient Mysteries";
    chapter4.description = "Uncover the mystical secrets of the Sacred Territories.";
    chapter4.requiredRank = ClanRank::SeniorWarrior;
    chapter4.requiredLevel = 20;
    chapter4.prerequisiteChapters = {3};
    chapter4.experienceReward = 3500;
    chapter4.skillRewards = {"mysticism:25", "healing:20"};
    chapter4.territoryUnlocks = {"Mystical Nexus"};
    addStoryChapter(chapter4);

    // Chapter 5: Leadership
    StoryChapter chapter5;
    chapter5.chapterNumber = 5;
    chapter5.title = "Rise of a Leader";
    chapter5.description = "Become Deputy and lead your clan to glory.";
    chapter5.requiredRank = ClanRank::Deputy;
    chapter5.requiredLevel = 30;
    chapter5.prerequisiteChapters = {4};
    chapter5.experienceReward = 5000;
    chapter5.skillRewards = {"leadership:30", "fighting:25", "mysticism:20"};
    addStoryChapter(chapter5);

    // Final Chapter
    StoryChapter finalChapter;
    finalChapter.chapterNumber = 6;
    finalChapter.title = "Destiny of the Clans";
    finalChapter.description = "Unite or conquer the clans in the ultimate battle.";
    finalChapter.requiredRank = ClanRank::Leader;
    finalChapter.requiredLevel = 50;
    finalChapter.prerequisiteChapters = {5};
    finalChapter.experienceReward = 10000;
    finalChapter.skillRewards = {"fighting:50", "mysticism:50", "leadership:50"};
    addStoryChapter(finalChapter);
}

void StoryModeSystem::checkRankPromotion() {
    // Auto-promote if eligible (optional, could be manual via UI)
    // For now, just check and notify
}

void StoryModeSystem::applyRankBonuses() {
    // Rank bonuses are applied through getRankBonus()
    // This method can be used to apply one-time bonuses on promotion
}

float StoryModeSystem::getRankBonus() const {
    switch (state_.playerRank) {
        case ClanRank::Outsider: return 1.0f;
        case ClanRank::Apprentice: return 1.05f;  // +5%
        case ClanRank::Warrior: return 1.15f;     // +15%
        case ClanRank::SeniorWarrior: return 1.25f; // +25%
        case ClanRank::Deputy: return 1.40f;      // +40%
        case ClanRank::Leader: return 1.60f;      // +60%
        default: return 1.0f;
    }
}

void StoryModeSystem::calculateClanBonuses(
    Clan clan,
    float& attack,
    float& defense,
    float& speed,
    float& stealth
) const {
    switch (clan) {
        case Clan::MistClan:
            // Stealth specialists
            stealth = 1.5f;
            speed = 1.1f;
            attack = 1.0f;
            defense = 0.95f;
            break;

        case Clan::StormClan:
            // Speed and agility
            speed = 1.4f;
            attack = 1.15f;
            defense = 1.0f;
            stealth = 1.1f;
            break;

        case Clan::EmberClan:
            // Raw attack power
            attack = 1.5f;
            speed = 1.05f;
            defense = 0.9f;
            stealth = 0.95f;
            break;

        case Clan::FrostClan:
            // Defense and endurance
            defense = 1.5f;
            attack = 1.05f;
            speed = 0.9f;
            stealth = 1.0f;
            break;
    }
}

// ============================================================================
// StoryModeHelpers Implementation
// ============================================================================

namespace StoryModeHelpers {

const char* getElementName(ClanElementType element) {
    switch (element) {
        case ClanElementType::Shadow: return "Shadow";
        case ClanElementType::Lightning: return "Lightning";
        case ClanElementType::Fire: return "Fire";
        case ClanElementType::Ice: return "Ice";
        default: return "Unknown";
    }
}

Engine::vec3 getElementColor(ClanElementType element) {
    switch (element) {
        case ClanElementType::Shadow: return Engine::vec3(0.2f, 0.1f, 0.4f);    // Dark purple
        case ClanElementType::Lightning: return Engine::vec3(0.9f, 0.9f, 0.2f); // Bright yellow
        case ClanElementType::Fire: return Engine::vec3(1.0f, 0.3f, 0.1f);      // Orange-red
        case ClanElementType::Ice: return Engine::vec3(0.5f, 0.8f, 1.0f);       // Light blue
        default: return Engine::vec3(1.0f, 1.0f, 1.0f);
    }
}

int calculateExperienceForLevel(int level) {
    // Exponential curve: XP = 100 * (level^1.8)
    return static_cast<int>(100.0f * std::pow(level, 1.8f));
}

RankRequirements getRankRequirements(ClanRank rank) {
    RankRequirements req;

    switch (rank) {
        case ClanRank::Outsider:
            req.requiredLevel = 1;
            req.requiredQuestsCompleted = 0;
            req.requiredEnemiesDefeated = 0;
            req.description = "No requirements";
            break;

        case ClanRank::Apprentice:
            req.requiredLevel = 1;
            req.requiredQuestsCompleted = 0;
            req.requiredEnemiesDefeated = 0;
            req.description = "Join a clan";
            break;

        case ClanRank::Warrior:
            req.requiredLevel = 5;
            req.requiredQuestsCompleted = 3;
            req.requiredEnemiesDefeated = 25;
            req.description = "Prove yourself in combat";
            break;

        case ClanRank::SeniorWarrior:
            req.requiredLevel = 15;
            req.requiredQuestsCompleted = 10;
            req.requiredEnemiesDefeated = 100;
            req.description = "Demonstrate veteran combat skills";
            break;

        case ClanRank::Deputy:
            req.requiredLevel = 25;
            req.requiredQuestsCompleted = 20;
            req.requiredEnemiesDefeated = 250;
            req.description = "Show exceptional leadership";
            break;

        case ClanRank::Leader:
            req.requiredLevel = 40;
            req.requiredQuestsCompleted = 35;
            req.requiredEnemiesDefeated = 500;
            req.description = "Achieve legendary status";
            break;

        default:
            req.requiredLevel = 999;
            req.requiredQuestsCompleted = 999;
            req.requiredEnemiesDefeated = 9999;
            req.description = "Unknown rank";
            break;
    }

    return req;
}

} // namespace StoryModeHelpers

// ============================================================================
// Save/Load Helper Methods
// ============================================================================

bool StoryModeSystem::isStoryComplete() const {
    // Story is complete when all chapters are finished
    if (chapters_.empty()) {
        return false;
    }

    for (const auto& chapter : chapters_) {
        if (!chapter.completed) {
            return false;
        }
    }

    return true;
}

void StoryModeSystem::setChapter(int chapterIndex) {
    state_.currentChapter = chapterIndex;

    // Ensure all chapters up to this one are marked as completed
    for (auto& chapter : chapters_) {
        if (chapter.chapterNumber < chapterIndex) {
            chapter.completed = true;
            chapter.unlocked = true;
        } else if (chapter.chapterNumber == chapterIndex) {
            chapter.unlocked = true;
        }
    }
}

void StoryModeSystem::setMission(int missionIndex) {
    state_.currentMission = missionIndex;
}

void StoryModeSystem::advanceStory() {
    // Advance to next mission in current chapter
    state_.currentMission++;

    // Check if we've completed all missions in the chapter
    // For now, assume 3 missions per chapter
    const int missionsPerChapter = 3;
    if (state_.currentMission > missionsPerChapter) {
        // Complete current chapter and move to next
        completeStoryChapter(state_.currentChapter);
        state_.currentChapter++;
        state_.currentMission = 1;
    }
}

} // namespace CatGame

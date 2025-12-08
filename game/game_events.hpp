#pragma once

#include "../engine/ecs/Entity.hpp"
#include "../engine/math/Vector.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <typeindex>
#include <any>

namespace CatGame {

// Forward declarations
struct QuestReward;
enum class DamageType;
enum class ElementType;

/**
 * ============================================================================
 * GAME EVENT DEFINITIONS
 * Events that flow between systems to coordinate gameplay
 * ============================================================================
 */

/**
 * Fired when an enemy is killed
 */
struct EnemyKilledEvent {
    CatEngine::Entity enemy;
    CatEngine::Entity killer;
    std::string enemyType;
    int xpReward;
    Engine::vec3 position;
    float timestamp;

    EnemyKilledEvent(CatEngine::Entity e, CatEngine::Entity k, const std::string& type, int xp, const Engine::vec3& pos)
        : enemy(e), killer(k), enemyType(type), xpReward(xp), position(pos), timestamp(0.0f) {}
};

/**
 * Fired when a quest is completed
 */
struct QuestCompletedEvent {
    std::string questId;
    std::string questName;
    int xpReward;
    int currencyReward;
    std::vector<std::string> itemRewards;
    bool wasMainQuest;

    QuestCompletedEvent(const std::string& id, const std::string& name)
        : questId(id), questName(name), xpReward(0), currencyReward(0), wasMainQuest(false) {}
};

/**
 * Fired when a quest objective progresses
 */
struct QuestProgressEvent {
    std::string questId;
    std::string objectiveId;
    int currentProgress;
    int requiredProgress;
    bool objectiveCompleted;

    QuestProgressEvent(const std::string& qId, const std::string& oId, int current, int required)
        : questId(qId), objectiveId(oId), currentProgress(current),
          requiredProgress(required), objectiveCompleted(current >= required) {}
};

/**
 * Fired when player levels up
 */
struct LevelUpEvent {
    int oldLevel;
    int newLevel;
    int skillPointsGained;
    std::vector<std::string> newAbilitiesUnlocked;
    float totalXP;

    LevelUpEvent(int oldLvl, int newLvl, int skillPoints = 1)
        : oldLevel(oldLvl), newLevel(newLvl), skillPointsGained(skillPoints), totalXP(0.0f) {}
};

/**
 * Fired when damage is dealt
 */
struct DamageEvent {
    CatEngine::Entity target;
    CatEngine::Entity source;
    float amount;
    std::string damageType;  // "physical", "fire", "ice", "lightning", etc.
    bool wasCritical;
    bool wasBlocked;
    bool wasDodged;
    Engine::vec3 hitPosition;
    Engine::vec3 hitNormal;

    DamageEvent(CatEngine::Entity tgt, CatEngine::Entity src, float amt, const std::string& type)
        : target(tgt), source(src), amount(amt), damageType(type),
          wasCritical(false), wasBlocked(false), wasDodged(false),
          hitPosition(Engine::vec3::zero()), hitNormal(Engine::vec3(0, 1, 0)) {}
};

/**
 * Fired when an entity dies
 */
struct EntityDeathEvent {
    CatEngine::Entity entity;
    CatEngine::Entity killer;
    std::string causeOfDeath;
    Engine::vec3 deathPosition;
    bool wasPlayer;

    EntityDeathEvent(CatEngine::Entity ent, CatEngine::Entity kill, const std::string& cause)
        : entity(ent), killer(kill), causeOfDeath(cause),
          deathPosition(Engine::vec3::zero()), wasPlayer(false) {}
};

/**
 * Fired when a spell is cast
 */
struct SpellCastEvent {
    CatEngine::Entity caster;
    std::string spellId;
    std::string spellName;
    float manaCost;
    Engine::vec3 castPosition;
    Engine::vec3 targetPosition;
    CatEngine::Entity targetEntity;
    bool wasSuccessful;

    SpellCastEvent(CatEngine::Entity cast, const std::string& id, const std::string& name)
        : caster(cast), spellId(id), spellName(name), manaCost(0.0f),
          castPosition(Engine::vec3::zero()), targetPosition(Engine::vec3::zero()),
          targetEntity(CatEngine::NULL_ENTITY), wasSuccessful(true) {}
};

/**
 * Fired when player interacts with NPC
 */
struct DialogEvent {
    std::string npcId;
    std::string npcName;
    std::string dialogId;
    std::string selectedOption;
    int optionIndex;
    bool dialogStarted;
    bool dialogEnded;

    DialogEvent(const std::string& npc, const std::string& name, const std::string& dialog)
        : npcId(npc), npcName(name), dialogId(dialog), optionIndex(-1),
          dialogStarted(false), dialogEnded(false) {}
};

/**
 * Fired when player picks up an item
 */
struct ItemPickupEvent {
    CatEngine::Entity entity;
    std::string itemId;
    std::string itemName;
    int quantity;
    Engine::vec3 pickupPosition;

    ItemPickupEvent(CatEngine::Entity ent, const std::string& id, const std::string& name, int qty = 1)
        : entity(ent), itemId(id), itemName(name), quantity(qty),
          pickupPosition(Engine::vec3::zero()) {}
};

/**
 * Fired when wave starts
 */
struct WaveStartEvent {
    int waveNumber;
    int totalEnemies;
    float waveMultiplier;
    std::vector<std::string> enemyTypes;

    WaveStartEvent(int wave, int enemies)
        : waveNumber(wave), totalEnemies(enemies), waveMultiplier(1.0f) {}
};

/**
 * Fired when wave completes
 */
struct WaveCompleteEvent {
    int waveNumber;
    float completionTime;
    int enemiesKilled;
    int xpReward;
    int currencyReward;

    WaveCompleteEvent(int wave)
        : waveNumber(wave), completionTime(0.0f), enemiesKilled(0),
          xpReward(0), currencyReward(0) {}
};

/**
 * Fired when player enters/exits a territory
 */
struct TerritoryChangeEvent {
    std::string previousTerritory;
    std::string newTerritory;
    CatEngine::Entity player;
    bool firstVisit;

    TerritoryChangeEvent(const std::string& prev, const std::string& next, CatEngine::Entity p)
        : previousTerritory(prev), newTerritory(next), player(p), firstVisit(false) {}
};

/**
 * Fired when time of day changes significantly
 */
struct TimeOfDayChangeEvent {
    float previousTime;  // 0.0-1.0
    float currentTime;
    bool isNow Night;
    bool wasDawn;
    bool wasDusk;
    int dayNumber;

    TimeOfDayChangeEvent(float prev, float current, int day)
        : previousTime(prev), currentTime(current), isNowNight(false),
          wasDawn(false), wasDusk(false), dayNumber(day) {}
};

/**
 * Fired when player ranks up in clan
 */
struct ClanRankUpEvent {
    std::string clanName;
    int previousRank;
    int newRank;
    std::string rankName;
    std::vector<std::string> abilitiesUnlocked;

    ClanRankUpEvent(const std::string& clan, int prevRank, int newRank)
        : clanName(clan), previousRank(prevRank), newRank(newRank) {}
};

/**
 * Fired when player changes clan
 */
struct ClanChangeEvent {
    std::string previousClan;
    std::string newClan;
    bool wasForcedChange;

    ClanChangeEvent(const std::string& prev, const std::string& next)
        : previousClan(prev), newClan(next), wasForcedChange(false) {}
};

/**
 * Fired when achievement is unlocked
 */
struct AchievementUnlockedEvent {
    std::string achievementId;
    std::string achievementName;
    std::string description;
    int rewardXP;

    AchievementUnlockedEvent(const std::string& id, const std::string& name, const std::string& desc)
        : achievementId(id), achievementName(name), description(desc), rewardXP(0) {}
};

/**
 * Fired when game is saved
 */
struct GameSavedEvent {
    std::string saveSlot;
    float playtime;
    int playerLevel;
    bool wasAutoSave;

    GameSavedEvent(const std::string& slot)
        : saveSlot(slot), playtime(0.0f), playerLevel(1), wasAutoSave(false) {}
};

/**
 * Fired when game is loaded
 */
struct GameLoadedEvent {
    std::string saveSlot;
    float playtime;
    int playerLevel;

    GameLoadedEvent(const std::string& slot)
        : saveSlot(slot), playtime(0.0f), playerLevel(1) {}
};

/**
 * Fired when combo is achieved
 */
struct ComboEvent {
    int comboCount;
    float comboDamageMultiplier;
    std::string comboType;  // "melee", "magic", "ranged"
    CatEngine::Entity attacker;

    ComboEvent(int count, float multiplier, const std::string& type, CatEngine::Entity att)
        : comboCount(count), comboDamageMultiplier(multiplier), comboType(type), attacker(att) {}
};

/**
 * Fired when status effect is applied
 */
struct StatusEffectAppliedEvent {
    CatEngine::Entity target;
    std::string effectId;
    std::string effectName;
    float duration;
    int stackCount;

    StatusEffectAppliedEvent(CatEngine::Entity tgt, const std::string& id, const std::string& name, float dur)
        : target(tgt), effectId(id), effectName(name), duration(dur), stackCount(1) {}
};

/**
 * ============================================================================
 * EVENT BUS - Central event dispatching system
 * ============================================================================
 */

/**
 * Type-safe event bus for inter-system communication
 *
 * Usage:
 *   // Subscribe to events
 *   eventBus.subscribe<EnemyKilledEvent>([](const EnemyKilledEvent& e) {
 *       // Handle enemy death
 *   });
 *
 *   // Publish events
 *   EnemyKilledEvent event(enemy, player, "dog", 100, pos);
 *   eventBus.publish(event);
 */
class GameEventBus {
public:
    /**
     * Subscribe to an event type with a handler function
     * @tparam T Event type
     * @param handler Callback function to handle the event
     * @return Subscription ID (for unsubscribing)
     */
    template<typename T>
    size_t subscribe(std::function<void(const T&)> handler) {
        auto typeId = std::type_index(typeid(T));
        size_t handlerId = nextHandlerId_++;

        // Store the handler wrapped in a type-erased container
        auto wrapper = [handler](const std::any& event) {
            handler(std::any_cast<const T&>(event));
        };

        handlers_[typeId].push_back({handlerId, wrapper});
        return handlerId;
    }

    /**
     * Unsubscribe from an event
     * @param handlerId The ID returned from subscribe()
     */
    void unsubscribe(size_t handlerId) {
        for (auto& [typeId, handlerList] : handlers_) {
            handlerList.erase(
                std::remove_if(handlerList.begin(), handlerList.end(),
                    [handlerId](const auto& pair) { return pair.first == handlerId; }),
                handlerList.end()
            );
        }
    }

    /**
     * Publish an event to all subscribers
     * @tparam T Event type
     * @param event The event to publish
     */
    template<typename T>
    void publish(const T& event) {
        auto typeId = std::type_index(typeid(T));

        // Store in immediate queue for this frame
        if (handlers_.find(typeId) != handlers_.end()) {
            for (const auto& [id, handler] : handlers_[typeId]) {
                handler(event);
            }
        }

        // Store in history (for debugging/replay)
        if (enableHistory_) {
            eventHistory_.push_back({typeId, std::make_any<T>(event)});
            if (eventHistory_.size() > maxHistorySize_) {
                eventHistory_.erase(eventHistory_.begin());
            }
        }
    }

    /**
     * Clear all event handlers
     */
    void clear() {
        handlers_.clear();
        eventHistory_.clear();
    }

    /**
     * Enable/disable event history (for debugging)
     */
    void setHistoryEnabled(bool enabled) {
        enableHistory_ = enabled;
    }

    /**
     * Set maximum history size
     */
    void setMaxHistorySize(size_t size) {
        maxHistorySize_ = size;
    }

    /**
     * Get number of subscribers for an event type
     */
    template<typename T>
    size_t getSubscriberCount() const {
        auto typeId = std::type_index(typeid(T));
        auto it = handlers_.find(typeId);
        return (it != handlers_.end()) ? it->second.size() : 0;
    }

private:
    // Handler storage: type -> list of (id, handler function)
    std::unordered_map<std::type_index, std::vector<std::pair<size_t, std::function<void(const std::any&)>>>> handlers_;

    // Event history for debugging
    std::vector<std::pair<std::type_index, std::any>> eventHistory_;

    // Configuration
    size_t nextHandlerId_ = 0;
    size_t maxHistorySize_ = 1000;
    bool enableHistory_ = false;
};

/**
 * ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * Get damage type name as string
 */
inline const char* getDamageTypeName(const std::string& type) {
    if (type == "physical") return "Physical";
    if (type == "fire") return "Fire";
    if (type == "ice") return "Ice";
    if (type == "lightning") return "Lightning";
    if (type == "poison") return "Poison";
    if (type == "shadow") return "Shadow";
    return "Unknown";
}

/**
 * Get element color for UI display
 */
inline Engine::vec3 getElementColor(const std::string& element) {
    if (element == "fire") return Engine::vec3(1.0f, 0.4f, 0.0f);
    if (element == "ice") return Engine::vec3(0.4f, 0.8f, 1.0f);
    if (element == "lightning") return Engine::vec3(1.0f, 1.0f, 0.3f);
    if (element == "poison") return Engine::vec3(0.4f, 1.0f, 0.4f);
    if (element == "shadow") return Engine::vec3(0.5f, 0.3f, 0.8f);
    return Engine::vec3(1.0f, 1.0f, 1.0f);  // Default white
}

} // namespace CatGame

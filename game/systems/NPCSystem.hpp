#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include "story_mode.hpp"  // For Clan enum
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <memory>

namespace CatGame {

// Forward declarations
class DialogSystem;
class MerchantSystem;

// Clan enum is now defined in story_mode.hpp
// Values: MistClan, StormClan, EmberClan, FrostClan

/**
 * NPC type enumeration
 */
enum class NPCType {
    Mentor,       // Teaches abilities
    QuestGiver,   // Gives quests
    Merchant,     // Sells items
    ClanLeader,   // Clan leader, main quest giver
    Healer,       // Healing services
    Trainer,      // Training services
    Villager      // Generic villager
};

/**
 * Dialog option in conversation
 */
struct NPCDialogOption {
    std::string text;                           // Option text shown to player
    std::string responseId;                     // ID of dialog node to jump to
    std::optional<std::string> requiredQuest;   // Quest must be active
    std::optional<int> requiredLevel;           // Player level requirement
    std::optional<Clan> requiredClan;           // Clan requirement
    std::function<void()> onSelect;             // Action when selected
    bool isAvailable = true;                    // Can be selected
};

/**
 * Dialog node in conversation tree
 */
struct DialogNode {
    std::string id;                             // Unique node ID
    std::string speakerName;                    // Who is speaking
    std::string text;                           // Dialog text
    std::vector<NPCDialogOption> options;       // Available options
    bool autoAdvance = false;                   // Automatically advance
    float autoAdvanceDelay = 2.0f;              // Delay before auto-advance
    std::string nextNodeId;                     // Next node if auto-advance
};

/**
 * Shop item data
 */
struct ShopItem {
    std::string itemId;                         // Item identifier
    std::string name;                           // Item name
    std::string description;                    // Item description
    int price;                                  // Purchase price
    int stock = -1;                             // -1 = unlimited
    bool isAvailable = true;                    // Can be purchased
    std::optional<Clan> requiredClan;           // Clan requirement
    std::optional<int> requiredLevel;           // Level requirement
};

/**
 * Training option data
 */
struct TrainingOption {
    std::string abilityId;                      // Ability to train
    std::string name;                           // Training name
    std::string description;                    // Description
    int cost;                                   // Training cost
    std::optional<Clan> requiredClan;           // Clan requirement
    std::optional<int> requiredLevel;           // Level requirement
    bool isAvailable = true;                    // Can be trained
};

/**
 * NPC data structure
 */
struct NPCData {
    std::string id;                             // Unique NPC ID
    std::string name;                           // NPC name
    NPCType type;                               // NPC type
    std::optional<Clan> clan;                   // NPC clan affiliation
    Engine::vec3 position;                      // World position
    float interactionRadius = 3.0f;             // Interaction distance
    std::string modelPath;                      // Model asset path
    std::string dialogTreeId;                   // Dialog tree identifier
    std::vector<std::string> questsToGive;      // Available quests
    std::vector<ShopItem> shopInventory;        // Shop items if merchant
    std::vector<TrainingOption> trainingOptions;// Training if trainer
    bool isInteractable = true;                 // Can interact
    CatEngine::Entity entity;                   // Associated entity
};

/**
 * NPC System
 * Manages NPCs, interactions, and dialog
 */
class NPCSystem : public CatEngine::System {
public:
    using InteractionCallback = std::function<void(const std::string& npcId)>;
    using DialogEndCallback = std::function<void()>;

    explicit NPCSystem(int priority = 150);
    ~NPCSystem() override = default;

    void init(CatEngine::ECS* ecs) override;
    void update(float dt) override;
    const char* getName() const override { return "NPCSystem"; }

    /**
     * Set player entity for distance checks
     */
    void setPlayer(CatEngine::Entity player) { playerEntity_ = player; }

    /**
     * Set player clan for dialog/shop filtering
     */
    void setPlayerClan(Clan clan) { playerClan_ = clan; }

    /**
     * Set player level for requirements
     */
    void setPlayerLevel(int level) { playerLevel_ = level; }

    /**
     * NPC Management
     */
    void spawnNPC(const NPCData& data);
    void despawnNPC(const std::string& npcId);
    NPCData* getNPC(const std::string& npcId);
    const NPCData* getNPC(const std::string& npcId) const;
    std::vector<NPCData*> getNPCsInRange(Engine::vec3 position, float radius);
    NPCData* getClosestInteractableNPC(Engine::vec3 playerPos);

    /**
     * Interaction
     */
    bool canInteract(const std::string& npcId, Engine::vec3 playerPos) const;
    void startInteraction(const std::string& npcId);
    void endInteraction();
    bool isInDialog() const { return inDialog_; }
    const std::string& getCurrentNPC() const { return currentNPCId_; }

    /**
     * Dialog
     */
    const DialogNode* getCurrentDialogNode() const;
    void selectDialogOption(int optionIndex);
    void advanceDialog();
    void skipDialog();

    /**
     * Merchant
     */
    void openShop(const std::string& npcId);
    void closeShop();
    bool isShopOpen() const { return shopOpen_; }
    const std::vector<ShopItem>& getShopInventory() const;
    bool purchaseItem(const std::string& itemId);

    /**
     * Trainer
     */
    void openTraining(const std::string& npcId);
    void closeTraining();
    bool isTrainingOpen() const { return trainingOpen_; }
    const std::vector<TrainingOption>& getTrainingOptions() const;
    bool trainAbility(const std::string& abilityId);

    /**
     * Quest
     */
    std::vector<std::string> getAvailableQuests(const std::string& npcId) const;

    /**
     * Callbacks
     */
    void setOnInteractionStart(InteractionCallback callback) { onInteractionStart_ = callback; }
    void setOnInteractionEnd(DialogEndCallback callback) { onInteractionEnd_ = callback; }
    void setOnDialogAdvance(InteractionCallback callback) { onDialogAdvance_ = callback; }

    /**
     * Load NPCs from JSON file
     */
    bool loadNPCsFromFile(const std::string& filepath);

    /**
     * Set dialog system reference
     */
    void setDialogSystem(std::shared_ptr<DialogSystem> dialogSystem) { dialogSystem_ = dialogSystem; }

    /**
     * Set merchant system reference
     */
    void setMerchantSystem(std::shared_ptr<MerchantSystem> merchantSystem) { merchantSystem_ = merchantSystem; }

    /**
     * Set quest check callback for dialog option requirements
     */
    using QuestCheckCallback = std::function<bool(const std::string& questId)>;
    void setQuestCheckCallback(const QuestCheckCallback& callback) { questCheckCallback_ = callback; }

private:
    /**
     * Update auto-advance dialog
     */
    void updateAutoAdvance(float dt);

    /**
     * Check if option is available for player
     */
    bool isOptionAvailable(const NPCDialogOption& option) const;

    /**
     * Calculate distance to NPC
     */
    float getDistanceToNPC(const NPCData& npc, Engine::vec3 position) const;

    // NPC storage
    std::unordered_map<std::string, NPCData> npcs_;

    // Player references
    CatEngine::Entity playerEntity_;
    Clan playerClan_ = Clan::None;
    int playerLevel_ = 1;

    // Dialog state
    bool inDialog_ = false;
    std::string currentNPCId_;
    std::string currentDialogNodeId_;
    float autoAdvanceTimer_ = 0.0f;

    // Shop state
    bool shopOpen_ = false;
    std::string currentShopNPCId_;

    // Training state
    bool trainingOpen_ = false;
    std::string currentTrainerNPCId_;

    // Systems
    std::shared_ptr<DialogSystem> dialogSystem_;
    std::shared_ptr<MerchantSystem> merchantSystem_;

    // Callbacks
    InteractionCallback onInteractionStart_;
    DialogEndCallback onInteractionEnd_;
    InteractionCallback onDialogAdvance_;
    QuestCheckCallback questCheckCallback_;
};

/**
 * Utility functions
 */
std::string clanToString(Clan clan);
Clan stringToClan(const std::string& str);
std::string npcTypeToString(NPCType type);
NPCType stringToNPCType(const std::string& str);

} // namespace CatGame

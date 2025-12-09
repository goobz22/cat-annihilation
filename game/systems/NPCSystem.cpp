#include "NPCSystem.hpp"
#include "DialogSystem.hpp"
#include "MerchantSystem.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/core/Logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>

using json = nlohmann::json;

namespace CatGame {

NPCSystem::NPCSystem(int priority)
    : System(priority)
{}

void NPCSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);
    CatEngine::Logger::info("NPCSystem initialized");
}

void NPCSystem::update(float dt) {
    if (!ecs_) {
        return;
    }

    // Update auto-advance dialog
    if (inDialog_) {
        updateAutoAdvance(dt);
    }
}

void NPCSystem::spawnNPC(const NPCData& data) {
    // Check if NPC already exists
    if (npcs_.find(data.id) != npcs_.end()) {
        CatEngine::Logger::warning("NPC already exists: " + data.id);
        return;
    }

    // Create entity for NPC
    NPCData npc = data;
    if (!ecs_) {
        CatEngine::Logger::error("ECS not initialized");
        return;
    }

    npc.entity = ecs_->createEntity();

    // Add transform component
    ecs_->emplaceComponent<Engine::Transform>(npc.entity, data.position);

    // Store NPC data
    npcs_[data.id] = npc;

    CatEngine::Logger::info("Spawned NPC: " + data.name + " (" + data.id + ")");
}

void NPCSystem::despawnNPC(const std::string& npcId) {
    auto it = npcs_.find(npcId);
    if (it == npcs_.end()) {
        CatEngine::Logger::warning("NPC not found: " + npcId);
        return;
    }

    // Destroy entity
    if (ecs_ && ecs_->isAlive(it->second.entity)) {
        ecs_->destroyEntity(it->second.entity);
    }

    // Remove from storage
    npcs_.erase(it);

    CatEngine::Logger::info("Despawned NPC: " + npcId);
}

NPCData* NPCSystem::getNPC(const std::string& npcId) {
    auto it = npcs_.find(npcId);
    return (it != npcs_.end()) ? &it->second : nullptr;
}

const NPCData* NPCSystem::getNPC(const std::string& npcId) const {
    auto it = npcs_.find(npcId);
    return (it != npcs_.end()) ? &it->second : nullptr;
}

std::vector<NPCData*> NPCSystem::getNPCsInRange(Engine::vec3 position, float radius) {
    std::vector<NPCData*> result;

    for (auto& pair : npcs_) {
        NPCData& npc = pair.second;
        float distance = getDistanceToNPC(npc, position);

        if (distance <= radius) {
            result.push_back(&npc);
        }
    }

    return result;
}

NPCData* NPCSystem::getClosestInteractableNPC(Engine::vec3 playerPos) {
    NPCData* closest = nullptr;
    float closestDistance = std::numeric_limits<float>::max();

    for (auto& pair : npcs_) {
        NPCData& npc = pair.second;

        if (!npc.isInteractable) {
            continue;
        }

        float distance = getDistanceToNPC(npc, playerPos);

        if (distance <= npc.interactionRadius && distance < closestDistance) {
            closest = &npc;
            closestDistance = distance;
        }
    }

    return closest;
}

bool NPCSystem::canInteract(const std::string& npcId, Engine::vec3 playerPos) const {
    const NPCData* npc = getNPC(npcId);
    if (!npc || !npc->isInteractable) {
        return false;
    }

    float distance = getDistanceToNPC(*npc, playerPos);
    return distance <= npc->interactionRadius;
}

void NPCSystem::startInteraction(const std::string& npcId) {
    NPCData* npc = getNPC(npcId);
    if (!npc) {
        CatEngine::Logger::error("Cannot start interaction: NPC not found: " + npcId);
        return;
    }

    if (inDialog_) {
        CatEngine::Logger::warning("Already in dialog, ending previous dialog");
        endInteraction();
    }

    currentNPCId_ = npcId;
    inDialog_ = true;

    // Start dialog tree if dialog system is available
    if (dialogSystem_ && !npc->dialogTreeId.empty()) {
        dialogSystem_->startDialog(npc->dialogTreeId);
        currentDialogNodeId_ = dialogSystem_->getCurrentNodeId();
    }

    // Trigger callback
    if (onInteractionStart_) {
        onInteractionStart_(npcId);
    }

    CatEngine::Logger::info("Started interaction with: " + npc->name);
}

void NPCSystem::endInteraction() {
    if (!inDialog_) {
        return;
    }

    inDialog_ = false;
    currentNPCId_.clear();
    currentDialogNodeId_.clear();
    autoAdvanceTimer_ = 0.0f;

    // End dialog in dialog system
    if (dialogSystem_) {
        dialogSystem_->endDialog();
    }

    // Close shop if open
    if (shopOpen_) {
        closeShop();
    }

    // Close training if open
    if (trainingOpen_) {
        closeTraining();
    }

    // Trigger callback
    if (onInteractionEnd_) {
        onInteractionEnd_();
    }

    CatEngine::Logger::info("Ended interaction");
}

const DialogNode* NPCSystem::getCurrentDialogNode() const {
    if (!dialogSystem_ || !inDialog_) {
        return nullptr;
    }

    return dialogSystem_->getCurrentNode();
}

void NPCSystem::selectDialogOption(int optionIndex) {
    if (!dialogSystem_ || !inDialog_) {
        CatEngine::Logger::warning("Not in dialog");
        return;
    }

    const DialogNode* currentNode = dialogSystem_->getCurrentNode();
    if (!currentNode) {
        CatEngine::Logger::error("Current dialog node is null");
        return;
    }

    if (optionIndex < 0 || optionIndex >= static_cast<int>(currentNode->options.size())) {
        CatEngine::Logger::error("Invalid dialog option index: " + std::to_string(optionIndex));
        return;
    }

    const NPCDialogOption& option = currentNode->options[optionIndex];

    // Check if option is available
    if (!isOptionAvailable(option)) {
        CatEngine::Logger::warning("Dialog option not available");
        return;
    }

    // Execute option callback
    if (option.onSelect) {
        option.onSelect();
    }

    // Advance to response node
    if (!option.responseId.empty()) {
        dialogSystem_->setCurrentNode(option.responseId);
        currentDialogNodeId_ = option.responseId;
        autoAdvanceTimer_ = 0.0f;

        // Trigger callback
        if (onDialogAdvance_) {
            onDialogAdvance_(currentNPCId_);
        }
    } else {
        // No response, end dialog
        endInteraction();
    }
}

void NPCSystem::advanceDialog() {
    if (!dialogSystem_ || !inDialog_) {
        return;
    }

    const DialogNode* currentNode = dialogSystem_->getCurrentNode();
    if (!currentNode) {
        return;
    }

    if (currentNode->autoAdvance && !currentNode->nextNodeId.empty()) {
        dialogSystem_->setCurrentNode(currentNode->nextNodeId);
        currentDialogNodeId_ = currentNode->nextNodeId;
        autoAdvanceTimer_ = 0.0f;

        // Trigger callback
        if (onDialogAdvance_) {
            onDialogAdvance_(currentNPCId_);
        }
    }
}

void NPCSystem::skipDialog() {
    if (!inDialog_) {
        return;
    }

    endInteraction();
}

void NPCSystem::openShop(const std::string& npcId) {
    NPCData* npc = getNPC(npcId);
    if (!npc) {
        CatEngine::Logger::error("Cannot open shop: NPC not found: " + npcId);
        return;
    }

    if (npc->type != NPCType::Merchant) {
        CatEngine::Logger::warning("NPC is not a merchant: " + npcId);
        return;
    }

    shopOpen_ = true;
    currentShopNPCId_ = npcId;

    CatEngine::Logger::info("Opened shop for: " + npc->name);
}

void NPCSystem::closeShop() {
    shopOpen_ = false;
    currentShopNPCId_.clear();

    CatEngine::Logger::info("Closed shop");
}

const std::vector<ShopItem>& NPCSystem::getShopInventory() const {
    static std::vector<ShopItem> empty;

    if (!shopOpen_) {
        return empty;
    }

    const NPCData* npc = getNPC(currentShopNPCId_);
    if (!npc) {
        return empty;
    }

    return npc->shopInventory;
}

bool NPCSystem::purchaseItem(const std::string& itemId) {
    if (!shopOpen_) {
        CatEngine::Logger::warning("Shop is not open");
        return false;
    }

    NPCData* npc = getNPC(currentShopNPCId_);
    if (!npc) {
        return false;
    }

    // Find item in inventory
    for (auto& item : npc->shopInventory) {
        if (item.itemId == itemId) {
            if (!item.isAvailable) {
                CatEngine::Logger::warning("Item not available: " + itemId);
                return false;
            }

            // Check clan requirement
            if (item.requiredClan.has_value() && item.requiredClan.value() != playerClan_) {
                CatEngine::Logger::warning("Clan requirement not met for item: " + itemId);
                return false;
            }

            // Check level requirement
            if (item.requiredLevel.has_value() && playerLevel_ < item.requiredLevel.value()) {
                CatEngine::Logger::warning("Level requirement not met for item: " + itemId);
                return false;
            }

            // Decrease stock if not unlimited
            if (item.stock > 0) {
                item.stock--;
                if (item.stock == 0) {
                    item.isAvailable = false;
                }
            }

            CatEngine::Logger::info("Purchased item: " + item.name);
            return true;
        }
    }

    CatEngine::Logger::error("Item not found in shop: " + itemId);
    return false;
}

void NPCSystem::openTraining(const std::string& npcId) {
    NPCData* npc = getNPC(npcId);
    if (!npc) {
        CatEngine::Logger::error("Cannot open training: NPC not found: " + npcId);
        return;
    }

    if (npc->type != NPCType::Trainer && npc->type != NPCType::Mentor) {
        CatEngine::Logger::warning("NPC is not a trainer: " + npcId);
        return;
    }

    trainingOpen_ = true;
    currentTrainerNPCId_ = npcId;

    CatEngine::Logger::info("Opened training for: " + npc->name);
}

void NPCSystem::closeTraining() {
    trainingOpen_ = false;
    currentTrainerNPCId_.clear();

    CatEngine::Logger::info("Closed training");
}

const std::vector<TrainingOption>& NPCSystem::getTrainingOptions() const {
    static std::vector<TrainingOption> empty;

    if (!trainingOpen_) {
        return empty;
    }

    const NPCData* npc = getNPC(currentTrainerNPCId_);
    if (!npc) {
        return empty;
    }

    return npc->trainingOptions;
}

bool NPCSystem::trainAbility(const std::string& abilityId) {
    if (!trainingOpen_) {
        CatEngine::Logger::warning("Training is not open");
        return false;
    }

    NPCData* npc = getNPC(currentTrainerNPCId_);
    if (!npc) {
        return false;
    }

    // Find ability in training options
    for (const auto& option : npc->trainingOptions) {
        if (option.abilityId == abilityId) {
            if (!option.isAvailable) {
                CatEngine::Logger::warning("Ability not available: " + abilityId);
                return false;
            }

            // Check clan requirement
            if (option.requiredClan.has_value() && option.requiredClan.value() != playerClan_) {
                CatEngine::Logger::warning("Clan requirement not met for ability: " + abilityId);
                return false;
            }

            // Check level requirement
            if (option.requiredLevel.has_value() && playerLevel_ < option.requiredLevel.value()) {
                CatEngine::Logger::warning("Level requirement not met for ability: " + abilityId);
                return false;
            }

            CatEngine::Logger::info("Trained ability: " + option.name);
            return true;
        }
    }

    CatEngine::Logger::error("Ability not found in training options: " + abilityId);
    return false;
}

std::vector<std::string> NPCSystem::getAvailableQuests(const std::string& npcId) const {
    const NPCData* npc = getNPC(npcId);
    if (!npc) {
        return {};
    }

    return npc->questsToGive;
}

bool NPCSystem::loadNPCsFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        CatEngine::Logger::error("Failed to open NPC file: " + filepath);
        return false;
    }

    try {
        json data = json::parse(file);

        if (!data.contains("npcs") || !data["npcs"].is_array()) {
            CatEngine::Logger::error("Invalid NPC file format: missing 'npcs' array");
            return false;
        }

        for (const auto& npcJson : data["npcs"]) {
            NPCData npc;

            npc.id = npcJson.value("id", "");
            npc.name = npcJson.value("name", "");
            npc.type = stringToNPCType(npcJson.value("type", "Villager"));

            if (npcJson.contains("clan")) {
                npc.clan = stringToClan(npcJson["clan"].get<std::string>());
            }

            if (npcJson.contains("position") && npcJson["position"].is_array()) {
                auto pos = npcJson["position"];
                npc.position = Engine::vec3(
                    pos[0].get<float>(),
                    pos[1].get<float>(),
                    pos[2].get<float>()
                );
            }

            npc.interactionRadius = npcJson.value("interactionRadius", 3.0f);
            npc.modelPath = npcJson.value("modelPath", "");
            npc.dialogTreeId = npcJson.value("dialogTreeId", "");

            if (npcJson.contains("quests") && npcJson["quests"].is_array()) {
                for (const auto& quest : npcJson["quests"]) {
                    npc.questsToGive.push_back(quest.get<std::string>());
                }
            }

            if (npcJson.contains("shopInventory") && npcJson["shopInventory"].is_array()) {
                for (const auto& itemJson : npcJson["shopInventory"]) {
                    ShopItem item;
                    item.itemId = itemJson.value("itemId", "");
                    item.name = itemJson.value("name", "");
                    item.description = itemJson.value("description", "");
                    item.price = itemJson.value("price", 0);
                    item.stock = itemJson.value("stock", -1);

                    if (itemJson.contains("requiredClan")) {
                        item.requiredClan = stringToClan(itemJson["requiredClan"].get<std::string>());
                    }

                    if (itemJson.contains("requiredLevel")) {
                        item.requiredLevel = itemJson["requiredLevel"].get<int>();
                    }

                    npc.shopInventory.push_back(item);
                }
            }

            if (npcJson.contains("trainingOptions") && npcJson["trainingOptions"].is_array()) {
                for (const auto& trainingJson : npcJson["trainingOptions"]) {
                    TrainingOption option;
                    option.abilityId = trainingJson.value("abilityId", "");
                    option.name = trainingJson.value("name", "");
                    option.description = trainingJson.value("description", "");
                    option.cost = trainingJson.value("cost", 0);

                    if (trainingJson.contains("requiredClan")) {
                        option.requiredClan = stringToClan(trainingJson["requiredClan"].get<std::string>());
                    }

                    if (trainingJson.contains("requiredLevel")) {
                        option.requiredLevel = trainingJson["requiredLevel"].get<int>();
                    }

                    npc.trainingOptions.push_back(option);
                }
            }

            spawnNPC(npc);
        }

        CatEngine::Logger::info("Loaded " + std::to_string(npcs_.size()) + " NPCs from " + filepath);
        return true;

    } catch (const json::exception& e) {
        CatEngine::Logger::error("Failed to parse NPC file: " + std::string(e.what()));
        return false;
    }
}

void NPCSystem::updateAutoAdvance(float dt) {
    if (!dialogSystem_) {
        return;
    }

    const DialogNode* currentNode = dialogSystem_->getCurrentNode();
    if (!currentNode || !currentNode->autoAdvance) {
        return;
    }

    autoAdvanceTimer_ += dt;

    if (autoAdvanceTimer_ >= currentNode->autoAdvanceDelay) {
        advanceDialog();
    }
}

bool NPCSystem::isOptionAvailable(const NPCDialogOption& option) const {
    if (!option.isAvailable) {
        return false;
    }

    // Check clan requirement
    if (option.requiredClan.has_value() && option.requiredClan.value() != playerClan_) {
        return false;
    }

    // Check level requirement
    if (option.requiredLevel.has_value() && playerLevel_ < option.requiredLevel.value()) {
        return false;
    }

    // Check quest requirement using callback
    if (option.requiredQuest.has_value() && questCheckCallback_) {
        if (!questCheckCallback_(option.requiredQuest.value())) {
            return false;
        }
    }

    return true;
}

float NPCSystem::getDistanceToNPC(const NPCData& npc, Engine::vec3 position) const {
    if (!ecs_ || !ecs_->isAlive(npc.entity)) {
        return std::numeric_limits<float>::max();
    }

    auto* transform = ecs_->getComponent<Engine::Transform>(npc.entity);
    if (!transform) {
        return std::numeric_limits<float>::max();
    }

    Engine::vec3 diff = transform->position - position;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
}

// Utility functions

std::string clanToString(Clan clan) {
    switch (clan) {
        case Clan::None: return "None";
        case Clan::MistClan: return "MistClan";
        case Clan::StormClan: return "StormClan";
        case Clan::EmberClan: return "EmberClan";
        case Clan::FrostClan: return "FrostClan";
        default: return "Unknown";
    }
}

Clan stringToClan(const std::string& str) {
    if (str == "MistClan") return Clan::MistClan;
    if (str == "StormClan") return Clan::StormClan;
    if (str == "EmberClan") return Clan::EmberClan;
    if (str == "FrostClan") return Clan::FrostClan;
    return Clan::None;
}

std::string npcTypeToString(NPCType type) {
    switch (type) {
        case NPCType::Mentor: return "Mentor";
        case NPCType::QuestGiver: return "QuestGiver";
        case NPCType::Merchant: return "Merchant";
        case NPCType::ClanLeader: return "ClanLeader";
        case NPCType::Healer: return "Healer";
        case NPCType::Trainer: return "Trainer";
        case NPCType::Villager: return "Villager";
        default: return "Unknown";
    }
}

NPCType stringToNPCType(const std::string& str) {
    if (str == "Mentor") return NPCType::Mentor;
    if (str == "QuestGiver") return NPCType::QuestGiver;
    if (str == "Merchant") return NPCType::Merchant;
    if (str == "ClanLeader") return NPCType::ClanLeader;
    if (str == "Healer") return NPCType::Healer;
    if (str == "Trainer") return NPCType::Trainer;
    return NPCType::Villager;
}

} // namespace CatGame

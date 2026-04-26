#include "NPCSystem.hpp"
#include "DialogSystem.hpp"
#include "MerchantSystem.hpp"
#include "../entities/CatEntity.hpp"
#include "../components/MeshComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>

using json = nlohmann::json;

namespace {

// Resolve a Meshy NPC model path from the JSON form ("models/cats/foo.glb")
// to a runtime path the AssetManager can open.
//
// Two on-disk parallel sets exist (see commentary in CatEntity.cpp):
//   * assets/models/cats/<name>.glb         — raw Meshy export, single mesh,
//                                             no skeleton / no clips.
//   * assets/models/cats/rigged/<name>.glb  — auto-rigged variant carrying a
//                                             real bone hierarchy and (when
//                                             the rigger emitted them)
//                                             animation clips.
//
// We bias to the rigged variant so once skinning lands the same NPC entities
// animate without re-spawning. The rigged path resolves the same Model* via
// the AssetManager cache as the player when both reference the same rig
// (ember_leader.glb is a concrete example — player AND ember_blazestar both
// load `assets/models/cats/rigged/ember_leader.glb`, so the GPU mesh cache
// in ScenePass deduplicates the upload to a single 6.5 MB resident copy
// servicing both entities).
//
// Fallbacks (in order):
//   1. assets/models/cats/rigged/<basename>          — ideal, has skeleton.
//   2. assets/<jsonPath>                             — raw mesh, no skeleton.
//   3. <jsonPath>                                    — caller already gave us
//                                                      a fully-resolved path.
// Returns empty string when nothing on disk matches; caller skips mesh load
// and keeps the NPC as a transform-only entity (still interactable, just
// invisible) rather than aborting the world load.
std::string ResolveNpcModelPath(const std::string& jsonPath) {
    if (jsonPath.empty()) {
        return std::string();
    }

    namespace fs = std::filesystem;

    // Compute the rigged candidate by inserting "rigged/" before the
    // filename component of "assets/<jsonPath>". We do this rather than a
    // blanket string replace so paths that don't conform to "models/cats/"
    // still produce a sensible candidate (e.g. a future "models/dogs/foo.glb"
    // would resolve to "assets/models/dogs/rigged/foo.glb" via the same
    // logic without needing a category-specific branch here).
    std::string assetsPrefixed = "assets/" + jsonPath;
    std::string riggedCandidate;
    {
        const size_t lastSlash = assetsPrefixed.find_last_of('/');
        if (lastSlash != std::string::npos) {
            riggedCandidate = assetsPrefixed.substr(0, lastSlash) +
                              "/rigged" +
                              assetsPrefixed.substr(lastSlash);
        } else {
            // Path has no separator — treat the whole thing as a filename
            // sitting under assets/ and prepend the rigged dir directly.
            riggedCandidate = "assets/rigged/" + assetsPrefixed.substr(7);
        }
    }

    std::error_code ec;
    if (fs::exists(riggedCandidate, ec) && !ec) {
        return riggedCandidate;
    }
    if (fs::exists(assetsPrefixed, ec) && !ec) {
        return assetsPrefixed;
    }
    if (fs::exists(jsonPath, ec) && !ec) {
        return jsonPath;
    }

    return std::string();
}

// Map a (clan, npcType) pair to a flat-shaded tint that visually distinguishes
// the four clans + neutral NPCs while the renderer still flat-shades every
// entity (no PBR sampler binding yet). The Meshy-authored cat GLBs ship with a
// baseColorTexture but NO baseColorFactor, so by default every cat collapses
// to glm::vec4(1) → white, and a viewer cannot tell ember_blazestar apart from
// frost_winterstar without reading the spawn log. These tints lift that
// failure mode without dragging in textured-PBR descriptor sets — a 6-line
// switch instead of a multi-iteration renderer rewrite.
//
// Color rationale (against the engine's green-grass terrain):
//   * Mist  = cool grey-blue, evokes fog-shrouded undergrowth.
//   * Storm = pale electric yellow, evokes lightning glow.
//   * Ember = saturated warm orange-red, evokes flame fur.
//   * Frost = pale ice-blue, evokes snow-tipped fur.
//   * None  = warm tan, the "neutral wanderer" nutmeg color used by the
//             elder / wanderer / scout to distinguish them from the four
//             clan castes without giving them their own clan tint.
//
// ClanLeader gets a small +12% multiplicative brightness so the leader of
// each clan reads as the strongest silhouette in their cluster — a tiny but
// real readability gain on the world map (Mistheart, Stormstar, Blazestar,
// Winterstar are the cats the player needs to identify first).
Engine::vec3 ComputeNpcTint(CatGame::Clan clan, CatGame::NPCType npcType) {
    Engine::vec3 base;
    switch (clan) {
        case CatGame::Clan::MistClan:
            base = Engine::vec3(0.55F, 0.65F, 0.78F);
            break;
        case CatGame::Clan::StormClan:
            base = Engine::vec3(0.90F, 0.85F, 0.55F);
            break;
        case CatGame::Clan::EmberClan:
            base = Engine::vec3(0.95F, 0.45F, 0.20F);
            break;
        case CatGame::Clan::FrostClan:
            base = Engine::vec3(0.78F, 0.90F, 1.00F);
            break;
        case CatGame::Clan::None:
        default:
            base = Engine::vec3(0.78F, 0.65F, 0.45F);
            break;
    }

    // Type-driven brightness modulator. Multiplicative so we stay inside the
    // [0, 1] surface-color range even after the clamp below; clan identity
    // dominates and type is a secondary cue (a Mist mentor is clearly mist-
    // blue first, slightly-darker mist-blue second). Boundaries clamp the
    // modulated tint into [0, 1] so a future bright-clan addition can't
    // overflow into the > 1.0 emissive territory the entity shader doesn't
    // expect.
    float brightnessMul = 1.0F;
    switch (npcType) {
        case CatGame::NPCType::ClanLeader:
            brightnessMul = 1.12F;  // Slightly brighter to read as the
                                    // dominant silhouette in their cluster.
            break;
        case CatGame::NPCType::Healer:
            brightnessMul = 1.06F;  // Subtle elevation; healers are rare
                                    // and worth being a touch readable.
            break;
        case CatGame::NPCType::Mentor:
            brightnessMul = 0.92F;  // Gravitas — slightly darker than
                                    // their clan baseline.
            break;
        case CatGame::NPCType::Merchant:
        case CatGame::NPCType::Trainer:
        case CatGame::NPCType::QuestGiver:
        case CatGame::NPCType::Villager:
        default:
            brightnessMul = 1.00F;
            break;
    }

    Engine::vec3 result(base.x * brightnessMul,
                        base.y * brightnessMul,
                        base.z * brightnessMul);
    // Clamp into the entity-shader's expected [0,1] surface-color range; with
    // brightnessMul=1.12 a base channel of 1.00 would otherwise bleed to
    // 1.12, which the forward fragment doesn't tone-map and would clip
    // visibly on Frost / Storm cats.
    result.x = std::min(1.0F, std::max(0.0F, result.x));
    result.y = std::min(1.0F, std::max(0.0F, result.y));
    result.z = std::min(1.0F, std::max(0.0F, result.z));
    return result;
}

} // namespace

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

    // Snap the NPC's Y coordinate to the terrain heightfield when a
    // sampler is wired. The JSON catalogue authors every NPC at y = 0.5
    // — a flat-editor convention — but the runtime Terrain reaches
    // ~50 m at peaks and 5–15 m even in valleys, so an unsnapped NPC is
    // buried beneath the heightfield at almost every authored x,z. The
    // small +0.05 m epsilon keeps the cat's feet above the surface to
    // avoid Z-fighting between the foot polygons and the terrain mesh.
    // When the sampler is unset (tests, headless, GameWorld-less paths)
    // we fall through to the JSON-authored y so the existing behaviour
    // is preserved bit-for-bit.
    Engine::vec3 spawnPosition = data.position;
    if (heightSampler_) {
        spawnPosition.y = heightSampler_(spawnPosition.x, spawnPosition.z) + 0.05F;
        npc.position.y = spawnPosition.y;
    }

    // Add transform component
    ecs_->emplaceComponent<Engine::Transform>(npc.entity, spawnPosition);

    // Attach the Meshy GLB referenced by the NPC's modelPath so the world map
    // visibly populates with 16 distinct cats (mist/storm/ember/frost mentors,
    // leaders, merchants, trainers, healers, plus neutral wanderer / elder /
    // scout) instead of 16 invisible transforms. Path resolution prefers the
    // rigged variant under assets/models/cats/rigged/ — that way once skinning
    // lands these same NPC entities animate from the existing animator wired
    // by configureAnimations() below, no respawn pass needed.
    //
    // Failure modes are non-fatal:
    //   * Empty modelPath in JSON → skip silently (some scaffolded NPCs
    //     intentionally have no mesh yet).
    //   * Resolver finds no matching file → log a warning and leave the NPC
    //     as transform-only. The interaction radius / dialog / shop pipeline
    //     still works; the NPC just won't render.
    //   * loadModel throws (corrupt GLB, AssetManager not initialized) → it
    //     catches the exception internally and returns false; we proceed
    //     without a mesh.
    if (!data.modelPath.empty()) {
        const std::string resolved = ResolveNpcModelPath(data.modelPath);
        if (resolved.empty()) {
            CatEngine::Logger::warning(
                "NPCSystem: model not found for " + data.id +
                " (json path '" + data.modelPath + "'), spawning invisible");
        } else if (CatEntity::loadModel(*ecs_, npc.entity, resolved.c_str())) {
            // configureAnimations is intentionally permissive — even if the
            // rigged GLB has zero clips (the rig_batch tooling sometimes
            // emits skeleton-only files), it still attaches a valid Animator
            // sitting at bind pose so future code can call play() / setFloat
            // without a null check on every NPC.
            CatEntity::configureAnimations(*ecs_, npc.entity);

            // Stamp the per-clan + per-type tint onto the freshly-attached
            // mesh so MeshSubmissionSystem reads it on the very first frame
            // (no per-frame clan lookup, no flag plumbing through the ECS
            // hot path). Without this every cat reads as flat white
            // regardless of clan because Meshy GLBs ship a baseColorTexture
            // we don't sample yet — see ComputeNpcTint above for the full
            // rationale and the chosen color palette.
            //
            // The mesh component attached above (CatEntity::loadModel ->
            // ecs.addComponent(MeshComponent)) is grabbed by ref and mutated
            // in-place. If a future NPC asset variant ever loads but doesn't
            // produce a MeshComponent, the null check keeps us safe — the
            // NPC just renders untinted (flat white) as before, which is
            // strictly no worse than the pre-override behavior.
            MeshComponent* meshAttached = ecs_->getComponent<MeshComponent>(npc.entity);
            if (meshAttached != nullptr) {
                // npc.clan is std::optional<Clan> — JSON entries can omit it
                // (the neutral wanderer / elder / scout do, on purpose). When
                // unset, fall through to Clan::None so ComputeNpcTint emits
                // the warm-tan neutral colour rather than a debug-magenta or
                // crash on optional::value() against an empty optional.
                const Clan effectiveClan = npc.clan.has_value()
                    ? npc.clan.value()
                    : Clan::None;
                meshAttached->hasTintOverride = true;
                meshAttached->tintOverride = ComputeNpcTint(effectiveClan, npc.type);
            }
        } else {
            CatEngine::Logger::warning(
                "NPCSystem: loadModel failed for " + data.id +
                " (resolved '" + resolved + "'), spawning invisible");
        }
    }

    // Store NPC data
    npcs_[data.id] = npc;

    CatEngine::Logger::info("Spawned NPC: " + data.name + " (" + data.id + ")");
}

void NPCSystem::clearAll() {
    // The ECS entities themselves have already been removed by the caller
    // (CatAnnihilation::restart calls ecs_.clearEntities() before invoking
    // this) — we only need to drop the NPCSystem-side bookkeeping that
    // would otherwise collide with the next loadNPCsFromFile pass.
    //
    // Why not iterate npcs_ and call despawnNPC on each:
    //   - despawnNPC tries to destroy the Entity via the ECS, which now
    //     either no-ops on a dead generation (best case) or asserts in a
    //     hardening build (worst case). The entity is gone — there is
    //     nothing left to destroy.
    //   - despawnNPC also logs once per NPC; clearing 16 NPCs spams 16
    //     stale lines into the playtest log every restart with no signal.
    //
    // Resetting interaction state (dialog/shop/training) is required: a
    // `restart()` from inside a dialog node would otherwise leave the
    // dialog system pointing at a now-dead NPC entity and pop its UI on
    // the next frame.
    npcs_.clear();
    inDialog_ = false;
    currentNPCId_.clear();
    currentDialogNodeId_.clear();
    autoAdvanceTimer_ = 0.0f;
    shopOpen_ = false;
    currentShopNPCId_.clear();
    trainingOpen_ = false;
    currentTrainerNPCId_.clear();

    if (dialogSystem_) {
        dialogSystem_->endDialog();
    }
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

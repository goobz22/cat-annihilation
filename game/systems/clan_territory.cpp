#include "clan_territory.hpp"
#include "story_mode.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace CatGame {

// ============================================================================
// TerritoryBoundary Implementation
// ============================================================================

bool TerritoryBoundary::contains(const Engine::vec3& point) const {
    if (!usePolygon) {
        // Simple circular boundary check
        Engine::vec3 diff = point - center;
        return diff.lengthSquared() <= (radius * radius);
    } else {
        // Point-in-polygon test (2D, ignoring Y)
        if (polygon.size() < 3) return false;

        int intersections = 0;
        for (size_t i = 0; i < polygon.size(); ++i) {
            const Engine::vec3& v1 = polygon[i];
            const Engine::vec3& v2 = polygon[(i + 1) % polygon.size()];

            // Ray casting algorithm
            if ((v1.z > point.z) != (v2.z > point.z)) {
                float slope = (point.z - v1.z) / (v2.z - v1.z);
                if (point.x < v1.x + slope * (v2.x - v1.x)) {
                    intersections++;
                }
            }
        }

        return (intersections % 2) == 1;
    }
}

float TerritoryBoundary::distanceToBoundary(const Engine::vec3& point) const {
    if (!usePolygon) {
        Engine::vec3 diff = point - center;
        float distToCenter = diff.length();
        return std::abs(distToCenter - radius);
    } else {
        // Find minimum distance to any polygon edge
        float minDist = FLT_MAX;

        for (size_t i = 0; i < polygon.size(); ++i) {
            const Engine::vec3& v1 = polygon[i];
            const Engine::vec3& v2 = polygon[(i + 1) % polygon.size()];

            Engine::vec3 edge = v2 - v1;
            Engine::vec3 toPoint = point - v1;

            float edgeLengthSq = edge.lengthSquared();
            if (edgeLengthSq < 0.0001f) continue;

            float t = std::clamp(toPoint.dot(edge) / edgeLengthSq, 0.0f, 1.0f);
            Engine::vec3 projection = v1 + edge * t;
            float dist = (point - projection).length();

            minDist = std::min(minDist, dist);
        }

        return minDist;
    }
}

Engine::vec3 TerritoryBoundary::closestPointOnBoundary(const Engine::vec3& point) const {
    if (!usePolygon) {
        Engine::vec3 diff = point - center;
        float dist = diff.length();
        if (dist < 0.0001f) {
            return center + Engine::vec3(radius, 0.0f, 0.0f);
        }
        return center + diff.normalized() * radius;
    } else {
        Engine::vec3 closest = polygon[0];
        float minDist = FLT_MAX;

        for (size_t i = 0; i < polygon.size(); ++i) {
            const Engine::vec3& v1 = polygon[i];
            const Engine::vec3& v2 = polygon[(i + 1) % polygon.size()];

            Engine::vec3 edge = v2 - v1;
            Engine::vec3 toPoint = point - v1;

            float edgeLengthSq = edge.lengthSquared();
            if (edgeLengthSq < 0.0001f) continue;

            float t = std::clamp(toPoint.dot(edge) / edgeLengthSq, 0.0f, 1.0f);
            Engine::vec3 projection = v1 + edge * t;
            float dist = (point - projection).lengthSquared();

            if (dist < minDist) {
                minDist = dist;
                closest = projection;
            }
        }

        return closest;
    }
}

// ============================================================================
// TerritoryResources Implementation
// ============================================================================

void TerritoryResources::regenerate(float deltaTime) {
    // Slowly regenerate resources over time
    const float REGEN_RATE = 2.0f; // points per second

    foodAbundance = std::min(100, foodAbundance + static_cast<int>(REGEN_RATE * deltaTime));
    herbs = std::min(100, herbs + static_cast<int>(REGEN_RATE * deltaTime));
    minerals = std::min(100, minerals + static_cast<int>(REGEN_RATE * deltaTime));
    mysticalEnergy = std::min(100.0f, mysticalEnergy + REGEN_RATE * deltaTime * 0.5f);
}

bool TerritoryResources::harvest(int& food, int& herb, int& mineral, float harvestPower) {
    int harvestAmount = static_cast<int>(harvestPower * 10.0f);

    bool success = false;

    if (foodAbundance >= harvestAmount) {
        food += harvestAmount;
        foodAbundance -= harvestAmount;
        success = true;
    }

    if (herbs >= harvestAmount) {
        herb += harvestAmount;
        herbs -= harvestAmount;
        success = true;
    }

    if (minerals >= harvestAmount) {
        mineral += harvestAmount;
        minerals -= harvestAmount;
        success = true;
    }

    return success;
}

// ============================================================================
// Territory Implementation
// ============================================================================

bool Territory::canAccess(Clan clan, int playerRank) const {
    // Check if clan is explicitly allowed
    if (allowedClans.find(clan) != allowedClans.end()) {
        return playerRank >= requiredRank;
    }

    // Controlling clan always has access
    if (clan == controllingClan) {
        return playerRank >= requiredRank;
    }

    // Neutral territories are generally accessible
    if (controlStatus == ControlStatus::Unclaimed) {
        return true;
    }

    return false;
}

float Territory::getTerritoryBonus(Clan clan) const {
    // Home territory bonus
    if (clan == controllingClan) {
        return 1.25f; // 25% bonus on home turf
    }

    // Enemy territory penalty
    if (controlStatus == ControlStatus::Controlled && clan != controllingClan) {
        return 0.85f; // 15% penalty on enemy turf
    }

    return 1.0f; // No bonus/penalty
}

void Territory::updateCapture(float deltaTime) {
    if (controlStatus != ControlStatus::Contested) {
        return;
    }

    // Calculate capture delta based on attacker/defender ratio
    if (attackersPresent > 0) {
        float captureMultiplier = 1.0f + (attackersPresent - defendersPresent) * 0.2f;
        captureMultiplier = std::max(0.0f, captureMultiplier);

        captureProgress += captureRate * captureMultiplier * deltaTime;

        // Complete capture at 100%
        if (captureProgress >= 100.0f) {
            captureProgress = 100.0f;
        }
    } else if (defendersPresent > attackersPresent) {
        // Defenders are reclaiming
        captureProgress -= captureRate * deltaTime * 2.0f; // Faster reclaim

        if (captureProgress <= 0.0f) {
            captureProgress = 0.0f;
            controlStatus = ControlStatus::Controlled;
        }
    }
}

void Territory::applyTerritoryEffects(
    float& damage,
    float& defense,
    float& speed,
    Clan playerClan
) const {
    float bonus = getTerritoryBonus(playerClan);

    damage *= damageMultiplier * bonus;
    defense *= defenseMultiplier * bonus;
    speed *= speedMultiplier;
}

// ============================================================================
// ClanTerritorySystem Implementation
// ============================================================================

ClanTerritorySystem::ClanTerritorySystem() {
}

void ClanTerritorySystem::initialize() {
    territories_.clear();
    territoryNameMap_.clear();

    createMistClanTerritories();
    createStormClanTerritories();
    createEmberClanTerritories();
    createFrostClanTerritories();
    createNeutralTerritories();
    createSacredTerritories();
}

void ClanTerritorySystem::update(float deltaTime) {
    for (auto& territory : territories_) {
        // Update capture progress
        territory.updateCapture(deltaTime);

        // Regenerate resources
        territory.resources.regenerate(deltaTime);

        // Check if capture completed
        if (territory.controlStatus == ControlStatus::Contested &&
            territory.captureProgress >= 100.0f) {
            completeCapture(territory.name);
        }
    }
}

Territory* ClanTerritorySystem::getTerritoryAt(const Engine::vec3& position) {
    for (auto& territory : territories_) {
        if (territory.boundary.contains(position)) {
            return &territory;
        }
    }
    return nullptr;
}

const Territory* ClanTerritorySystem::getTerritoryAt(const Engine::vec3& position) const {
    for (const auto& territory : territories_) {
        if (territory.boundary.contains(position)) {
            return &territory;
        }
    }
    return nullptr;
}

Territory* ClanTerritorySystem::getTerritory(const std::string& name) {
    auto it = territoryNameMap_.find(name);
    if (it != territoryNameMap_.end()) {
        return &territories_[it->second];
    }
    return nullptr;
}

const Territory* ClanTerritorySystem::getTerritory(const std::string& name) const {
    auto it = territoryNameMap_.find(name);
    if (it != territoryNameMap_.end()) {
        return &territories_[it->second];
    }
    return nullptr;
}

std::vector<Territory*> ClanTerritorySystem::getTerritoriesByLan(Clan clan) {
    std::vector<Territory*> result;
    for (auto& territory : territories_) {
        if (territory.controllingClan == clan) {
            result.push_back(&territory);
        }
    }
    return result;
}

bool ClanTerritorySystem::canEnterTerritory(
    const Territory* territory,
    Clan playerClan,
    int playerRank
) const {
    if (!territory) return true;
    return territory->canAccess(playerClan, playerRank);
}

TerritoryWarning ClanTerritorySystem::checkTerritoryWarning(
    const Engine::vec3& position,
    Clan playerClan,
    int playerRank
) const {
    TerritoryWarning warning;
    warning.territory = nullptr;
    warning.distanceToBoundary = FLT_MAX;
    warning.canEnter = true;

    // Find closest restricted territory
    for (const auto& territory : territories_) {
        float dist = territory.boundary.distanceToBoundary(position);

        // Check if approaching a restricted area
        if (dist < 50.0f && !territory.canAccess(playerClan, playerRank)) {
            if (dist < warning.distanceToBoundary) {
                warning.distanceToBoundary = dist;
                warning.territory = const_cast<Territory*>(&territory);
                warning.canEnter = false;
                warning.message = "Warning: Approaching " + territory.name +
                                " - Access Restricted";
            }
        }
    }

    return warning;
}

void ClanTerritorySystem::unlockTerritory(const std::string& territoryName, Clan clan) {
    Territory* territory = getTerritory(territoryName);
    if (territory) {
        territory->allowedClans.insert(clan);
    }
}

bool ClanTerritorySystem::startCapture(const std::string& territoryName, Clan attackingClan) {
    Territory* territory = getTerritory(territoryName);
    if (!territory) return false;

    if (territory->controlStatus == ControlStatus::Controlled) {
        territory->controlStatus = ControlStatus::Contested;
        territory->capturingClan = attackingClan;
        territory->captureProgress = 0.0f;
        return true;
    }

    return false;
}

void ClanTerritorySystem::completeCapture(const std::string& territoryName) {
    Territory* territory = getTerritory(territoryName);
    if (!territory) return;

    Clan oldClan = territory->controllingClan;
    Clan newClan = territory->capturingClan;

    territory->controllingClan = newClan;
    territory->controlStatus = ControlStatus::Controlled;
    territory->captureProgress = 0.0f;
    territory->attackersPresent = 0;
    territory->defendersPresent = 0;

    // Trigger callback
    if (territoryCapturedCallback_) {
        territoryCapturedCallback_(territory, oldClan, newClan);
    }
}

void ClanTerritorySystem::resetCapture(const std::string& territoryName) {
    Territory* territory = getTerritory(territoryName);
    if (!territory) return;

    territory->controlStatus = ControlStatus::Controlled;
    territory->captureProgress = 0.0f;
    territory->attackersPresent = 0;
    territory->defendersPresent = 0;
}

void ClanTerritorySystem::updatePlayerPresence(
    const Engine::vec3& position,
    Clan playerClan,
    bool isDefending
) {
    Territory* currentTerritory = getTerritoryAt(position);

    // Check if player entered/left a territory
    if (currentTerritory != lastTerritory_) {
        if (lastTerritory_ && territoryLeftCallback_) {
            territoryLeftCallback_(lastTerritory_, playerClan);
        }

        if (currentTerritory && territoryEnteredCallback_) {
            territoryEnteredCallback_(currentTerritory, playerClan);
        }

        lastTerritory_ = currentTerritory;
    }

    // Update presence counters for capture mechanic
    if (currentTerritory && currentTerritory->controlStatus == ControlStatus::Contested) {
        if (isDefending) {
            currentTerritory->defendersPresent++;
        } else {
            currentTerritory->attackersPresent++;
        }
    }
}

std::string ClanTerritorySystem::getTerritoryBonusDescription(
    const Territory* territory,
    Clan clan
) const {
    if (!territory) return "No bonuses";

    std::stringstream ss;
    float bonus = territory->getTerritoryBonus(clan);

    if (bonus > 1.0f) {
        ss << "Home Territory: +" << static_cast<int>((bonus - 1.0f) * 100.0f) << "% to all stats";
    } else if (bonus < 1.0f) {
        ss << "Enemy Territory: " << static_cast<int>((1.0f - bonus) * 100.0f) << "% penalty";
    } else {
        ss << "Neutral Territory";
    }

    return ss.str();
}

void ClanTerritorySystem::onTerritoryEntered(void (*callback)(const Territory*, Clan)) {
    territoryEnteredCallback_ = callback;
}

void ClanTerritorySystem::onTerritoryLeft(void (*callback)(const Territory*, Clan)) {
    territoryLeftCallback_ = callback;
}

void ClanTerritorySystem::onTerritoryCaptured(
    void (*callback)(const Territory*, Clan, Clan)
) {
    territoryCapturedCallback_ = callback;
}

void ClanTerritorySystem::addTerritory(Territory&& territory) {
    size_t index = territories_.size();
    territoryNameMap_[territory.name] = index;
    calculateTerritoryBonuses(territory);
    territories_.push_back(std::move(territory));
}

void ClanTerritorySystem::calculateTerritoryBonuses(Territory& territory) {
    // Apply type-specific bonuses
    switch (territory.type) {
        case TerritoryType::Forest:
            territory.stealthBonus = 0.3f;
            territory.speedMultiplier = 0.9f;
            break;

        case TerritoryType::Mountains:
            territory.speedMultiplier = 1.2f;
            territory.damageMultiplier = 1.1f;
            break;

        case TerritoryType::Volcanic:
            territory.damageMultiplier = 1.3f;
            territory.defenseMultiplier = 0.9f;
            break;

        case TerritoryType::Tundra:
            territory.defenseMultiplier = 1.3f;
            territory.speedMultiplier = 0.85f;
            break;

        case TerritoryType::Sacred:
            territory.damageMultiplier = 1.2f;
            territory.defenseMultiplier = 1.2f;
            territory.resources.mysticalEnergy = 100.0f;
            break;

        default:
            break;
    }
}

// ============================================================================
// Territory Creation Methods
// ============================================================================

void ClanTerritorySystem::createMistClanTerritories() {
    // Main MistClan Territory
    Territory mistlands;
    mistlands.name = "Mistwood Forest";
    mistlands.type = TerritoryType::Forest;
    mistlands.zoneType = ZoneType::SafeZone;
    mistlands.controlStatus = ControlStatus::Controlled;
    mistlands.controllingClan = Clan::MistClan;
    mistlands.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(-200.0f, 0.0f, -200.0f), 150.0f
    );
    mistlands.allowedClans.insert(Clan::MistClan);
    addTerritory(std::move(mistlands));

    // MistClan Training Grounds
    Territory training;
    training.name = "Shadow's Edge Training Ground";
    training.type = TerritoryType::Forest;
    training.zoneType = ZoneType::TrainingZone;
    training.controlStatus = ControlStatus::Controlled;
    training.controllingClan = Clan::MistClan;
    training.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(-250.0f, 0.0f, -150.0f), 50.0f
    );
    training.requiredRank = 1; // Apprentice or higher
    training.allowedClans.insert(Clan::MistClan);
    addTerritory(std::move(training));
}

void ClanTerritorySystem::createStormClanTerritories() {
    // Main StormClan Territory
    Territory peaks;
    peaks.name = "Thunder Peaks";
    peaks.type = TerritoryType::Mountains;
    peaks.zoneType = ZoneType::SafeZone;
    peaks.controlStatus = ControlStatus::Controlled;
    peaks.controllingClan = Clan::StormClan;
    peaks.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(200.0f, 50.0f, -200.0f), 150.0f
    );
    peaks.allowedClans.insert(Clan::StormClan);
    addTerritory(std::move(peaks));

    // StormClan Arena
    Territory arena;
    arena.name = "Sky Arena";
    arena.type = TerritoryType::Mountains;
    arena.zoneType = ZoneType::PvPZone;
    arena.controlStatus = ControlStatus::Controlled;
    arena.controllingClan = Clan::StormClan;
    arena.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(250.0f, 80.0f, -150.0f), 60.0f
    );
    arena.requiredRank = 2; // Warrior or higher
    arena.allowedClans.insert(Clan::StormClan);
    addTerritory(std::move(arena));
}

void ClanTerritorySystem::createEmberClanTerritories() {
    // Main EmberClan Territory
    Territory volcanic;
    volcanic.name = "Emberforge Caldera";
    volcanic.type = TerritoryType::Volcanic;
    volcanic.zoneType = ZoneType::SafeZone;
    volcanic.controlStatus = ControlStatus::Controlled;
    volcanic.controllingClan = Clan::EmberClan;
    volcanic.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(200.0f, 0.0f, 200.0f), 150.0f
    );
    volcanic.allowedClans.insert(Clan::EmberClan);
    addTerritory(std::move(volcanic));

    // EmberClan Forge
    Territory forge;
    forge.name = "Flame Forge";
    forge.type = TerritoryType::Volcanic;
    forge.zoneType = ZoneType::TrainingZone;
    forge.controlStatus = ControlStatus::Controlled;
    forge.controllingClan = Clan::EmberClan;
    forge.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(180.0f, 0.0f, 250.0f), 40.0f
    );
    forge.requiredRank = 1;
    forge.allowedClans.insert(Clan::EmberClan);
    forge.resources.minerals = 100; // Rich in minerals
    addTerritory(std::move(forge));
}

void ClanTerritorySystem::createFrostClanTerritories() {
    // Main FrostClan Territory
    Territory tundra;
    tundra.name = "Frostbite Tundra";
    tundra.type = TerritoryType::Tundra;
    tundra.zoneType = ZoneType::SafeZone;
    tundra.controlStatus = ControlStatus::Controlled;
    tundra.controllingClan = Clan::FrostClan;
    tundra.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(-200.0f, 0.0f, 200.0f), 150.0f
    );
    tundra.allowedClans.insert(Clan::FrostClan);
    addTerritory(std::move(tundra));

    // FrostClan Sanctuary
    Territory sanctuary;
    sanctuary.name = "Ice Sanctuary";
    sanctuary.type = TerritoryType::Tundra;
    sanctuary.zoneType = ZoneType::SafeZone;
    sanctuary.controlStatus = ControlStatus::Controlled;
    sanctuary.controllingClan = Clan::FrostClan;
    sanctuary.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(-250.0f, 0.0f, 250.0f), 50.0f
    );
    sanctuary.requiredRank = 3; // Senior Warrior or higher
    sanctuary.allowedClans.insert(Clan::FrostClan);
    sanctuary.resources.herbs = 100; // Rich in healing herbs
    addTerritory(std::move(sanctuary));
}

void ClanTerritorySystem::createNeutralTerritories() {
    // Central Battleground
    Territory battleground;
    battleground.name = "Contested Battleground";
    battleground.type = TerritoryType::Neutral;
    battleground.zoneType = ZoneType::PvPZone;
    battleground.controlStatus = ControlStatus::Unclaimed;
    battleground.controllingClan = Clan::MistClan; // Placeholder
    battleground.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(0.0f, 0.0f, 0.0f), 100.0f
    );
    // All clans can access
    battleground.allowedClans.insert(Clan::MistClan);
    battleground.allowedClans.insert(Clan::StormClan);
    battleground.allowedClans.insert(Clan::EmberClan);
    battleground.allowedClans.insert(Clan::FrostClan);
    addTerritory(std::move(battleground));
}

void ClanTerritorySystem::createSacredTerritories() {
    // Mystical Nexus
    Territory nexus;
    nexus.name = "Mystical Nexus";
    nexus.type = TerritoryType::Sacred;
    nexus.zoneType = ZoneType::BossZone;
    nexus.controlStatus = ControlStatus::Unclaimed;
    nexus.controllingClan = Clan::MistClan; // Placeholder
    nexus.boundary = TerritoryHelpers::createCircularBoundary(
        Engine::vec3(0.0f, 100.0f, 0.0f), 80.0f
    );
    nexus.requiredRank = 4; // Deputy or higher
    addTerritory(std::move(nexus));
}

std::string ClanTerritorySystem::serialize() const {
    std::stringstream ss;
    ss << territories_.size() << ";";

    for (const auto& territory : territories_) {
        ss << territory.name << ","
           << static_cast<int>(territory.controlStatus) << ","
           << static_cast<int>(territory.controllingClan) << ","
           << territory.captureProgress << ","
           << territory.resources.foodAbundance << ","
           << territory.resources.herbs << ","
           << territory.resources.minerals << ","
           << territory.resources.mysticalEnergy << ";";
    }

    return ss.str();
}

bool ClanTerritorySystem::deserialize(const std::string& data) {
    // Basic deserialization - expand as needed
    std::stringstream ss(data);
    size_t count;
    char delimiter;

    if (!(ss >> count >> delimiter)) return false;

    // Load state for existing territories (matching by name)
    for (size_t i = 0; i < count && !ss.eof(); ++i) {
        std::string name;
        int status, clan;
        float progress;
        int food, herb, mineral;
        float mystical;

        std::getline(ss, name, ',');
        ss >> status >> delimiter >> clan >> delimiter
           >> progress >> delimiter
           >> food >> delimiter >> herb >> delimiter
           >> mineral >> delimiter >> mystical >> delimiter;

        Territory* territory = getTerritory(name);
        if (territory) {
            territory->controlStatus = static_cast<ControlStatus>(status);
            territory->controllingClan = static_cast<Clan>(clan);
            territory->captureProgress = progress;
            territory->resources.foodAbundance = food;
            territory->resources.herbs = herb;
            territory->resources.minerals = mineral;
            territory->resources.mysticalEnergy = mystical;
        }
    }

    return true;
}

// ============================================================================
// TerritoryHelpers Implementation
// ============================================================================

namespace TerritoryHelpers {

const char* getTerritoryTypeName(TerritoryType type) {
    switch (type) {
        case TerritoryType::Forest: return "Forest";
        case TerritoryType::Mountains: return "Mountains";
        case TerritoryType::Volcanic: return "Volcanic";
        case TerritoryType::Tundra: return "Tundra";
        case TerritoryType::Neutral: return "Neutral";
        case TerritoryType::Sacred: return "Sacred";
        default: return "Unknown";
    }
}

const char* getZoneTypeName(ZoneType type) {
    switch (type) {
        case ZoneType::SafeZone: return "Safe Zone";
        case ZoneType::PvPZone: return "PvP Zone";
        case ZoneType::BossZone: return "Boss Zone";
        case ZoneType::TrainingZone: return "Training Zone";
        default: return "Unknown";
    }
}

const char* getControlStatusName(ControlStatus status) {
    switch (status) {
        case ControlStatus::Controlled: return "Controlled";
        case ControlStatus::Contested: return "Contested";
        case ControlStatus::Lost: return "Lost";
        case ControlStatus::Unclaimed: return "Unclaimed";
        default: return "Unknown";
    }
}

TerritoryBoundary createCircularBoundary(const Engine::vec3& center, float radius) {
    TerritoryBoundary boundary;
    boundary.center = center;
    boundary.radius = radius;
    boundary.usePolygon = false;
    return boundary;
}

TerritoryBoundary createRectangularBoundary(
    const Engine::vec3& center,
    float width,
    float height
) {
    TerritoryBoundary boundary;
    boundary.usePolygon = true;

    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;

    boundary.polygon.push_back(center + Engine::vec3(-halfWidth, 0.0f, -halfHeight));
    boundary.polygon.push_back(center + Engine::vec3(halfWidth, 0.0f, -halfHeight));
    boundary.polygon.push_back(center + Engine::vec3(halfWidth, 0.0f, halfHeight));
    boundary.polygon.push_back(center + Engine::vec3(-halfWidth, 0.0f, halfHeight));

    return boundary;
}

TerritoryBoundary createPolygonalBoundary(const std::vector<Engine::vec3>& points) {
    TerritoryBoundary boundary;
    boundary.usePolygon = true;
    boundary.polygon = points;

    // Calculate center as average of points
    Engine::vec3 sum(0.0f, 0.0f, 0.0f);
    for (const auto& point : points) {
        sum += point;
    }
    boundary.center = sum / static_cast<float>(points.size());

    return boundary;
}

} // namespace TerritoryHelpers

} // namespace CatGame

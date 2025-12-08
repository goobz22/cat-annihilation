#pragma once

#include "../../engine/math/Vector.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace CatGame {

// Forward declarations
enum class Clan;

/**
 * Territory types for each clan
 */
enum class TerritoryType {
    Forest,      // MistClan territory - Dense woods, stealth advantage
    Mountains,   // StormClan territory - High peaks, speed advantage
    Volcanic,    // EmberClan territory - Lava fields, attack advantage
    Tundra,      // FrostClan territory - Frozen wastes, defense advantage
    Neutral,     // Unclaimed/shared territory
    Sacred       // Special mystical locations
};

/**
 * Territory zone type
 */
enum class ZoneType {
    SafeZone,    // No PvP allowed
    PvPZone,     // PvP enabled
    BossZone,    // Special encounter area
    TrainingZone // Skill training area
};

/**
 * Territory control status
 */
enum class ControlStatus {
    Controlled,   // Clan has full control
    Contested,    // Multiple clans fighting for control
    Lost,         // Recently lost to another clan
    Unclaimed     // No clan controls this territory
};

/**
 * Defines a geographic territory boundary
 */
struct TerritoryBoundary {
    Engine::vec3 center;
    float radius;                    // For circular territories
    std::vector<Engine::vec3> polygon; // For complex shapes (optional)
    bool usePolygon = false;

    /**
     * Check if a point is within territory bounds
     */
    bool contains(const Engine::vec3& point) const;

    /**
     * Get distance to nearest boundary edge
     */
    float distanceToBoundary(const Engine::vec3& point) const;

    /**
     * Get closest point on boundary
     */
    Engine::vec3 closestPointOnBoundary(const Engine::vec3& point) const;
};

/**
 * Territory resource data
 */
struct TerritoryResources {
    int foodAbundance = 50;      // 0-100, affects hunting success
    int herbs = 50;              // 0-100, affects healing items
    int minerals = 50;           // 0-100, affects crafting materials
    float mysticalEnergy = 0.0f; // Special energy for mysticism skills

    /**
     * Regenerate resources over time
     */
    void regenerate(float deltaTime);

    /**
     * Harvest resources
     */
    bool harvest(int& food, int& herb, int& mineral, float harvestPower);
};

/**
 * Complete territory definition
 */
struct Territory {
    std::string name;
    TerritoryType type;
    ZoneType zoneType;
    ControlStatus controlStatus;
    Clan controllingClan;

    TerritoryBoundary boundary;
    TerritoryResources resources;

    // Territory modifiers
    float damageMultiplier = 1.0f;    // Combat modifier in this territory
    float defenseMultiplier = 1.0f;   // Defense modifier
    float speedMultiplier = 1.0f;     // Movement speed modifier
    float stealthBonus = 0.0f;        // Stealth effectiveness bonus

    // Access control
    std::unordered_set<Clan> allowedClans;
    int requiredRank = 0;             // Minimum rank to enter (0-5)

    // Capture mechanics
    float captureProgress = 0.0f;     // 0.0 to 100.0
    float captureRate = 1.0f;         // Points per second when capturing
    Clan capturingClan;
    int defendersPresent = 0;
    int attackersPresent = 0;

    /**
     * Check if a clan can access this territory
     */
    bool canAccess(Clan clan, int playerRank) const;

    /**
     * Get territory bonus for a specific clan
     */
    float getTerritoryBonus(Clan clan) const;

    /**
     * Update capture progress
     */
    void updateCapture(float deltaTime);

    /**
     * Check if PvP is enabled
     */
    bool isPvPEnabled() const { return zoneType == ZoneType::PvPZone; }

    /**
     * Apply territory effects to combat stats
     */
    void applyTerritoryEffects(float& damage, float& defense, float& speed, Clan playerClan) const;
};

/**
 * Territory warning when approaching boundaries
 */
struct TerritoryWarning {
    std::string message;
    Territory* territory;
    float distanceToBoundary;
    bool canEnter;
};

/**
 * Clan Territory Management System
 */
class ClanTerritorySystem {
public:
    ClanTerritorySystem();
    ~ClanTerritorySystem() = default;

    /**
     * Initialize all territories with default values
     */
    void initialize();

    /**
     * Update territory system (capture progress, resource regen, etc.)
     */
    void update(float deltaTime);

    /**
     * Get territory at a specific world position
     */
    Territory* getTerritoryAt(const Engine::vec3& position);
    const Territory* getTerritoryAt(const Engine::vec3& position) const;

    /**
     * Get territory by name
     */
    Territory* getTerritory(const std::string& name);
    const Territory* getTerritory(const std::string& name) const;

    /**
     * Get all territories controlled by a clan
     */
    std::vector<Territory*> getTerritoriesByLan(Clan clan);

    /**
     * Check if player can enter a territory
     */
    bool canEnterTerritory(const Territory* territory, Clan playerClan, int playerRank) const;

    /**
     * Get warning when approaching restricted territory
     */
    TerritoryWarning checkTerritoryWarning(
        const Engine::vec3& position,
        Clan playerClan,
        int playerRank
    ) const;

    /**
     * Unlock territory for a clan (story progression)
     */
    void unlockTerritory(const std::string& territoryName, Clan clan);

    /**
     * Start territory capture
     */
    bool startCapture(const std::string& territoryName, Clan attackingClan);

    /**
     * Complete territory capture
     */
    void completeCapture(const std::string& territoryName);

    /**
     * Reset capture progress
     */
    void resetCapture(const std::string& territoryName);

    /**
     * Update player presence in territories
     */
    void updatePlayerPresence(
        const Engine::vec3& position,
        Clan playerClan,
        bool isDefending
    );

    /**
     * Get all territories
     */
    const std::vector<Territory>& getAllTerritories() const { return territories_; }

    /**
     * Get territory bonuses for display
     */
    std::string getTerritoryBonusDescription(const Territory* territory, Clan clan) const;

    /**
     * Serialize territory states for save system
     */
    std::string serialize() const;

    /**
     * Deserialize territory states from save data
     */
    bool deserialize(const std::string& data);

    /**
     * Register territory event callbacks
     */
    void onTerritoryEntered(void (*callback)(const Territory*, Clan));
    void onTerritoryLeft(void (*callback)(const Territory*, Clan));
    void onTerritoryCaptured(void (*callback)(const Territory*, Clan, Clan)); // territory, old owner, new owner

private:
    std::vector<Territory> territories_;
    std::unordered_map<std::string, size_t> territoryNameMap_;

    // Event callbacks
    void (*territoryEnteredCallback_)(const Territory*, Clan) = nullptr;
    void (*territoryLeftCallback_)(const Territory*, Clan) = nullptr;
    void (*territoryCapturedCallback_)(const Territory*, Clan, Clan) = nullptr;

    // Territory tracking
    Territory* lastTerritory_ = nullptr;

    // Helper functions
    void createMistClanTerritories();
    void createStormClanTerritories();
    void createEmberClanTerritories();
    void createFrostClanTerritories();
    void createNeutralTerritories();
    void createSacredTerritories();

    /**
     * Add a new territory to the system
     */
    void addTerritory(Territory&& territory);

    /**
     * Calculate territory-specific bonuses
     */
    void calculateTerritoryBonuses(Territory& territory);
};

/**
 * Helper functions for territory management
 */
namespace TerritoryHelpers {
    /**
     * Get territory type name as string
     */
    const char* getTerritoryTypeName(TerritoryType type);

    /**
     * Get zone type name as string
     */
    const char* getZoneTypeName(ZoneType type);

    /**
     * Get control status name as string
     */
    const char* getControlStatusName(ControlStatus status);

    /**
     * Create circular territory boundary
     */
    TerritoryBoundary createCircularBoundary(const Engine::vec3& center, float radius);

    /**
     * Create rectangular territory boundary
     */
    TerritoryBoundary createRectangularBoundary(
        const Engine::vec3& center,
        float width,
        float height
    );

    /**
     * Create polygonal territory boundary
     */
    TerritoryBoundary createPolygonalBoundary(const std::vector<Engine::vec3>& points);
}

} // namespace CatGame

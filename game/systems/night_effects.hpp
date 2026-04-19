#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include "day_night_cycle.hpp"
#include <vector>
#include <unordered_map>

namespace CatGame {

/**
 * Light source types for night illumination
 */
enum class LightSourceType {
    Campfire,        // Static campfire (safe zone)
    Torch,           // Player-held torch
    Flashlight,      // Directional flashlight
    Lantern,         // Area lantern
    GlowStick        // Thrown light source
};

/**
 * Night light source component
 */
struct NightLightSource {
    LightSourceType type = LightSourceType::Torch;
    Engine::vec3 color = Engine::vec3(1.0f, 0.8f, 0.5f);  // Warm firelight
    float intensity = 1.0f;
    float radius = 10.0f;
    bool isActive = true;
    float flickerAmount = 0.0f;  // 0.0 = no flicker, 1.0 = heavy flicker
    float flickerSpeed = 1.0f;

    // For campfires
    bool isSafeZone = false;

    // For directional lights (flashlight)
    float coneAngle = 45.0f;     // Degrees
    float coneAttenuation = 0.8f;
};

/**
 * Nocturnal enemy tag
 * Enemies with this component only spawn at night
 */
struct NocturnalEnemy {
    float nightVisionRange = 30.0f;  // Can see player from this distance at night
    float dayVisionRange = 10.0f;    // Reduced vision during day
    bool despawnAtDawn = true;       // Despawn when day breaks
    float aggressionBonus = 0.5f;    // 50% more aggressive at night
};

/**
 * Player visibility component
 * Tracks how visible the player is to enemies
 */
struct PlayerVisibility {
    float baseVisibility = 1.0f;        // Base visibility level
    float lightSourceModifier = 0.0f;   // Modifier from nearby light sources
    float movementModifier = 0.0f;      // Moving makes you more visible
    float currentVisibility = 1.0f;     // Computed total visibility

    // Detection ranges
    float detectionRangeDay = 20.0f;
    float detectionRangeNight = 10.0f;  // Harder to see at night
};

/**
 * Night Effects System
 * Manages night-specific gameplay effects, enemy behavior, and lighting
 */
class NightEffectsSystem : public CatEngine::System {
public:
    explicit NightEffectsSystem(DayNightCycleSystem* dayCycle, int priority = 300);
    ~NightEffectsSystem() override = default;

    void init(CatEngine::ECS* ecs) override;
    void update(float dt) override;
    const char* getName() const override { return "NightEffectsSystem"; }

    // ========================================================================
    // Light Source Management
    // ========================================================================

    /**
     * Create a campfire light source
     */
    CatEngine::Entity createCampfire(const Engine::vec3& position, bool isSafeZone = true);

    /**
     * Give player a torch
     */
    void givePlayerTorch(CatEngine::Entity player);

    /**
     * Give player a flashlight
     */
    void givePlayerFlashlight(CatEngine::Entity player);

    /**
     * Toggle player's light source
     */
    void togglePlayerLight(CatEngine::Entity player);

    /**
     * Throw a glow stick
     */
    CatEngine::Entity throwGlowStick(const Engine::vec3& position, const Engine::vec3& velocity);

    // ========================================================================
    // Enemy Behavior
    // ========================================================================

    /**
     * Mark an enemy as nocturnal
     */
    void makeEnemyNocturnal(CatEngine::Entity enemy, float nightVisionRange = 30.0f);

    /**
     * Get enemy vision range (adjusted for time of day)
     */
    float getEnemyVisionRange(CatEngine::Entity enemy) const;

    /**
     * Get enemy aggression multiplier (higher at night)
     */
    float getEnemyAggressionMultiplier(CatEngine::Entity enemy) const;

    /**
     * Check if an enemy should despawn (nocturnal enemies at dawn)
     */
    bool shouldDespawnEnemy(CatEngine::Entity enemy) const;

    // ========================================================================
    // Player Visibility
    // ========================================================================

    /**
     * Setup player visibility tracking
     */
    void setupPlayerVisibility(CatEngine::Entity player);

    /**
     * Get player's current visibility (0.0 = invisible, 1.0 = fully visible)
     */
    float getPlayerVisibility(CatEngine::Entity player) const;

    /**
     * Get player detection range for enemies
     */
    float getPlayerDetectionRange(CatEngine::Entity player) const;

    /**
     * Check if player is in a safe zone (near campfire)
     */
    bool isPlayerInSafeZone(CatEngine::Entity player, float* distanceToSafeZone = nullptr) const;

    // ========================================================================
    // Queries
    // ========================================================================

    /**
     * Get all active light sources
     */
    const std::vector<CatEngine::Entity>& getActiveLightSources() const { return activeLightSources_; }

    /**
     * Get nearest light source to a position
     */
    CatEngine::Entity getNearestLightSource(const Engine::vec3& position, float maxDistance = 100.0f) const;

    /**
     * Calculate total illumination at a position
     */
    float getIlluminationAt(const Engine::vec3& position) const;

    /**
     * Query the flicker multiplier applied to a light source this frame.
     * Returns 1.0 for entities that aren't flickering or that the
     * renderer can safely draw at full base intensity. Updated by
     * updateLightFlickering() so the renderer can read it in the same
     * frame without racing the update.
     */
    float getLightFlickerMultiplier(CatEngine::Entity light) const;

    /**
     * Query the current (time-of-day-adjusted) aggression multiplier for
     * a nocturnal enemy. Written by updateNocturnalEnemies() each tick so
     * the AI system can multiply base aggression by this factor when
     * scoring targets — without having to re-compute the time-of-day
     * weighting itself.
     */
    float getCurrentAggressionMultiplier(CatEngine::Entity enemy) const;

private:
    // ========================================================================
    // Update Methods
    // ========================================================================

    /**
     * Update light source flickering
     */
    void updateLightFlickering(float dt);

    /**
     * Update player visibility calculations
     */
    void updatePlayerVisibility(float dt);

    /**
     * Update nocturnal enemy behavior
     */
    void updateNocturnalEnemies(float dt);

    /**
     * Despawn nocturnal enemies at dawn
     */
    void despawnNocturnalEnemies();

    /**
     * Update glow stick physics
     */
    void updateGlowSticks(float dt);

    /**
     * Calculate visibility modifier from light sources
     */
    float calculateLightModifier(const Engine::vec3& position) const;

    /**
     * Calculate movement modifier for visibility
     */
    float calculateMovementModifier(CatEngine::Entity entity, float dt) const;

    // ========================================================================
    // State
    // ========================================================================

    DayNightCycleSystem* dayCycle_;
    std::vector<CatEngine::Entity> activeLightSources_;
    std::vector<CatEngine::Entity> nocturnalEnemies_;
    std::vector<CatEngine::Entity> glowSticks_;

    // Per-frame scratch maps populated by updateLightFlickering and
    // updateNocturnalEnemies and consumed by the renderer / AI system via
    // the public getters above. Kept as std::unordered_map (not packed
    // arrays) because the light and enemy sets are both small (<100 each
    // in typical gameplay) and the hash-lookup hot path is amortised
    // across the whole frame, not hit in a tight inner loop.
    std::unordered_map<CatEngine::Entity, float> lightFlickerMultipliers_;
    std::unordered_map<CatEngine::Entity, float> aggressionMultipliers_;

    bool lastFrameWasNight_ = false;
    float timeAccumulator_ = 0.0f;
};

} // namespace CatGame

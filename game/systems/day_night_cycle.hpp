#pragma once

#include "../../engine/math/Vector.hpp"
#include <string>
#include <cstdint>

namespace CatGame {

/**
 * Day/Night Cycle State
 * Tracks current time of day and related parameters
 */
struct DayCycleState {
    float currentTime = 0.5f;        // 0.0 = midnight, 0.5 = noon, 1.0 = midnight
    bool isNight = false;
    float dayLength = 600.0f;        // 10 minutes real time = 1 game day
    int currentDay = 1;

    // Lighting parameters (computed from currentTime)
    Engine::vec3 sunDirection;
    Engine::vec3 sunColor;
    float sunIntensity;
    Engine::vec3 ambientColor;
    float shadowStrength;

    // Sky parameters
    Engine::vec3 skyColorTop;
    Engine::vec3 skyColorBottom;
    float starVisibility;
    float moonPhase;                 // 0-1 for moon cycle (28 day cycle)

    // Gameplay modifiers
    float nightVisionRequired;       // 0.0 = none, 1.0 = full darkness
    float enemySpawnMultiplier;      // More enemies at night
    float stealthBonus;              // Easier to hide at night
};

/**
 * Time of Day enumeration
 */
enum class TimeOfDay {
    Night,      // 0.00-0.20, 0.80-1.00
    Dawn,       // 0.20-0.30
    Morning,    // 0.30-0.45
    Noon,       // 0.45-0.55
    Afternoon,  // 0.55-0.70
    Dusk,       // 0.70-0.80
};

/**
 * Day/Night Cycle System
 * Manages time progression, sun/moon position, lighting, and atmospheric effects
 */
class DayNightCycleSystem {
public:
    DayNightCycleSystem();
    ~DayNightCycleSystem() = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * Initialize the day/night cycle system
     */
    void initialize();

    /**
     * Update the cycle (call every frame)
     */
    void update(float deltaTime);

    /**
     * Shutdown and cleanup
     */
    void shutdown();

    // ========================================================================
    // Time Control
    // ========================================================================

    /**
     * Set time of day (0.0 = midnight, 0.5 = noon, 1.0 = midnight)
     */
    void setTimeOfDay(float time);

    /**
     * Set the length of a full day in seconds
     */
    void setDayLength(float seconds);

    /**
     * Pause time progression
     */
    void pauseTime();

    /**
     * Resume time progression
     */
    void resumeTime();

    /**
     * Advance to next day
     */
    void advanceDay();

    /**
     * Set current day number (for save loading)
     */
    void setCurrentDay(int day);

    /**
     * Set time scale multiplier (2.0 = 2x speed, 0.5 = half speed)
     */
    void setTimeScale(float scale);

    // ========================================================================
    // Time Queries
    // ========================================================================

    /**
     * Get current time of day (0.0-1.0)
     */
    float getCurrentTime() const { return state_.currentTime; }

    /**
     * Check if it's currently night
     */
    bool isNight() const { return state_.isNight; }

    /**
     * Check if it's dawn (0.20-0.30)
     */
    bool isDawn() const;

    /**
     * Check if it's dusk (0.70-0.80)
     */
    bool isDusk() const;

    /**
     * Get current day number
     */
    int getCurrentDay() const { return state_.currentDay; }

    /**
     * Get time of day category
     */
    TimeOfDay getTimeOfDay() const;

    /**
     * Get formatted time string (e.g., "14:30")
     */
    std::string getFormattedTime() const;

    /**
     * Get time of day name (e.g., "Morning", "Afternoon")
     */
    std::string getTimeOfDayName() const;

    /**
     * Get hours (0-23)
     */
    int getHours() const;

    /**
     * Get minutes (0-59)
     */
    int getMinutes() const;

    // ========================================================================
    // Lighting & Atmosphere Queries
    // ========================================================================

    /**
     * Get sun direction (normalized world space vector)
     */
    Engine::vec3 getSunDirection() const { return state_.sunDirection; }

    /**
     * Get sun color
     */
    Engine::vec3 getSunColor() const { return state_.sunColor; }

    /**
     * Get sun intensity
     */
    float getSunIntensity() const { return state_.sunIntensity; }

    /**
     * Get ambient light color
     */
    Engine::vec3 getAmbientColor() const { return state_.ambientColor; }

    /**
     * Get shadow strength (0.0 = no shadows, 1.0 = full shadows)
     */
    float getShadowStrength() const { return state_.shadowStrength; }

    /**
     * Get sky color (top/zenith)
     */
    Engine::vec3 getSkyColorTop() const { return state_.skyColorTop; }

    /**
     * Get sky color (bottom/horizon)
     */
    Engine::vec3 getSkyColorBottom() const { return state_.skyColorBottom; }

    /**
     * Get star visibility (0.0 = invisible, 1.0 = fully visible)
     */
    float getStarVisibility() const { return state_.starVisibility; }

    /**
     * Get moon phase (0.0 = new moon, 0.5 = full moon, 1.0 = new moon)
     */
    float getMoonPhase() const { return state_.moonPhase; }

    /**
     * Get moon direction (normalized world space vector)
     */
    Engine::vec3 getMoonDirection() const;

    /**
     * Get moon brightness (0.0 = new moon/day, 1.0 = full moon at night)
     */
    float getMoonBrightness() const;

    // ========================================================================
    // Gameplay Effects
    // ========================================================================

    /**
     * Get night vision requirement (0.0 = bright, 1.0 = need flashlight/night vision)
     */
    float getNightVisionRequired() const { return state_.nightVisionRequired; }

    /**
     * Get enemy spawn multiplier (higher at night)
     */
    float getEnemySpawnMultiplier() const { return state_.enemySpawnMultiplier; }

    /**
     * Get stealth bonus (higher at night)
     */
    float getStealthBonus() const { return state_.stealthBonus; }

    /**
     * Get complete state (for rendering)
     */
    const DayCycleState& getState() const { return state_; }

private:
    // ========================================================================
    // Internal Update Methods
    // ========================================================================

    /**
     * Update sun position and color based on time
     */
    void updateSun();

    /**
     * Update ambient lighting
     */
    void updateAmbientLight();

    /**
     * Update sky colors
     */
    void updateSkyColors();

    /**
     * Update stars and moon
     */
    void updateCelestialBodies();

    /**
     * Update gameplay modifiers
     */
    void updateGameplayModifiers();

    /**
     * Smooth interpolation between colors with optional curve
     */
    Engine::vec3 interpolateColor(const Engine::vec3& a, const Engine::vec3& b, float t) const;

    /**
     * Apply smooth step curve to value
     */
    float smoothCurve(float t) const;

    // ========================================================================
    // State
    // ========================================================================

    DayCycleState state_;
    bool paused_ = false;
    float timeScale_ = 1.0f;
    bool initialized_ = false;
};

} // namespace CatGame

#include "day_night_cycle.hpp"
#include "../../engine/math/Math.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace CatGame {

// Color presets for different times of day
namespace DayNightColors {
    // Sun colors
    const Engine::vec3 SUN_MIDNIGHT     = Engine::vec3(0.0f, 0.0f, 0.0f);
    const Engine::vec3 SUN_DAWN         = Engine::vec3(1.0f, 0.4f, 0.2f);
    const Engine::vec3 SUN_MORNING      = Engine::vec3(1.0f, 0.95f, 0.8f);
    const Engine::vec3 SUN_NOON         = Engine::vec3(1.0f, 1.0f, 0.95f);
    const Engine::vec3 SUN_AFTERNOON    = Engine::vec3(1.0f, 0.95f, 0.85f);
    const Engine::vec3 SUN_DUSK         = Engine::vec3(1.0f, 0.3f, 0.1f);

    // Sky colors (top/zenith)
    const Engine::vec3 SKY_TOP_NIGHT    = Engine::vec3(0.01f, 0.01f, 0.05f);
    const Engine::vec3 SKY_TOP_DAWN     = Engine::vec3(0.3f, 0.4f, 0.6f);
    const Engine::vec3 SKY_TOP_DAY      = Engine::vec3(0.2f, 0.5f, 0.9f);
    const Engine::vec3 SKY_TOP_DUSK     = Engine::vec3(0.2f, 0.3f, 0.5f);

    // Sky colors (bottom/horizon)
    const Engine::vec3 SKY_BOTTOM_NIGHT = Engine::vec3(0.02f, 0.02f, 0.08f);
    const Engine::vec3 SKY_BOTTOM_DAWN  = Engine::vec3(1.0f, 0.6f, 0.3f);
    const Engine::vec3 SKY_BOTTOM_DAY   = Engine::vec3(0.7f, 0.85f, 1.0f);
    const Engine::vec3 SKY_BOTTOM_DUSK  = Engine::vec3(1.0f, 0.4f, 0.2f);

    // Ambient light colors
    const Engine::vec3 AMBIENT_NIGHT    = Engine::vec3(0.05f, 0.05f, 0.1f);
    const Engine::vec3 AMBIENT_DAWN     = Engine::vec3(0.3f, 0.25f, 0.3f);
    const Engine::vec3 AMBIENT_DAY      = Engine::vec3(0.4f, 0.45f, 0.5f);
    const Engine::vec3 AMBIENT_DUSK     = Engine::vec3(0.3f, 0.2f, 0.25f);
}

DayNightCycleSystem::DayNightCycleSystem() {
}

// ============================================================================
// Lifecycle
// ============================================================================

void DayNightCycleSystem::initialize() {
    if (initialized_) {
        return;
    }

    // Set initial state to noon
    state_.currentTime = 0.5f;
    state_.dayLength = 600.0f;  // 10 minutes
    state_.currentDay = 1;
    state_.moonPhase = 0.0f;

    // Update all parameters
    updateSun();
    updateAmbientLight();
    updateSkyColors();
    updateCelestialBodies();
    updateGameplayModifiers();

    initialized_ = true;
}

void DayNightCycleSystem::update(float deltaTime) {
    if (!initialized_ || paused_) {
        return;
    }

    // Update time
    float timeStep = (deltaTime * timeScale_) / state_.dayLength;
    state_.currentTime += timeStep;

    // Wrap time to [0, 1]
    if (state_.currentTime >= 1.0f) {
        state_.currentTime -= 1.0f;
        advanceDay();
    }

    // Update night flag
    state_.isNight = (state_.currentTime < 0.2f || state_.currentTime > 0.8f);

    // Update all parameters based on new time
    updateSun();
    updateAmbientLight();
    updateSkyColors();
    updateCelestialBodies();
    updateGameplayModifiers();
}

void DayNightCycleSystem::shutdown() {
    initialized_ = false;
}

// ============================================================================
// Time Control
// ============================================================================

void DayNightCycleSystem::setTimeOfDay(float time) {
    state_.currentTime = std::clamp(time, 0.0f, 1.0f);
    state_.isNight = (state_.currentTime < 0.2f || state_.currentTime > 0.8f);

    // Update all parameters
    updateSun();
    updateAmbientLight();
    updateSkyColors();
    updateCelestialBodies();
    updateGameplayModifiers();
}

void DayNightCycleSystem::setDayLength(float seconds) {
    state_.dayLength = std::max(1.0f, seconds);
}

void DayNightCycleSystem::pauseTime() {
    paused_ = true;
}

void DayNightCycleSystem::resumeTime() {
    paused_ = false;
}

void DayNightCycleSystem::advanceDay() {
    state_.currentDay++;

    // Update moon phase (28-day cycle)
    state_.moonPhase = fmodf(static_cast<float>(state_.currentDay) / 28.0f, 1.0f);
}

void DayNightCycleSystem::setTimeScale(float scale) {
    timeScale_ = std::max(0.0f, scale);
}

// ============================================================================
// Time Queries
// ============================================================================

bool DayNightCycleSystem::isDawn() const {
    return state_.currentTime >= 0.2f && state_.currentTime < 0.3f;
}

bool DayNightCycleSystem::isDusk() const {
    return state_.currentTime >= 0.7f && state_.currentTime < 0.8f;
}

TimeOfDay DayNightCycleSystem::getTimeOfDay() const {
    if (state_.currentTime < 0.2f || state_.currentTime >= 0.8f) {
        return TimeOfDay::Night;
    } else if (state_.currentTime < 0.3f) {
        return TimeOfDay::Dawn;
    } else if (state_.currentTime < 0.45f) {
        return TimeOfDay::Morning;
    } else if (state_.currentTime < 0.55f) {
        return TimeOfDay::Noon;
    } else if (state_.currentTime < 0.7f) {
        return TimeOfDay::Afternoon;
    } else {
        return TimeOfDay::Dusk;
    }
}

std::string DayNightCycleSystem::getFormattedTime() const {
    int hours = getHours();
    int minutes = getMinutes();

    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setfill('0') << std::setw(2) << minutes;
    return ss.str();
}

std::string DayNightCycleSystem::getTimeOfDayName() const {
    switch (getTimeOfDay()) {
        case TimeOfDay::Night:     return "Night";
        case TimeOfDay::Dawn:      return "Dawn";
        case TimeOfDay::Morning:   return "Morning";
        case TimeOfDay::Noon:      return "Noon";
        case TimeOfDay::Afternoon: return "Afternoon";
        case TimeOfDay::Dusk:      return "Dusk";
        default:                   return "Unknown";
    }
}

int DayNightCycleSystem::getHours() const {
    return static_cast<int>(state_.currentTime * 24.0f) % 24;
}

int DayNightCycleSystem::getMinutes() const {
    float fractionalHour = fmodf(state_.currentTime * 24.0f, 1.0f);
    return static_cast<int>(fractionalHour * 60.0f);
}

// ============================================================================
// Moon Queries
// ============================================================================

Engine::vec3 DayNightCycleSystem::getMoonDirection() const {
    // Moon is opposite to sun
    return -state_.sunDirection;
}

float DayNightCycleSystem::getMoonBrightness() const {
    if (!state_.isNight) {
        return 0.0f;  // Moon not visible during day
    }

    // Moon brightness depends on phase
    // Full moon (phase 0.5) = 1.0 brightness
    // New moon (phase 0.0 or 1.0) = 0.0 brightness
    float phaseBrightness = 1.0f - std::abs(state_.moonPhase * 2.0f - 1.0f);

    // Also fade based on time (brightest at midnight)
    float timeBrightness = 1.0f;
    if (state_.currentTime < 0.2f) {
        // Early night (evening)
        timeBrightness = state_.currentTime / 0.2f;
    } else if (state_.currentTime > 0.8f) {
        // Late night (morning)
        timeBrightness = (1.0f - state_.currentTime) / 0.2f;
    }

    return phaseBrightness * timeBrightness * 0.3f;  // Max brightness = 0.3
}

// ============================================================================
// Internal Update Methods
// ============================================================================

void DayNightCycleSystem::updateSun() {
    // Sun rotates from east to west
    // 0.0 (midnight) = below horizon (south)
    // 0.25 (6am) = east horizon
    // 0.5 (noon) = overhead (south)
    // 0.75 (6pm) = west horizon
    // 1.0 (midnight) = below horizon (south)

    float angle = state_.currentTime * 2.0f * Engine::PI;

    // Sun position (rotates around X axis, moves in YZ plane)
    float sunHeight = std::sin(angle);
    float sunHorizontal = std::cos(angle);

    state_.sunDirection = Engine::vec3(
        sunHorizontal,    // East-West
        -sunHeight,       // Up-Down (negative because pointing down when high)
        0.0f              // North-South
    );
    state_.sunDirection = Engine::normalize(state_.sunDirection);

    // Sun color changes based on angle
    if (state_.currentTime < 0.2f) {
        // Night (0.0-0.2)
        state_.sunColor = DayNightColors::SUN_MIDNIGHT;
        state_.sunIntensity = 0.0f;
    } else if (state_.currentTime < 0.3f) {
        // Dawn (0.2-0.3)
        float t = (state_.currentTime - 0.2f) / 0.1f;
        state_.sunColor = interpolateColor(DayNightColors::SUN_DAWN, DayNightColors::SUN_MORNING, smoothCurve(t));
        state_.sunIntensity = smoothCurve(t) * 15.0f;
    } else if (state_.currentTime < 0.45f) {
        // Morning (0.3-0.45)
        float t = (state_.currentTime - 0.3f) / 0.15f;
        state_.sunColor = interpolateColor(DayNightColors::SUN_MORNING, DayNightColors::SUN_NOON, t);
        state_.sunIntensity = 15.0f + t * 5.0f;  // 15-20
    } else if (state_.currentTime < 0.55f) {
        // Noon (0.45-0.55)
        state_.sunColor = DayNightColors::SUN_NOON;
        state_.sunIntensity = 20.0f;
    } else if (state_.currentTime < 0.7f) {
        // Afternoon (0.55-0.7)
        float t = (state_.currentTime - 0.55f) / 0.15f;
        state_.sunColor = interpolateColor(DayNightColors::SUN_NOON, DayNightColors::SUN_AFTERNOON, t);
        state_.sunIntensity = 20.0f - t * 3.0f;  // 20-17
    } else if (state_.currentTime < 0.8f) {
        // Dusk (0.7-0.8)
        float t = (state_.currentTime - 0.7f) / 0.1f;
        state_.sunColor = interpolateColor(DayNightColors::SUN_AFTERNOON, DayNightColors::SUN_DUSK, smoothCurve(t));
        state_.sunIntensity = 17.0f * (1.0f - smoothCurve(t));
    } else {
        // Night (0.8-1.0)
        state_.sunColor = DayNightColors::SUN_MIDNIGHT;
        state_.sunIntensity = 0.0f;
    }

    // Shadow strength follows sun intensity
    state_.shadowStrength = std::clamp(state_.sunIntensity / 20.0f, 0.0f, 1.0f);
}

void DayNightCycleSystem::updateAmbientLight() {
    if (state_.currentTime < 0.2f) {
        // Night (0.0-0.2)
        state_.ambientColor = DayNightColors::AMBIENT_NIGHT;
    } else if (state_.currentTime < 0.3f) {
        // Dawn (0.2-0.3)
        float t = (state_.currentTime - 0.2f) / 0.1f;
        state_.ambientColor = interpolateColor(DayNightColors::AMBIENT_NIGHT, DayNightColors::AMBIENT_DAWN, smoothCurve(t));
    } else if (state_.currentTime < 0.35f) {
        // Dawn to Day transition
        float t = (state_.currentTime - 0.3f) / 0.05f;
        state_.ambientColor = interpolateColor(DayNightColors::AMBIENT_DAWN, DayNightColors::AMBIENT_DAY, t);
    } else if (state_.currentTime < 0.65f) {
        // Day (0.35-0.65)
        state_.ambientColor = DayNightColors::AMBIENT_DAY;
    } else if (state_.currentTime < 0.7f) {
        // Day to Dusk transition
        float t = (state_.currentTime - 0.65f) / 0.05f;
        state_.ambientColor = interpolateColor(DayNightColors::AMBIENT_DAY, DayNightColors::AMBIENT_DUSK, t);
    } else if (state_.currentTime < 0.8f) {
        // Dusk (0.7-0.8)
        float t = (state_.currentTime - 0.7f) / 0.1f;
        state_.ambientColor = interpolateColor(DayNightColors::AMBIENT_DUSK, DayNightColors::AMBIENT_NIGHT, smoothCurve(t));
    } else {
        // Night (0.8-1.0)
        state_.ambientColor = DayNightColors::AMBIENT_NIGHT;
    }
}

void DayNightCycleSystem::updateSkyColors() {
    // Update top/zenith color
    if (state_.currentTime < 0.2f) {
        // Night
        state_.skyColorTop = DayNightColors::SKY_TOP_NIGHT;
        state_.skyColorBottom = DayNightColors::SKY_BOTTOM_NIGHT;
    } else if (state_.currentTime < 0.3f) {
        // Dawn
        float t = (state_.currentTime - 0.2f) / 0.1f;
        state_.skyColorTop = interpolateColor(DayNightColors::SKY_TOP_NIGHT, DayNightColors::SKY_TOP_DAWN, smoothCurve(t));
        state_.skyColorBottom = interpolateColor(DayNightColors::SKY_BOTTOM_NIGHT, DayNightColors::SKY_BOTTOM_DAWN, smoothCurve(t));
    } else if (state_.currentTime < 0.4f) {
        // Dawn to day transition
        float t = (state_.currentTime - 0.3f) / 0.1f;
        state_.skyColorTop = interpolateColor(DayNightColors::SKY_TOP_DAWN, DayNightColors::SKY_TOP_DAY, t);
        state_.skyColorBottom = interpolateColor(DayNightColors::SKY_BOTTOM_DAWN, DayNightColors::SKY_BOTTOM_DAY, t);
    } else if (state_.currentTime < 0.6f) {
        // Day
        state_.skyColorTop = DayNightColors::SKY_TOP_DAY;
        state_.skyColorBottom = DayNightColors::SKY_BOTTOM_DAY;
    } else if (state_.currentTime < 0.7f) {
        // Day to dusk transition
        float t = (state_.currentTime - 0.6f) / 0.1f;
        state_.skyColorTop = interpolateColor(DayNightColors::SKY_TOP_DAY, DayNightColors::SKY_TOP_DUSK, t);
        state_.skyColorBottom = interpolateColor(DayNightColors::SKY_BOTTOM_DAY, DayNightColors::SKY_BOTTOM_DUSK, t);
    } else if (state_.currentTime < 0.8f) {
        // Dusk
        float t = (state_.currentTime - 0.7f) / 0.1f;
        state_.skyColorTop = interpolateColor(DayNightColors::SKY_TOP_DUSK, DayNightColors::SKY_TOP_NIGHT, smoothCurve(t));
        state_.skyColorBottom = interpolateColor(DayNightColors::SKY_BOTTOM_DUSK, DayNightColors::SKY_BOTTOM_NIGHT, smoothCurve(t));
    } else {
        // Night
        state_.skyColorTop = DayNightColors::SKY_TOP_NIGHT;
        state_.skyColorBottom = DayNightColors::SKY_BOTTOM_NIGHT;
    }
}

void DayNightCycleSystem::updateCelestialBodies() {
    // Stars are only visible at night
    if (state_.currentTime < 0.15f) {
        // Deep night (0.0-0.15)
        state_.starVisibility = 1.0f;
    } else if (state_.currentTime < 0.25f) {
        // Dawn fade (0.15-0.25)
        float t = (state_.currentTime - 0.15f) / 0.1f;
        state_.starVisibility = 1.0f - smoothCurve(t);
    } else if (state_.currentTime < 0.75f) {
        // Day (0.25-0.75)
        state_.starVisibility = 0.0f;
    } else if (state_.currentTime < 0.85f) {
        // Dusk fade in (0.75-0.85)
        float t = (state_.currentTime - 0.75f) / 0.1f;
        state_.starVisibility = smoothCurve(t);
    } else {
        // Deep night (0.85-1.0)
        state_.starVisibility = 1.0f;
    }
}

void DayNightCycleSystem::updateGameplayModifiers() {
    // Night vision required (darkness level)
    if (state_.currentTime < 0.2f || state_.currentTime > 0.8f) {
        // Night
        state_.nightVisionRequired = 0.8f;
    } else if (state_.currentTime < 0.3f) {
        // Dawn
        float t = (state_.currentTime - 0.2f) / 0.1f;
        state_.nightVisionRequired = 0.8f * (1.0f - smoothCurve(t));
    } else if (state_.currentTime < 0.7f) {
        // Day
        state_.nightVisionRequired = 0.0f;
    } else {
        // Dusk
        float t = (state_.currentTime - 0.7f) / 0.1f;
        state_.nightVisionRequired = 0.8f * smoothCurve(t);
    }

    // Enemy spawn multiplier (more enemies at night)
    if (state_.isNight) {
        state_.enemySpawnMultiplier = 1.5f;
    } else if (isDawn() || isDusk()) {
        state_.enemySpawnMultiplier = 1.2f;
    } else {
        state_.enemySpawnMultiplier = 1.0f;
    }

    // Stealth bonus (easier to hide at night)
    if (state_.isNight) {
        state_.stealthBonus = 0.5f;  // 50% harder for enemies to detect
    } else if (isDawn() || isDusk()) {
        state_.stealthBonus = 0.3f;  // 30% harder
    } else {
        state_.stealthBonus = 0.0f;  // No bonus during day
    }
}

// ============================================================================
// Helper Methods
// ============================================================================

Engine::vec3 DayNightCycleSystem::interpolateColor(const Engine::vec3& a, const Engine::vec3& b, float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    return Engine::vec3(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

float DayNightCycleSystem::smoothCurve(float t) const {
    // Smoothstep function for smoother transitions
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace CatGame

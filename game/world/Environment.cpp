#include "Environment.hpp"
#include "../../engine/math/Math.hpp"
#include <cmath>
#include <algorithm>

namespace CatGame {

// ============================================================================
// Construction / Destruction
// ============================================================================

Environment::Environment()
    : m_timeOfDay(12.0f)
    , m_dayNightCycleEnabled(false)
    , m_dayLengthSeconds(600.0f)
    , m_ambientColor(0.3f, 0.3f, 0.4f)
    , m_fogEnabled(true)
    , m_currentWeather(Weather::Clear)
    , m_targetWeather(Weather::Clear)
    , m_weatherTransitionTime(0.0f)
    , m_weatherTransitionDuration(0.0f)
    , m_windTime(0.0f)
{
    updateSunLight();
    updateSkyColors();
}

Environment::~Environment() = default;

Environment::Environment(Environment&& other) noexcept
    : m_timeOfDay(other.m_timeOfDay)
    , m_dayNightCycleEnabled(other.m_dayNightCycleEnabled)
    , m_dayLengthSeconds(other.m_dayLengthSeconds)
    , m_sunLight(other.m_sunLight)
    , m_ambientColor(other.m_ambientColor)
    , m_sky(other.m_sky)
    , m_fog(other.m_fog)
    , m_fogEnabled(other.m_fogEnabled)
    , m_currentWeather(other.m_currentWeather)
    , m_targetWeather(other.m_targetWeather)
    , m_weatherTransitionTime(other.m_weatherTransitionTime)
    , m_weatherTransitionDuration(other.m_weatherTransitionDuration)
    , m_wind(other.m_wind)
    , m_windTime(other.m_windTime)
{
}

Environment& Environment::operator=(Environment&& other) noexcept {
    if (this != &other) {
        m_timeOfDay = other.m_timeOfDay;
        m_dayNightCycleEnabled = other.m_dayNightCycleEnabled;
        m_dayLengthSeconds = other.m_dayLengthSeconds;
        m_sunLight = other.m_sunLight;
        m_ambientColor = other.m_ambientColor;
        m_sky = other.m_sky;
        m_fog = other.m_fog;
        m_fogEnabled = other.m_fogEnabled;
        m_currentWeather = other.m_currentWeather;
        m_targetWeather = other.m_targetWeather;
        m_weatherTransitionTime = other.m_weatherTransitionTime;
        m_weatherTransitionDuration = other.m_weatherTransitionDuration;
        m_wind = other.m_wind;
        m_windTime = other.m_windTime;
    }
    return *this;
}

// ============================================================================
// Update
// ============================================================================

void Environment::update(float deltaTime) {
    // Update time of day
    if (m_dayNightCycleEnabled) {
        updateTimeOfDay(deltaTime);
    }

    // Update weather transition
    if (m_weatherTransitionTime > 0.0f) {
        updateWeatherTransition(deltaTime);
    }

    // Update wind time
    m_windTime += deltaTime;
}

void Environment::updateTimeOfDay(float deltaTime) {
    // Advance time
    float hoursPerSecond = 24.0f / m_dayLengthSeconds;
    m_timeOfDay += hoursPerSecond * deltaTime;

    // Wrap around 24 hours
    while (m_timeOfDay >= 24.0f) {
        m_timeOfDay -= 24.0f;
    }

    // Update sun light based on time
    updateSunLight();
    updateSkyColors();
}

void Environment::updateSunLight() {
    // Calculate sun position based on time of day
    // 0:00 = midnight, 6:00 = sunrise, 12:00 = noon, 18:00 = sunset

    // Convert time to angle (0-360 degrees)
    float angle = (m_timeOfDay / 24.0f) * 360.0f;
    float angleRad = angle * Engine::Math::PI / 180.0f;

    // Sun direction (rotates around X axis)
    // At noon (12:00), sun is directly overhead (0, -1, 0)
    // Offset by 90 degrees so midnight is below horizon
    float sunAngle = angleRad - Engine::Math::PI * 0.5f;

    m_sunLight.direction = Engine::vec3(
        0.0f,
        -std::sin(sunAngle),
        -std::cos(sunAngle)
    ).normalized();

    // Sun color and intensity based on time
    float sunrise = 6.0f;
    float sunset = 18.0f;
    float noon = 12.0f;

    if (m_timeOfDay >= sunrise && m_timeOfDay <= sunset) {
        // Daytime
        float dayProgress = (m_timeOfDay - sunrise) / (sunset - sunrise);

        // Intensity: dim at sunrise/sunset, bright at noon
        float noonFactor = 1.0f - std::abs(dayProgress - 0.5f) * 2.0f;
        m_sunLight.intensity = 0.3f + noonFactor * 0.7f;

        // Color: warm at sunrise/sunset, white at noon
        float warmth = std::abs(dayProgress - 0.5f) * 2.0f;  // 0 at noon, 1 at edges
        m_sunLight.color = Engine::vec3::lerp(
            Engine::vec3(1.0f, 1.0f, 1.0f),      // Noon: white
            Engine::vec3(1.0f, 0.7f, 0.5f),      // Sunrise/sunset: warm orange
            warmth
        );

        // Ambient light
        m_ambientColor = Engine::vec3::lerp(
            Engine::vec3(0.4f, 0.4f, 0.5f),      // Noon: bright blue-ish
            Engine::vec3(0.3f, 0.2f, 0.15f),     // Sunrise/sunset: warm dim
            warmth
        );
    } else {
        // Nighttime
        m_sunLight.intensity = 0.05f;
        m_sunLight.color = Engine::vec3(0.3f, 0.3f, 0.5f);  // Moonlight: blue-ish
        m_ambientColor = Engine::vec3(0.05f, 0.05f, 0.1f);  // Dark blue ambient
    }
}

void Environment::updateSkyColors() {
    // Update sky colors based on time of day
    float sunrise = 6.0f;
    float sunset = 18.0f;

    if (m_timeOfDay >= sunrise && m_timeOfDay <= sunset) {
        // Daytime sky
        float dayProgress = (m_timeOfDay - sunrise) / (sunset - sunrise);
        float warmth = std::abs(dayProgress - 0.5f) * 2.0f;

        m_sky.zenithColor = Engine::vec3::lerp(
            Engine::vec3(0.2f, 0.4f, 0.8f),      // Noon: bright blue
            Engine::vec3(0.6f, 0.4f, 0.3f),      // Sunrise/sunset: warm
            warmth
        );

        m_sky.horizonColor = Engine::vec3::lerp(
            Engine::vec3(0.6f, 0.7f, 0.9f),      // Noon: light blue
            Engine::vec3(0.9f, 0.6f, 0.4f),      // Sunrise/sunset: orange
            warmth
        );
    } else {
        // Nighttime sky
        m_sky.zenithColor = Engine::vec3(0.01f, 0.01f, 0.05f);      // Dark blue
        m_sky.horizonColor = Engine::vec3(0.05f, 0.05f, 0.1f);      // Slightly lighter
    }
}

void Environment::updateWeatherTransition(float deltaTime) {
    m_weatherTransitionTime -= deltaTime;

    if (m_weatherTransitionTime <= 0.0f) {
        // Transition complete
        m_currentWeather = m_targetWeather;
        m_weatherTransitionTime = 0.0f;
    }

    // Update fog based on weather
    float transitionFactor = 1.0f - (m_weatherTransitionTime / m_weatherTransitionDuration);

    switch (m_targetWeather) {
        case Weather::Clear:
            m_fog.density = Engine::Math::lerp(m_fog.density, 0.0003f, transitionFactor);
            m_fog.color = Engine::vec3::lerp(m_fog.color, Engine::vec3(0.7f, 0.8f, 0.9f), transitionFactor);
            m_wind.strength = Engine::Math::lerp(m_wind.strength, 0.2f, transitionFactor);
            break;

        case Weather::Cloudy:
            m_fog.density = Engine::Math::lerp(m_fog.density, 0.0005f, transitionFactor);
            m_fog.color = Engine::vec3::lerp(m_fog.color, Engine::vec3(0.6f, 0.6f, 0.65f), transitionFactor);
            m_wind.strength = Engine::Math::lerp(m_wind.strength, 0.4f, transitionFactor);
            break;

        case Weather::Rain:
            m_fog.density = Engine::Math::lerp(m_fog.density, 0.001f, transitionFactor);
            m_fog.color = Engine::vec3::lerp(m_fog.color, Engine::vec3(0.5f, 0.5f, 0.55f), transitionFactor);
            m_wind.strength = Engine::Math::lerp(m_wind.strength, 0.6f, transitionFactor);
            break;

        case Weather::Storm:
            m_fog.density = Engine::Math::lerp(m_fog.density, 0.002f, transitionFactor);
            m_fog.color = Engine::vec3::lerp(m_fog.color, Engine::vec3(0.3f, 0.3f, 0.35f), transitionFactor);
            m_wind.strength = Engine::Math::lerp(m_wind.strength, 0.9f, transitionFactor);
            break;
    }
}

// ============================================================================
// Time of Day
// ============================================================================

void Environment::setTimeOfDay(float hours) {
    m_timeOfDay = std::fmod(hours, 24.0f);
    if (m_timeOfDay < 0.0f) {
        m_timeOfDay += 24.0f;
    }

    updateSunLight();
    updateSkyColors();
}

void Environment::setDayNightCycle(bool enabled, float dayLengthSeconds) {
    m_dayNightCycleEnabled = enabled;
    m_dayLengthSeconds = std::max(1.0f, dayLengthSeconds);
}

// ============================================================================
// Weather
// ============================================================================

void Environment::setWeather(Weather weather, float transitionTime) {
    if (weather == m_currentWeather) {
        return;
    }

    m_targetWeather = weather;
    m_weatherTransitionTime = transitionTime;
    m_weatherTransitionDuration = transitionTime;
}

// ============================================================================
// Wind
// ============================================================================

Engine::vec3 Environment::sampleWind(const Engine::vec3& position, float time) const {
    // Base wind direction and strength
    Engine::vec3 windVelocity = m_wind.direction * m_wind.strength;

    // Add gust variation using noise-like function
    if (m_wind.gustiness > 0.0f) {
        // Simple pseudo-random variation based on position and time
        float phaseX = std::sin(position.x * 0.05f + time * 0.5f);
        float phaseZ = std::cos(position.z * 0.05f + time * 0.7f);
        float phaseT = std::sin(time * 1.2f);

        float gustFactor = (phaseX * 0.3f + phaseZ * 0.3f + phaseT * 0.4f) * m_wind.gustiness;
        windVelocity *= (1.0f + gustFactor);
    }

    return windVelocity;
}

} // namespace CatGame

#pragma once

#include "../../engine/math/Vector.hpp"
#include <cstdint>

namespace CatGame {

/**
 * @brief Environment and atmosphere system
 *
 * Manages skybox, lighting, fog, and weather.
 * Supports day/night cycle and dynamic weather.
 */
class Environment {
public:
    /**
     * @brief Weather state
     */
    enum class Weather : uint8_t {
        Clear = 0,
        Cloudy = 1,
        Rain = 2,
        Storm = 3
    };

    /**
     * @brief Directional light (sun/moon)
     */
    struct DirectionalLight {
        Engine::vec3 direction;     // Light direction (normalized)
        Engine::vec3 color;         // Light color (RGB, HDR)
        float intensity;            // Light intensity multiplier

        DirectionalLight()
            : direction(0.0f, -1.0f, 0.0f)
            , color(1.0f, 1.0f, 1.0f)
            , intensity(1.0f) {}
    };

    /**
     * @brief Fog configuration
     */
    struct Fog {
        Engine::vec3 color;         // Fog color (RGB)
        float density;              // Exponential fog density
        float start;                // Linear fog start distance
        float end;                  // Linear fog end distance
        bool exponential;           // true = exponential fog, false = linear

        Fog()
            : color(0.7f, 0.8f, 0.9f)
            , density(0.0005f)
            , start(50.0f)
            , end(300.0f)
            , exponential(true) {}
    };

    /**
     * @brief Skybox/atmosphere configuration
     */
    struct Sky {
        Engine::vec3 zenithColor;       // Sky color at zenith (top)
        Engine::vec3 horizonColor;      // Sky color at horizon
        Engine::vec3 groundColor;       // Ground reflection color
        float turbidity;                // Atmospheric turbidity (haze)
        float rayleighCoefficient;      // Rayleigh scattering coefficient
        float mieCoefficient;           // Mie scattering coefficient

        Sky()
            : zenithColor(0.2f, 0.4f, 0.8f)
            , horizonColor(0.6f, 0.7f, 0.8f)
            , groundColor(0.3f, 0.25f, 0.2f)
            , turbidity(2.0f)
            , rayleighCoefficient(1.0f)
            , mieCoefficient(0.005f) {}
    };

    /**
     * @brief Wind parameters
     */
    struct Wind {
        Engine::vec3 direction;     // Wind direction (normalized)
        float strength;             // Wind strength (0-1)
        float gustiness;            // Gust variation (0-1)

        Wind()
            : direction(1.0f, 0.0f, 0.0f)
            , strength(0.3f)
            , gustiness(0.5f) {}
    };

    /**
     * @brief Construct environment
     */
    Environment();

    /**
     * @brief Destructor
     */
    ~Environment();

    // Non-copyable, movable
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&&) noexcept;
    Environment& operator=(Environment&&) noexcept;

    /**
     * @brief Update environment
     *
     * Updates time of day, weather transitions, wind.
     *
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    // ========================================================================
    // Time of Day
    // ========================================================================

    /**
     * @brief Set time of day (0-24 hours)
     *
     * @param hours Time in 24-hour format (0.0 = midnight, 12.0 = noon)
     */
    void setTimeOfDay(float hours);

    /**
     * @brief Get time of day
     *
     * @return Time in hours (0-24)
     */
    float getTimeOfDay() const { return m_timeOfDay; }

    /**
     * @brief Enable/disable day/night cycle
     *
     * @param enabled If true, time advances automatically
     * @param dayLengthSeconds Length of full day cycle in real-time seconds
     */
    void setDayNightCycle(bool enabled, float dayLengthSeconds = 600.0f);

    /**
     * @brief Check if day/night cycle is enabled
     */
    bool isDayNightCycleEnabled() const { return m_dayNightCycleEnabled; }

    // ========================================================================
    // Lighting
    // ========================================================================

    /**
     * @brief Get sun directional light
     */
    const DirectionalLight& getSunLight() const { return m_sunLight; }

    /**
     * @brief Get ambient light color
     */
    const Engine::vec3& getAmbientColor() const { return m_ambientColor; }

    /**
     * @brief Set ambient light color
     *
     * @param color RGB ambient color
     */
    void setAmbientColor(const Engine::vec3& color) { m_ambientColor = color; }

    // ========================================================================
    // Sky and Atmosphere
    // ========================================================================

    /**
     * @brief Get sky configuration
     */
    const Sky& getSky() const { return m_sky; }

    /**
     * @brief Set sky configuration
     *
     * @param sky Sky parameters
     */
    void setSky(const Sky& sky) { m_sky = sky; }

    // ========================================================================
    // Fog
    // ========================================================================

    /**
     * @brief Get fog configuration
     */
    const Fog& getFog() const { return m_fog; }

    /**
     * @brief Set fog configuration
     *
     * @param fog Fog parameters
     */
    void setFog(const Fog& fog) { m_fog = fog; }

    /**
     * @brief Enable/disable fog
     *
     * @param enabled Fog state
     */
    void setFogEnabled(bool enabled) { m_fogEnabled = enabled; }

    /**
     * @brief Check if fog is enabled
     */
    bool isFogEnabled() const { return m_fogEnabled; }

    // ========================================================================
    // Weather
    // ========================================================================

    /**
     * @brief Set weather
     *
     * @param weather New weather state
     * @param transitionTime Time to transition to new weather (seconds)
     */
    void setWeather(Weather weather, float transitionTime = 2.0f);

    /**
     * @brief Get current weather
     */
    Weather getWeather() const { return m_currentWeather; }

    /**
     * @brief Check if weather is transitioning
     */
    bool isWeatherTransitioning() const { return m_weatherTransitionTime > 0.0f; }

    // ========================================================================
    // Wind
    // ========================================================================

    /**
     * @brief Get wind parameters
     */
    const Wind& getWind() const { return m_wind; }

    /**
     * @brief Set wind parameters
     *
     * @param wind Wind configuration
     */
    void setWind(const Wind& wind) { m_wind = wind; }

    /**
     * @brief Sample wind at position
     *
     * Returns wind vector with gust variation.
     *
     * @param position World position
     * @param time Current time for animation
     * @return Wind velocity vector
     */
    Engine::vec3 sampleWind(const Engine::vec3& position, float time) const;

private:
    void updateTimeOfDay(float deltaTime);
    void updateSunLight();
    void updateWeatherTransition(float deltaTime);
    void updateSkyColors();

    // Time of day
    float m_timeOfDay;              // 0-24 hours
    bool m_dayNightCycleEnabled;
    float m_dayLengthSeconds;

    // Lighting
    DirectionalLight m_sunLight;
    Engine::vec3 m_ambientColor;

    // Sky
    Sky m_sky;

    // Fog
    Fog m_fog;
    bool m_fogEnabled;

    // Weather
    Weather m_currentWeather;
    Weather m_targetWeather;
    float m_weatherTransitionTime;
    float m_weatherTransitionDuration;

    // Wind
    Wind m_wind;
    float m_windTime;               // Accumulated time for wind animation
};

} // namespace CatGame

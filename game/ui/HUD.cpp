#include "HUD.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace Game {

HUD::HUD(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

HUD::~HUD() {
    shutdown();
}

bool HUD::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("HUD already initialized");
        return true;
    }

    // Initialize with default values
    m_currentHealth = 100.0f;
    m_maxHealth = 100.0f;
    m_currentWave = 1;
    m_remainingEnemies = 0;
    m_totalEnemies = 0;
    m_score = 0;
    m_combo = 0;

    m_initialized = true;
    Engine::Logger::info("HUD initialized successfully");
    return true;
}

void HUD::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_damageIndicators.clear();
    m_damageNumbers.clear();

    m_initialized = false;
    Engine::Logger::info("HUD shutdown");
}

void HUD::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update damage indicators
    updateDamageIndicators(deltaTime);

    // Update damage numbers
    updateDamageNumbers(deltaTime);

    // Update low health warning pulse
    if (m_lowHealthWarning) {
        m_lowHealthPulse += deltaTime * 4.0f; // Pulse speed
    }

    // Update combo display timer
    if (m_combo > 0) {
        m_comboDisplayTime += deltaTime;
    } else {
        m_comboDisplayTime = 0.0f;
    }
}

void HUD::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized) {
        return;
    }

    // Render low health warning first (background layer)
    if (m_lowHealthWarning) {
        renderLowHealthWarning(renderer);
    }

    // Render damage indicators
    renderDamageIndicators(renderer);

    // Render main HUD elements
    renderHealthBar(renderer);
    renderWaveCounter(renderer);
    renderEnemyCounter(renderer);
    renderScore(renderer);

    // Render crosshair (center of screen)
    if (m_showCrosshair) {
        renderCrosshair(renderer);
    }

    // Render damage numbers
    renderDamageNumbers(renderer);

    // Render FPS counter (if enabled)
    if (m_showFPS) {
        renderFPS(renderer);
    }
}

// ============================================================================
// Data Setters
// ============================================================================

void HUD::setHealth(float current, float max) {
    m_currentHealth = current;
    m_maxHealth = max;

    // Auto-enable low health warning below 30%
    float healthPercent = current / max;
    if (healthPercent < 0.3f && healthPercent > 0.0f) {
        setLowHealthWarning(true);
    } else {
        setLowHealthWarning(false);
    }
}

void HUD::setWave(uint32_t wave) {
    m_currentWave = wave;
}

void HUD::setEnemyCount(uint32_t remaining, uint32_t total) {
    m_remainingEnemies = remaining;
    m_totalEnemies = total;
}

void HUD::setScore(uint32_t score) {
    m_score = score;
}

void HUD::setCombo(uint32_t combo) {
    m_combo = combo;
    if (combo > 0) {
        m_comboDisplayTime = 0.0f; // Reset display timer
    }
}

// ============================================================================
// Visual Effects
// ============================================================================

void HUD::showDamageIndicator(const std::array<float, 2>& direction, float intensity) {
    DamageIndicator indicator;
    indicator.direction = direction;
    indicator.intensity = intensity;
    indicator.lifetime = 0.0f;

    m_damageIndicators.push_back(indicator);
}

void HUD::showDamageNumber(float damage,
                           const std::array<float, 2>& screenPosition,
                           bool isCritical) {
    DamageNumber number;
    number.amount = damage;
    number.position = screenPosition;
    number.velocity = {0.0f, -50.0f}; // Float upward
    number.lifetime = 0.0f;
    number.isCritical = isCritical;
    number.isHeal = false;

    m_damageNumbers.push_back(number);
}

void HUD::showHealNumber(float amount, const std::array<float, 2>& screenPosition) {
    DamageNumber number;
    number.amount = amount;
    number.position = screenPosition;
    number.velocity = {0.0f, -50.0f}; // Float upward
    number.lifetime = 0.0f;
    number.isCritical = false;
    number.isHeal = true;

    m_damageNumbers.push_back(number);
}

void HUD::setLowHealthWarning(bool enable) {
    m_lowHealthWarning = enable;
    if (enable) {
        m_lowHealthPulse = 0.0f;
    }
}

void HUD::setFPS(float fps) {
    m_fps = fps;
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void HUD::renderHealthBar(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    // For now, this is a placeholder showing the logic

    /*
    float healthPercent = m_currentHealth / m_maxHealth;

    // Health bar background (top-left)
    vec2 barPos = {20, 20};
    vec2 barSize = {200, 30};

    renderer.drawRect(barPos, barSize, {0.2f, 0.2f, 0.2f, 0.8f});

    // Health bar fill
    vec2 fillSize = {barSize.x * healthPercent, barSize.y};
    vec4 healthColor = healthPercent > 0.5f ?
        vec4{0.0f, 0.8f, 0.0f, 1.0f} :  // Green
        vec4{0.8f, 0.0f, 0.0f, 1.0f};   // Red

    renderer.drawRect(barPos, fillSize, healthColor);

    // Health text
    std::string healthText = std::to_string((int)m_currentHealth) + " / " +
                             std::to_string((int)m_maxHealth);
    renderer.drawText(healthText, barPos + vec2{5, 5}, {1, 1, 1, 1});
    */
}

void HUD::renderWaveCounter(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    std::string waveText = "WAVE " + std::to_string(m_currentWave);
    vec2 position = {screenWidth / 2 - 50, 20}; // Top center
    renderer.drawText(waveText, position, {1, 1, 0, 1}, 24); // Yellow, size 24
    */
}

void HUD::renderEnemyCounter(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    std::string enemyText = "Enemies: " + std::to_string(m_remainingEnemies) +
                           " / " + std::to_string(m_totalEnemies);
    vec2 position = {screenWidth - 200, 20}; // Top right
    renderer.drawText(enemyText, position, {1, 0.5f, 0, 1}, 18); // Orange
    */
}

void HUD::renderScore(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    std::string scoreText = "Score: " + std::to_string(m_score);
    vec2 position = {20, 60}; // Below health bar
    renderer.drawText(scoreText, position, {1, 1, 1, 1}, 18);

    // Render combo if active
    if (m_combo > 1 && m_comboDisplayTime < m_comboFadeTime) {
        float fadeAlpha = 1.0f - (m_comboDisplayTime / m_comboFadeTime);
        std::string comboText = "COMBO x" + std::to_string(m_combo);
        vec2 comboPos = {20, 90};
        renderer.drawText(comboText, comboPos, {1, 0.8f, 0, fadeAlpha}, 22);
    }
    */
}

void HUD::renderCrosshair(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    vec2 center = {screenWidth / 2, screenHeight / 2};
    float size = 10.0f;
    float thickness = 2.0f;
    vec4 color = {1, 1, 1, 0.8f};

    // Draw cross
    renderer.drawLine({center.x - size, center.y},
                     {center.x + size, center.y},
                     color, thickness);
    renderer.drawLine({center.x, center.y - size},
                     {center.x, center.y + size},
                     color, thickness);
    */
}

void HUD::renderDamageIndicators(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    for (const auto& indicator : m_damageIndicators) {
        float alpha = 1.0f - (indicator.lifetime / indicator.maxLifetime);
        alpha *= indicator.intensity;

        // Draw directional damage indicator at screen edge
        vec2 edgePos = calculateEdgePosition(indicator.direction);
        renderer.drawTriangle(edgePos, indicator.direction,
                            {1, 0, 0, alpha}, 30.0f);
    }
    */
}

void HUD::renderDamageNumbers(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    for (const auto& number : m_damageNumbers) {
        float alpha = 1.0f - (number.lifetime / number.maxLifetime);

        vec4 color;
        if (number.isHeal) {
            color = {0, 1, 0, alpha}; // Green for heal
        } else if (number.isCritical) {
            color = {1, 0.5f, 0, alpha}; // Orange for critical
        } else {
            color = {1, 1, 1, alpha}; // White for normal damage
        }

        std::string text = number.isCritical ?
            std::to_string((int)number.amount) + "!" :
            std::to_string((int)number.amount);

        int fontSize = number.isCritical ? 28 : 20;
        renderer.drawText(text, number.position, color, fontSize);
    }
    */
}

void HUD::renderLowHealthWarning(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    // Pulsing red glow at screen edges
    float pulseIntensity = (std::sin(m_lowHealthPulse) + 1.0f) * 0.5f;
    float alpha = 0.3f * pulseIntensity;

    vec4 redGlow = {1, 0, 0, alpha};

    // Draw vignette effect
    renderer.drawVignette({screenWidth / 2, screenHeight / 2},
                         screenWidth, redGlow);
    */
}

void HUD::renderFPS(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    std::string fpsText = "FPS: " + std::to_string((int)m_fps);
    vec2 position = {screenWidth - 100, screenHeight - 30}; // Bottom right
    renderer.drawText(fpsText, position, {0, 1, 0, 1}, 16); // Green
    */
}

// ============================================================================
// Private Update Methods
// ============================================================================

void HUD::updateDamageIndicators(float deltaTime) {
    // Update and remove expired indicators
    m_damageIndicators.erase(
        std::remove_if(m_damageIndicators.begin(), m_damageIndicators.end(),
            [deltaTime](DamageIndicator& indicator) {
                indicator.lifetime += deltaTime;
                return indicator.lifetime >= indicator.maxLifetime;
            }),
        m_damageIndicators.end()
    );
}

void HUD::updateDamageNumbers(float deltaTime) {
    // Update positions and remove expired numbers
    for (auto& number : m_damageNumbers) {
        number.lifetime += deltaTime;
        number.position[0] += number.velocity[0] * deltaTime;
        number.position[1] += number.velocity[1] * deltaTime;

        // Slow down over time
        number.velocity[1] *= 0.95f;
    }

    m_damageNumbers.erase(
        std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
            [](const DamageNumber& number) {
                return number.lifetime >= number.maxLifetime;
            }),
        m_damageNumbers.end()
    );
}

} // namespace Game

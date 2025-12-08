#include "WavePopup.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace Game {

WavePopup::WavePopup(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

WavePopup::~WavePopup() {
    shutdown();
}

bool WavePopup::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("WavePopup already initialized");
        return true;
    }

    m_state = PopupState::Hidden;
    m_isVisible = false;

    m_initialized = true;
    Engine::Logger::info("WavePopup initialized successfully");
    return true;
}

void WavePopup::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_initialized = false;
    Engine::Logger::info("WavePopup shutdown");
}

void WavePopup::update(float deltaTime) {
    if (!m_initialized || !m_isVisible) {
        return;
    }

    updateAnimation(deltaTime);

    // Handle state-specific updates
    switch (m_state) {
        case PopupState::WaveComplete:
            m_displayTimer += deltaTime;

            // Auto-dismiss after delay
            if (m_displayTimer >= m_autoDismissDelay) {
                dismiss();
            }
            break;

        case PopupState::Countdown:
            m_countdownTimer += deltaTime;

            // Auto-dismiss when countdown reaches zero
            if (m_countdownTimer >= m_countdownDuration) {
                dismiss();
            }
            break;

        default:
            break;
    }
}

void WavePopup::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized || !m_isVisible) {
        return;
    }

    switch (m_state) {
        case PopupState::WaveComplete:
            renderWaveComplete(renderer);
            break;

        case PopupState::Countdown:
            renderCountdown(renderer);
            break;

        default:
            break;
    }
}

void WavePopup::handleInput() {
    if (!m_initialized || !m_isVisible) {
        return;
    }

    // Dismiss on any key press (for wave complete)
    if (m_state == PopupState::WaveComplete) {
        if (m_input.isKeyPressed(Engine::Input::Key::Space) ||
            m_input.isKeyPressed(Engine::Input::Key::Enter) ||
            m_input.isMouseButtonPressed(Engine::Input::MouseButton::Left)) {
            m_audio.playMenuClick();
            dismiss();
        }
    }

    // Countdown cannot be manually dismissed (or can it? Up to design choice)
    // For now, let users skip countdown with Space/Enter
    if (m_state == PopupState::Countdown) {
        if (m_input.isKeyPressed(Engine::Input::Key::Space) ||
            m_input.isKeyPressed(Engine::Input::Key::Enter)) {
            m_audio.playMenuClick();
            dismiss();
        }
    }
}

// ============================================================================
// Display Control
// ============================================================================

void WavePopup::showWaveComplete(uint32_t waveNumber,
                                 uint32_t enemiesKilled,
                                 float timeTaken,
                                 uint32_t nextWaveEnemyCount) {
    m_state = PopupState::WaveComplete;
    m_isVisible = true;

    m_completedWave = waveNumber;
    m_enemiesKilled = enemiesKilled;
    m_timeTaken = timeTaken;
    m_nextWaveEnemyCount = nextWaveEnemyCount;

    m_displayTimer = 0.0f;
    m_animationTimer = 0.0f;
    m_fadeAlpha = 0.0f;

    // Wave complete sound is played by caller
    Engine::Logger::info("Wave " + std::to_string(waveNumber) + " complete popup shown");
}

void WavePopup::showWaveStart(uint32_t waveNumber, uint32_t enemyCount) {
    m_state = PopupState::Countdown;
    m_isVisible = true;

    m_startingWave = waveNumber;
    m_waveEnemyCount = enemyCount;

    m_countdownTimer = 0.0f;
    m_animationTimer = 0.0f;
    m_fadeAlpha = 0.0f;

    m_audio.playWaveStart();
    Engine::Logger::info("Wave " + std::to_string(waveNumber) + " countdown started");
}

void WavePopup::dismiss() {
    if (!m_isVisible) {
        return;
    }

    m_isVisible = false;
    m_state = PopupState::Hidden;

    if (m_dismissCallback) {
        m_dismissCallback();
    }

    Engine::Logger::info("Wave popup dismissed");
}

// ============================================================================
// Private Methods
// ============================================================================

void WavePopup::renderWaveComplete(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    // Semi-transparent overlay
    renderer.drawRect({0, 0}, {screenWidth, screenHeight},
                     {0, 0, 0, 0.6f * m_fadeAlpha});

    // Main title with animation (scale effect)
    float scale = 1.0f + std::sin(m_animationTimer * 3.0f) * 0.1f;
    vec2 titlePos = {screenWidth / 2 - 150, screenHeight / 2 - 100};

    std::string titleText = "WAVE " + std::to_string(m_completedWave) + " COMPLETE!";
    renderer.drawText(titleText, titlePos, {1, 0.8f, 0, m_fadeAlpha}, 36 * scale);

    // Stats display
    vec2 statsPos = {screenWidth / 2 - 100, screenHeight / 2};

    std::string killsText = "Enemies Killed: " + std::to_string(m_enemiesKilled);
    renderer.drawText(killsText, statsPos, {1, 1, 1, m_fadeAlpha}, 20);

    vec2 timePos = {statsPos[0], statsPos[1] + 30};
    std::string timeText = "Time: " + std::to_string((int)m_timeTaken) + "s";
    renderer.drawText(timeText, timePos, {1, 1, 1, m_fadeAlpha}, 20);

    // Next wave preview
    vec2 nextWavePos = {statsPos[0], statsPos[1] + 80};
    std::string nextWaveText = "Next Wave: " + std::to_string(m_nextWaveEnemyCount) + " Enemies";
    renderer.drawText(nextWaveText, nextWavePos, {0.8f, 0.8f, 1, m_fadeAlpha}, 22);

    // Prompt to continue
    vec2 promptPos = {screenWidth / 2 - 120, screenHeight / 2 + 150};
    float promptAlpha = (std::sin(m_animationTimer * 4.0f) + 1.0f) * 0.5f * m_fadeAlpha;
    renderer.drawText("Press SPACE to continue", promptPos,
                     {1, 1, 1, promptAlpha}, 18);
    */
}

void WavePopup::renderCountdown(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    // Semi-transparent overlay
    renderer.drawRect({0, 0}, {screenWidth, screenHeight},
                     {0, 0, 0, 0.5f * m_fadeAlpha});

    // Wave number display
    vec2 wavePos = {screenWidth / 2 - 100, screenHeight / 2 - 80};
    std::string waveText = "WAVE " + std::to_string(m_startingWave);
    renderer.drawText(waveText, wavePos, {1, 0.8f, 0, m_fadeAlpha}, 42);

    // Enemy count
    vec2 enemyPos = {screenWidth / 2 - 80, screenHeight / 2 - 20};
    std::string enemyText = std::to_string(m_waveEnemyCount) + " Enemies Incoming";
    renderer.drawText(enemyText, enemyPos, {1, 0.5f, 0, m_fadeAlpha}, 24);

    // Countdown timer
    float remainingTime = m_countdownDuration - m_countdownTimer;
    int countdown = static_cast<int>(std::ceil(remainingTime));

    vec2 countdownPos = {screenWidth / 2 - 30, screenHeight / 2 + 40};
    std::string countdownText = std::to_string(countdown);

    // Scale countdown number with pulsing effect
    float pulsePhase = remainingTime - std::floor(remainingTime);
    float countdownScale = 1.5f + (1.0f - pulsePhase) * 0.5f;

    vec4 countdownColor = {1, 1, 1, m_fadeAlpha};
    if (countdown <= 3) {
        // Flash red for last 3 seconds
        countdownColor = {1, 0.2f, 0.2f, m_fadeAlpha};
    }

    renderer.drawText(countdownText, countdownPos, countdownColor,
                     static_cast<int>(48 * countdownScale));

    // Hint text
    vec2 hintPos = {screenWidth / 2 - 100, screenHeight / 2 + 120};
    renderer.drawText("Press SPACE to skip", hintPos,
                     {0.6f, 0.6f, 0.6f, m_fadeAlpha * 0.7f}, 16);
    */
}

void WavePopup::updateAnimation(float deltaTime) {
    m_animationTimer += deltaTime;

    // Fade in effect
    float fadeInDuration = 0.3f;
    if (m_displayTimer < fadeInDuration || m_countdownTimer < fadeInDuration) {
        float currentTimer = (m_state == PopupState::WaveComplete) ?
                            m_displayTimer : m_countdownTimer;
        m_fadeAlpha = std::min(currentTimer / fadeInDuration, 1.0f);
    } else {
        m_fadeAlpha = 1.0f;
    }
}

} // namespace Game

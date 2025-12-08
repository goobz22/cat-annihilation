#ifndef GAME_UI_WAVE_POPUP_HPP
#define GAME_UI_WAVE_POPUP_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include <functional>
#include <cstdint>
#include <string>

namespace Game {

class GameAudio;

/**
 * @brief Wave completion popup
 *
 * Displays "Wave X Complete!" with stats and next wave preview.
 * Auto-dismisses after delay or on user input.
 */
class WavePopup {
public:
    /**
     * @brief Callback when popup is dismissed
     */
    using DismissCallback = std::function<void()>;

    explicit WavePopup(Engine::Input& input, GameAudio& audio);
    ~WavePopup();

    /**
     * @brief Initialize wave popup
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown wave popup
     */
    void shutdown();

    /**
     * @brief Update wave popup (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render wave popup
     * @param renderer Renderer to use for drawing
     */
    void render(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Handle input
     */
    void handleInput();

    // ========================================================================
    // Display Control
    // ========================================================================

    /**
     * @brief Show wave complete popup
     * @param waveNumber Wave that was completed
     * @param enemiesKilled Number of enemies killed
     * @param timeTaken Time taken to complete wave (seconds)
     * @param nextWaveEnemyCount Number of enemies in next wave
     */
    void showWaveComplete(uint32_t waveNumber,
                          uint32_t enemiesKilled,
                          float timeTaken,
                          uint32_t nextWaveEnemyCount);

    /**
     * @brief Show wave start countdown
     * @param waveNumber Wave that is starting
     * @param enemyCount Number of enemies in wave
     */
    void showWaveStart(uint32_t waveNumber, uint32_t enemyCount);

    /**
     * @brief Dismiss popup immediately
     */
    void dismiss();

    /**
     * @brief Check if popup is currently visible
     */
    bool isVisible() const { return m_isVisible; }

    /**
     * @brief Set dismiss callback
     */
    void setDismissCallback(DismissCallback callback) {
        m_dismissCallback = callback;
    }

    /**
     * @brief Set auto-dismiss delay (seconds)
     */
    void setAutoDismissDelay(float delay) { m_autoDismissDelay = delay; }

    /**
     * @brief Set countdown duration (seconds)
     */
    void setCountdownDuration(float duration) { m_countdownDuration = duration; }

private:
    /**
     * @brief Popup state
     */
    enum class PopupState {
        Hidden,
        WaveComplete,
        Countdown
    };

    /**
     * @brief Render wave complete screen
     */
    void renderWaveComplete(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render countdown screen
     */
    void renderCountdown(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Update animation timers
     */
    void updateAnimation(float deltaTime);

    Engine::Input& m_input;
    GameAudio& m_audio;

    // State
    PopupState m_state = PopupState::Hidden;
    bool m_isVisible = false;

    // Wave complete data
    uint32_t m_completedWave = 0;
    uint32_t m_enemiesKilled = 0;
    float m_timeTaken = 0.0f;
    uint32_t m_nextWaveEnemyCount = 0;

    // Countdown data
    uint32_t m_startingWave = 0;
    uint32_t m_waveEnemyCount = 0;
    float m_countdownTimer = 0.0f;
    float m_countdownDuration = 10.0f;

    // Timing
    float m_displayTimer = 0.0f;
    float m_autoDismissDelay = 5.0f;

    // Animation
    float m_animationTimer = 0.0f;
    float m_fadeAlpha = 0.0f;

    // Callback
    DismissCallback m_dismissCallback;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_WAVE_POPUP_HPP

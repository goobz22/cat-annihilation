#ifndef GAME_UI_GAME_UI_HPP
#define GAME_UI_GAME_UI_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include <memory>
#include <vector>
#include <string>

namespace Game {

// Forward declarations
class HUD;
class MainMenu;
class PauseMenu;
class WavePopup;
class GameAudio;

/**
 * @brief Game state for UI management
 */
enum class GameState {
    MainMenu,
    Playing,
    Paused,
    WaveComplete,
    GameOver,
    Victory
};

/**
 * @brief Main UI manager - coordinates all UI screens
 *
 * Manages screen transitions, input routing, and rendering of UI elements.
 * Maintains a screen stack for overlays (e.g., pause menu over gameplay).
 */
class GameUI {
public:
    explicit GameUI(Engine::Input& input, GameAudio& audio);
    ~GameUI();

    /**
     * @brief Initialize UI system
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown UI system
     */
    void shutdown();

    /**
     * @brief Update UI (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render UI (call after game rendering)
     * @param renderer Renderer to use for drawing
     */
    void render(CatEngine::Renderer::Renderer& renderer);

    // ========================================================================
    // State Management
    // ========================================================================

    /**
     * @brief Get current game state
     */
    GameState getGameState() const { return m_currentState; }

    /**
     * @brief Set game state and transition UI accordingly
     * @param state New game state
     */
    void setGameState(GameState state);

    /**
     * @brief Check if game is paused (UI is blocking gameplay)
     */
    bool isPaused() const {
        return m_currentState == GameState::MainMenu ||
               m_currentState == GameState::Paused ||
               m_currentState == GameState::WaveComplete ||
               m_currentState == GameState::GameOver;
    }

    // ========================================================================
    // Screen Access
    // ========================================================================

    /**
     * @brief Get HUD reference
     */
    HUD& getHUD() { return *m_hud; }

    /**
     * @brief Get Main Menu reference
     */
    MainMenu& getMainMenu() { return *m_mainMenu; }

    /**
     * @brief Get Pause Menu reference
     */
    PauseMenu& getPauseMenu() { return *m_pauseMenu; }

    /**
     * @brief Get Wave Popup reference
     */
    WavePopup& getWavePopup() { return *m_wavePopup; }

    // ========================================================================
    // Input Handling
    // ========================================================================

    /**
     * @brief Handle input events
     * Routes input to appropriate UI screen based on state
     */
    void handleInput();

    /**
     * @brief Check if UI is consuming input (blocks gameplay input)
     */
    bool isConsumingInput() const;

    // ========================================================================
    // Transitions
    // ========================================================================

    /**
     * @brief Start screen transition effect
     * @param duration Transition duration in seconds
     */
    void startTransition(float duration = 0.3f);

    /**
     * @brief Check if currently transitioning
     */
    bool isTransitioning() const { return m_isTransitioning; }

    /**
     * @brief Get transition progress (0.0 to 1.0)
     */
    float getTransitionProgress() const { return m_transitionProgress; }

private:
    /**
     * @brief Update screen visibility based on current state
     */
    void updateScreenVisibility();

    /**
     * @brief Update transition animation
     */
    void updateTransition(float deltaTime);

    Engine::Input& m_input;
    GameAudio& m_audio;

    // UI Screens
    std::unique_ptr<HUD> m_hud;
    std::unique_ptr<MainMenu> m_mainMenu;
    std::unique_ptr<PauseMenu> m_pauseMenu;
    std::unique_ptr<WavePopup> m_wavePopup;

    // State
    GameState m_currentState = GameState::MainMenu;
    GameState m_previousState = GameState::MainMenu;

    // Transition
    bool m_isTransitioning = false;
    float m_transitionProgress = 0.0f;
    float m_transitionDuration = 0.3f;
    float m_transitionTimer = 0.0f;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_GAME_UI_HPP

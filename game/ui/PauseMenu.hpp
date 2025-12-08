#ifndef GAME_UI_PAUSE_MENU_HPP
#define GAME_UI_PAUSE_MENU_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include <functional>
#include <vector>
#include <string>

namespace Game {

class GameAudio;

/**
 * @brief Pause Menu overlay
 *
 * Displayed when game is paused. Provides options to resume, restart, settings, or quit.
 */
class PauseMenu {
public:
    /**
     * @brief Menu button callback type
     */
    using ButtonCallback = std::function<void()>;

    explicit PauseMenu(Engine::Input& input, GameAudio& audio);
    ~PauseMenu();

    /**
     * @brief Initialize pause menu
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown pause menu
     */
    void shutdown();

    /**
     * @brief Update pause menu (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render pause menu
     * @param renderer Renderer to use for drawing
     */
    void render(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Handle input
     */
    void handleInput();

    // ========================================================================
    // Button Callbacks
    // ========================================================================

    /**
     * @brief Set callback for Resume button
     */
    void setResumeCallback(ButtonCallback callback) {
        m_resumeCallback = callback;
    }

    /**
     * @brief Set callback for Restart Wave button
     */
    void setRestartCallback(ButtonCallback callback) {
        m_restartCallback = callback;
    }

    /**
     * @brief Set callback for Settings button
     */
    void setSettingsCallback(ButtonCallback callback) {
        m_settingsCallback = callback;
    }

    /**
     * @brief Set callback for Main Menu button
     */
    void setMainMenuCallback(ButtonCallback callback) {
        m_mainMenuCallback = callback;
    }

    /**
     * @brief Set callback for Quit button
     */
    void setQuitCallback(ButtonCallback callback) {
        m_quitCallback = callback;
    }

    // ========================================================================
    // Confirmation Dialog
    // ========================================================================

    /**
     * @brief Check if confirmation dialog is active
     */
    bool isConfirmationActive() const { return m_confirmationActive; }

    /**
     * @brief Get confirmation dialog message
     */
    const std::string& getConfirmationMessage() const { return m_confirmationMessage; }

private:
    /**
     * @brief Menu button structure
     */
    struct MenuButton {
        std::string text;
        bool enabled = true;
        bool hovered = false;
        std::array<float, 2> position;
        std::array<float, 2> size;
        ButtonCallback callback;
        bool requiresConfirmation = false;
    };

    /**
     * @brief Update button states
     */
    void updateButtons();

    /**
     * @brief Render dimmed background overlay
     */
    void renderBackground(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render menu title
     */
    void renderTitle(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render buttons
     */
    void renderButtons(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render confirmation dialog
     */
    void renderConfirmationDialog(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Show confirmation dialog
     * @param message Confirmation message
     * @param onConfirm Callback if user confirms
     */
    void showConfirmation(const std::string& message, ButtonCallback onConfirm);

    /**
     * @brief Hide confirmation dialog
     */
    void hideConfirmation();

    Engine::Input& m_input;
    GameAudio& m_audio;

    // Buttons
    std::vector<MenuButton> m_buttons;
    int32_t m_selectedButtonIndex = 0;
    int32_t m_hoveredButtonIndex = -1;

    // Callbacks
    ButtonCallback m_resumeCallback;
    ButtonCallback m_restartCallback;
    ButtonCallback m_settingsCallback;
    ButtonCallback m_mainMenuCallback;
    ButtonCallback m_quitCallback;

    // Confirmation dialog
    bool m_confirmationActive = false;
    std::string m_confirmationMessage;
    ButtonCallback m_confirmationCallback;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_PAUSE_MENU_HPP

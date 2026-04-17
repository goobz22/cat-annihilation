#ifndef GAME_UI_PAUSE_MENU_HPP
#define GAME_UI_PAUSE_MENU_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include <functional>
#include <vector>
#include <string>
#include <array>

namespace Engine { class ImGuiLayer; }

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
     * @param uiPass UIPass to use for 2D drawing
     * @param screenWidth Current screen width
     * @param screenHeight Current screen height
     */
    void render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight);

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
        m_resumeCallback = std::move(callback);
    }

    /**
     * @brief Set callback for Restart Wave button
     */
    void setRestartCallback(ButtonCallback callback) {
        m_restartCallback = std::move(callback);
    }

    /**
     * @brief Set callback for Settings button
     */
    void setSettingsCallback(ButtonCallback callback) {
        m_settingsCallback = std::move(callback);
    }

    /**
     * @brief Set callback for Main Menu button
     */
    void setMainMenuCallback(ButtonCallback callback) {
        m_mainMenuCallback = std::move(callback);
    }

    /**
     * @brief Set callback for Quit button
     */
    void setQuitCallback(ButtonCallback callback) {
        m_quitCallback = std::move(callback);
    }

    // ========================================================================
    // Confirmation Dialog
    // ========================================================================

    /**
     * @brief Check if confirmation dialog is active
     */
    [[nodiscard]] bool isConfirmationActive() const { return m_confirmationActive; }

    /**
     * @brief Get confirmation dialog message
     */
    [[nodiscard]] const std::string& getConfirmationMessage() const { return m_confirmationMessage; }

    /**
     * @brief Attach the ImGui layer (used for fonts + widgets). Optional.
     */
    void setImGuiLayer(Engine::ImGuiLayer* imguiLayer) { m_imguiLayer = imguiLayer; }

private:
    /**
     * @brief Menu button structure
     */
    struct MenuButton {
        std::string text;
        bool enabled = true;
        bool hovered = false;
        std::array<float, 2> position = {0.0F, 0.0F};
        std::array<float, 2> size = {0.0F, 0.0F};
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
    void renderBackground(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render menu title
     */
    void renderTitle(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render buttons
     */
    void renderButtons(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render confirmation dialog
     */
    void renderConfirmationDialog(CatEngine::Renderer::UIPass& uiPass);

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

    /**
     * @brief Check if mouse is over button
     */
    bool isMouseOverButton(const MenuButton& button) const;

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

    // Screen dimensions (cached during render)
    uint32_t m_screenWidth = 1920;
    uint32_t m_screenHeight = 1080;

    bool m_initialized = false;

    // Optional ImGui layer (not owned). When set, render() builds widgets via ImGui.
    Engine::ImGuiLayer* m_imguiLayer = nullptr;
};

} // namespace Game

#endif // GAME_UI_PAUSE_MENU_HPP

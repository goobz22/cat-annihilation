#ifndef GAME_UI_MAIN_MENU_HPP
#define GAME_UI_MAIN_MENU_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include <functional>
#include <vector>
#include <string>

namespace Game {

class GameAudio;

/**
 * @brief Main Menu screen
 *
 * Title screen with buttons for starting game, continuing, settings, and quitting.
 */
class MainMenu {
public:
    /**
     * @brief Menu button callback type
     */
    using ButtonCallback = std::function<void()>;

    explicit MainMenu(Engine::Input& input, GameAudio& audio);
    ~MainMenu();

    /**
     * @brief Initialize main menu
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown main menu
     */
    void shutdown();

    /**
     * @brief Update main menu (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render main menu
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
     * @brief Set callback for Start Game button
     */
    void setStartGameCallback(ButtonCallback callback) {
        m_startGameCallback = callback;
    }

    /**
     * @brief Set callback for Continue button
     */
    void setContinueCallback(ButtonCallback callback) {
        m_continueCallback = callback;
    }

    /**
     * @brief Set callback for Settings button
     */
    void setSettingsCallback(ButtonCallback callback) {
        m_settingsCallback = callback;
    }

    /**
     * @brief Set callback for Quit button
     */
    void setQuitCallback(ButtonCallback callback) {
        m_quitCallback = callback;
    }

    // ========================================================================
    // State
    // ========================================================================

    /**
     * @brief Enable/disable Continue button (based on save existence)
     * @param hasSave true if save file exists
     */
    void setHasSaveGame(bool hasSave) { m_hasSaveGame = hasSave; }

    /**
     * @brief Set version string to display
     * @param version Version string (e.g., "v1.0.0")
     */
    void setVersionString(const std::string& version) { m_versionString = version; }

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
    };

    /**
     * @brief Update button states (hover detection)
     */
    void updateButtons();

    /**
     * @brief Render background
     */
    void renderBackground(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render title
     */
    void renderTitle(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render buttons
     */
    void renderButtons(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render version info
     */
    void renderVersion(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Check if mouse is over button
     */
    bool isMouseOverButton(const MenuButton& button);

    Engine::Input& m_input;
    GameAudio& m_audio;

    // Buttons
    std::vector<MenuButton> m_buttons;
    int32_t m_selectedButtonIndex = 0;
    int32_t m_hoveredButtonIndex = -1;

    // Callbacks
    ButtonCallback m_startGameCallback;
    ButtonCallback m_continueCallback;
    ButtonCallback m_settingsCallback;
    ButtonCallback m_quitCallback;

    // State
    bool m_hasSaveGame = false;
    std::string m_versionString = "v1.0.0";

    // Animation
    float m_titleAnimTimer = 0.0f;
    float m_backgroundAnimTimer = 0.0f;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_MAIN_MENU_HPP

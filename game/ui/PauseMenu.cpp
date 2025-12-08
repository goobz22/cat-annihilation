#include "PauseMenu.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"

namespace Game {

PauseMenu::PauseMenu(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

PauseMenu::~PauseMenu() {
    shutdown();
}

bool PauseMenu::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("PauseMenu already initialized");
        return true;
    }

    // Create menu buttons
    m_buttons.clear();

    // Resume button
    MenuButton resumeButton;
    resumeButton.text = "Resume";
    resumeButton.position = {400, 250};
    resumeButton.size = {200, 50};
    resumeButton.enabled = true;
    resumeButton.callback = [this]() {
        if (m_resumeCallback) {
            m_resumeCallback();
        }
    };
    m_buttons.push_back(resumeButton);

    // Restart Wave button
    MenuButton restartButton;
    restartButton.text = "Restart Wave";
    restartButton.position = {400, 310};
    restartButton.size = {200, 50};
    restartButton.enabled = true;
    restartButton.requiresConfirmation = true;
    restartButton.callback = [this]() {
        showConfirmation("Restart current wave?", m_restartCallback);
    };
    m_buttons.push_back(restartButton);

    // Settings button
    MenuButton settingsButton;
    settingsButton.text = "Settings";
    settingsButton.position = {400, 370};
    settingsButton.size = {200, 50};
    settingsButton.enabled = true;
    settingsButton.callback = [this]() {
        if (m_settingsCallback) {
            m_settingsCallback();
        } else {
            Engine::Logger::info("Settings menu not implemented yet");
        }
    };
    m_buttons.push_back(settingsButton);

    // Main Menu button
    MenuButton mainMenuButton;
    mainMenuButton.text = "Main Menu";
    mainMenuButton.position = {400, 430};
    mainMenuButton.size = {200, 50};
    mainMenuButton.enabled = true;
    mainMenuButton.requiresConfirmation = true;
    mainMenuButton.callback = [this]() {
        showConfirmation("Return to main menu? (Progress will be lost)", m_mainMenuCallback);
    };
    m_buttons.push_back(mainMenuButton);

    // Quit button
    MenuButton quitButton;
    quitButton.text = "Quit Game";
    quitButton.position = {400, 490};
    quitButton.size = {200, 50};
    quitButton.enabled = true;
    quitButton.requiresConfirmation = true;
    quitButton.callback = [this]() {
        showConfirmation("Quit to desktop?", m_quitCallback);
    };
    m_buttons.push_back(quitButton);

    m_selectedButtonIndex = 0;

    m_initialized = true;
    Engine::Logger::info("PauseMenu initialized successfully");
    return true;
}

void PauseMenu::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_buttons.clear();

    m_initialized = false;
    Engine::Logger::info("PauseMenu shutdown");
}

void PauseMenu::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update button states
    updateButtons();
}

void PauseMenu::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized) {
        return;
    }

    renderBackground(renderer);
    renderTitle(renderer);
    renderButtons(renderer);

    if (m_confirmationActive) {
        renderConfirmationDialog(renderer);
    }
}

void PauseMenu::handleInput() {
    if (!m_initialized) {
        return;
    }

    // If confirmation dialog is active, handle that first
    if (m_confirmationActive) {
        // Confirm with Enter or Y
        if (m_input.isKeyPressed(Engine::Input::Key::Enter) ||
            m_input.isKeyPressed(Engine::Input::Key::Y)) {
            m_audio.playMenuClick();
            if (m_confirmationCallback) {
                m_confirmationCallback();
            }
            hideConfirmation();
            return;
        }

        // Cancel with Escape or N
        if (m_input.isKeyPressed(Engine::Input::Key::Escape) ||
            m_input.isKeyPressed(Engine::Input::Key::N)) {
            m_audio.playMenuClick();
            hideConfirmation();
            return;
        }

        return; // Block other input while confirmation is active
    }

    // Keyboard navigation
    if (m_input.isKeyPressed(Engine::Input::Key::Down)) {
        m_selectedButtonIndex = (m_selectedButtonIndex + 1) % m_buttons.size();

        // Skip disabled buttons
        while (!m_buttons[m_selectedButtonIndex].enabled) {
            m_selectedButtonIndex = (m_selectedButtonIndex + 1) % m_buttons.size();
        }

        m_audio.playMenuHover();
    }

    if (m_input.isKeyPressed(Engine::Input::Key::Up)) {
        m_selectedButtonIndex = (m_selectedButtonIndex - 1 + m_buttons.size()) % m_buttons.size();

        // Skip disabled buttons
        while (!m_buttons[m_selectedButtonIndex].enabled) {
            m_selectedButtonIndex = (m_selectedButtonIndex - 1 + m_buttons.size()) % m_buttons.size();
        }

        m_audio.playMenuHover();
    }

    // Activate button with Enter or Space
    if (m_input.isKeyPressed(Engine::Input::Key::Enter) ||
        m_input.isKeyPressed(Engine::Input::Key::Space)) {

        if (m_buttons[m_selectedButtonIndex].enabled) {
            m_audio.playMenuClick();
            if (m_buttons[m_selectedButtonIndex].callback) {
                m_buttons[m_selectedButtonIndex].callback();
            }
        }
    }

    // Mouse click
    if (m_input.isMouseButtonPressed(Engine::Input::MouseButton::Left)) {
        if (m_hoveredButtonIndex >= 0 &&
            m_hoveredButtonIndex < static_cast<int32_t>(m_buttons.size())) {

            auto& button = m_buttons[m_hoveredButtonIndex];
            if (button.enabled && button.callback) {
                m_audio.playMenuClick();
                button.callback();
            }
        }
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void PauseMenu::updateButtons() {
    // Update button hover states based on mouse position
    Engine::f64 mouseX, mouseY;
    m_input.getMousePosition(mouseX, mouseY);

    int32_t previousHoveredIndex = m_hoveredButtonIndex;
    m_hoveredButtonIndex = -1;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        auto& button = m_buttons[i];

        // Check if mouse is over button
        bool isOver = mouseX >= button.position[0] &&
                      mouseX <= button.position[0] + button.size[0] &&
                      mouseY >= button.position[1] &&
                      mouseY <= button.position[1] + button.size[1];

        button.hovered = isOver && button.enabled;

        if (button.hovered) {
            m_hoveredButtonIndex = static_cast<int32_t>(i);

            // Play hover sound on first hover
            if (previousHoveredIndex != m_hoveredButtonIndex) {
                m_audio.playMenuHover();
            }
        }
    }
}

void PauseMenu::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    // Dim the entire screen
    renderer.drawRect({0, 0}, {screenWidth, screenHeight},
                     {0, 0, 0, 0.7f}); // Semi-transparent black
    */
}

void PauseMenu::renderTitle(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    vec2 titlePos = {screenWidth / 2 - 100, 150};
    renderer.drawText("PAUSED", titlePos, {1, 1, 1, 1}, 42);
    */
}

void PauseMenu::renderButtons(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const auto& button = m_buttons[i];

        // Button background
        vec4 bgColor;
        if (!button.enabled) {
            bgColor = {0.2f, 0.2f, 0.2f, 0.5f}; // Disabled
        } else if (button.hovered || i == m_selectedButtonIndex) {
            bgColor = {0.5f, 0.5f, 0.7f, 0.9f}; // Highlighted
        } else {
            bgColor = {0.3f, 0.3f, 0.4f, 0.8f}; // Normal
        }

        renderer.drawRect(button.position, button.size, bgColor);

        // Button border
        if (i == m_selectedButtonIndex) {
            renderer.drawRectOutline(button.position, button.size,
                                    {1, 1, 1, 1}, 2.0f);
        }

        // Button text
        vec4 textColor = button.enabled ?
            vec4{1, 1, 1, 1} : vec4{0.5f, 0.5f, 0.5f, 1};

        vec2 textPos = {
            button.position[0] + button.size[0] / 2 - 50,
            button.position[1] + button.size[1] / 2 - 10
        };

        renderer.drawText(button.text, textPos, textColor, 20);
    }
    */
}

void PauseMenu::renderConfirmationDialog(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    // Darken background even more
    renderer.drawRect({0, 0}, {screenWidth, screenHeight},
                     {0, 0, 0, 0.5f});

    // Dialog box
    vec2 dialogPos = {screenWidth / 2 - 200, screenHeight / 2 - 100};
    vec2 dialogSize = {400, 200};

    renderer.drawRect(dialogPos, dialogSize, {0.2f, 0.2f, 0.3f, 1.0f});
    renderer.drawRectOutline(dialogPos, dialogSize, {1, 1, 1, 1}, 2.0f);

    // Message
    vec2 messagePos = {dialogPos[0] + 20, dialogPos[1] + 40};
    renderer.drawText(m_confirmationMessage, messagePos, {1, 1, 1, 1}, 18);

    // Buttons hint
    vec2 hintPos = {dialogPos[0] + 60, dialogPos[1] + 120};
    renderer.drawText("Y - Confirm    N - Cancel", hintPos, {0.8f, 0.8f, 0.8f, 1}, 16);
    */
}

void PauseMenu::showConfirmation(const std::string& message, ButtonCallback onConfirm) {
    m_confirmationActive = true;
    m_confirmationMessage = message;
    m_confirmationCallback = onConfirm;
}

void PauseMenu::hideConfirmation() {
    m_confirmationActive = false;
    m_confirmationMessage.clear();
    m_confirmationCallback = nullptr;
}

} // namespace Game

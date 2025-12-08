#include "MainMenu.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>

namespace Game {

MainMenu::MainMenu(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

MainMenu::~MainMenu() {
    shutdown();
}

bool MainMenu::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("MainMenu already initialized");
        return true;
    }

    // Create menu buttons
    m_buttons.clear();

    // Start Game button
    MenuButton startButton;
    startButton.text = "Start Game";
    startButton.position = {400, 300};
    startButton.size = {200, 50};
    startButton.enabled = true;
    startButton.callback = [this]() {
        if (m_startGameCallback) {
            m_startGameCallback();
        }
    };
    m_buttons.push_back(startButton);

    // Continue button
    MenuButton continueButton;
    continueButton.text = "Continue";
    continueButton.position = {400, 360};
    continueButton.size = {200, 50};
    continueButton.enabled = m_hasSaveGame;
    continueButton.callback = [this]() {
        if (m_continueCallback) {
            m_continueCallback();
        }
    };
    m_buttons.push_back(continueButton);

    // Settings button
    MenuButton settingsButton;
    settingsButton.text = "Settings";
    settingsButton.position = {400, 420};
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

    // Quit button
    MenuButton quitButton;
    quitButton.text = "Quit";
    quitButton.position = {400, 480};
    quitButton.size = {200, 50};
    quitButton.enabled = true;
    quitButton.callback = [this]() {
        if (m_quitCallback) {
            m_quitCallback();
        }
    };
    m_buttons.push_back(quitButton);

    m_selectedButtonIndex = 0;

    m_initialized = true;
    Engine::Logger::info("MainMenu initialized successfully");
    return true;
}

void MainMenu::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_buttons.clear();

    m_initialized = false;
    Engine::Logger::info("MainMenu shutdown");
}

void MainMenu::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update animations
    m_titleAnimTimer += deltaTime;
    m_backgroundAnimTimer += deltaTime * 0.5f;

    // Update button states
    updateButtons();
}

void MainMenu::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized) {
        return;
    }

    renderBackground(renderer);
    renderTitle(renderer);
    renderButtons(renderer);
    renderVersion(renderer);
}

void MainMenu::handleInput() {
    if (!m_initialized) {
        return;
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

void MainMenu::updateButtons() {
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

void MainMenu::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    // Animated gradient or parallax background
    float animOffset = std::sin(m_backgroundAnimTimer) * 20.0f;

    renderer.drawGradient({0, 0}, {screenWidth, screenHeight},
                         {0.1f, 0.1f, 0.2f, 1.0f},  // Dark blue
                         {0.2f, 0.1f, 0.3f, 1.0f}); // Purple

    // Draw some animated particles or stars
    for (int i = 0; i < 50; ++i) {
        float x = (i * 37.0f) % screenWidth;
        float y = ((i * 53.0f + animOffset) % screenHeight);
        float alpha = (std::sin(m_backgroundAnimTimer + i) + 1.0f) * 0.5f;

        renderer.drawCircle({x, y}, 2.0f, {1, 1, 1, alpha * 0.5f});
    }
    */
}

void MainMenu::renderTitle(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    // Animated title text
    float bounce = std::sin(m_titleAnimTimer * 2.0f) * 5.0f;

    vec2 titlePos = {screenWidth / 2 - 200, 100 + bounce};
    renderer.drawText("CAT ANNIHILATION", titlePos, {1, 0.8f, 0, 1}, 48);

    // Subtitle
    vec2 subtitlePos = {screenWidth / 2 - 150, 160};
    renderer.drawText("Survive the Waves", subtitlePos, {0.8f, 0.8f, 0.8f, 1}, 24);
    */
}

void MainMenu::renderButtons(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const auto& button = m_buttons[i];

        // Button background
        vec4 bgColor;
        if (!button.enabled) {
            bgColor = {0.2f, 0.2f, 0.2f, 0.5f}; // Disabled
        } else if (button.hovered || i == m_selectedButtonIndex) {
            bgColor = {0.6f, 0.4f, 0.1f, 0.9f}; // Highlighted
        } else {
            bgColor = {0.3f, 0.3f, 0.3f, 0.8f}; // Normal
        }

        renderer.drawRect(button.position, button.size, bgColor);

        // Button border
        if (i == m_selectedButtonIndex) {
            renderer.drawRectOutline(button.position, button.size,
                                    {1, 0.8f, 0, 1}, 2.0f);
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

void MainMenu::renderVersion(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement with actual rendering
    /*
    vec2 versionPos = {screenWidth - 100, screenHeight - 30};
    renderer.drawText(m_versionString, versionPos, {0.5f, 0.5f, 0.5f, 1}, 14);
    */
}

bool MainMenu::isMouseOverButton(const MenuButton& button) {
    Engine::f64 mouseX, mouseY;
    m_input.getMousePosition(mouseX, mouseY);

    return mouseX >= button.position[0] &&
           mouseX <= button.position[0] + button.size[0] &&
           mouseY >= button.position[1] &&
           mouseY <= button.position[1] + button.size[1];
}

} // namespace Game

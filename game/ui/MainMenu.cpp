#include "MainMenu.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/ui/ImGuiLayer.hpp"

#include "imgui.h"

#include <cmath>
#include <iostream>

namespace Game {

// ---------------------------------------------------------------------------
// Settings panel state
//
// No dedicated Settings singleton exists yet, so the panel owns its own sliders
// via file-local storage. When a Settings system lands these are the first
// values to wire through to the real store. The layout is deliberately narrow
// (ImGui sliders + checkboxes) so the panel works on every platform the engine
// already supports.
// ---------------------------------------------------------------------------
namespace {

struct SettingsPanelState {
    bool   open             = false;
    float  masterVolume     = 0.80F;
    float  musicVolume      = 0.70F;
    float  sfxVolume        = 0.90F;
    float  mouseSensitivity = 1.00F;
    bool   fullscreen       = false;
    bool   vsync            = true;
    bool   invertY          = false;
};

SettingsPanelState& settingsState() {
    static SettingsPanelState state;
    return state;
}

void drawSettingsPanel(GameAudio& /*audio*/) {
    SettingsPanelState& state = settingsState();
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480.0F, 360.0F), ImGuiCond_Appearing);
    if (ImGui::Begin("Settings", &state.open)) {
        ImGui::TextUnformatted("Audio");
        ImGui::Separator();
        // Volumes are held locally until GameAudio exposes a mixer setter; the
        // sliders still respond instantly so the panel is never an empty shell.
        ImGui::SliderFloat("Master Volume", &state.masterVolume, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Music Volume",  &state.musicVolume,  0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("SFX Volume",    &state.sfxVolume,    0.0F, 1.0F, "%.2f");

        ImGui::Dummy(ImVec2(0.0F, 6.0F));
        ImGui::TextUnformatted("Input");
        ImGui::Separator();
        ImGui::SliderFloat("Mouse Sensitivity", &state.mouseSensitivity, 0.25F, 4.0F, "%.2f");
        ImGui::Checkbox("Invert Y Axis", &state.invertY);

        ImGui::Dummy(ImVec2(0.0F, 6.0F));
        ImGui::TextUnformatted("Display");
        ImGui::Separator();
        ImGui::Checkbox("Fullscreen", &state.fullscreen);
        ImGui::Checkbox("VSync",      &state.vsync);

        ImGui::Dummy(ImVec2(0.0F, 12.0F));
        if (ImGui::Button("Close", ImVec2(120.0F, 0.0F))) {
            state.open = false;
        }
    }
    ImGui::End();
}

} // namespace

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
    startButton.position = {400.0F, 300.0F};
    startButton.size = {200.0F, 50.0F};
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
    continueButton.position = {400.0F, 360.0F};
    continueButton.size = {200.0F, 50.0F};
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
    settingsButton.position = {400.0F, 420.0F};
    settingsButton.size = {200.0F, 50.0F};
    settingsButton.enabled = true;
    settingsButton.callback = [this]() {
        if (m_settingsCallback) {
            m_settingsCallback();
        } else {
            // Toggle the in-menu settings window. A consumer-supplied callback
            // (e.g. to open a dedicated scene) still takes precedence.
            settingsState().open = !settingsState().open;
        }
    };
    m_buttons.push_back(settingsButton);

    // Quit button
    MenuButton quitButton;
    quitButton.text = "Quit";
    quitButton.position = {400.0F, 480.0F};
    quitButton.size = {200.0F, 50.0F};
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
    m_backgroundAnimTimer += deltaTime * 0.5F;

    // Update button states
    updateButtons();
}

void MainMenu::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized) {
        return;
    }

    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Keep the atmospheric background (gradient + animated stars) as UIPass quads —
    // those render fine and don't involve the old bitmap-font path.
    renderBackground(uiPass);

    // Everything else (title, buttons, version) is now built with Dear ImGui so we
    // get real typography, keyboard nav, and hover/focus states for free.
    if (m_imguiLayer == nullptr) {
        return;
    }

    const float width = static_cast<float>(screenWidth);
    const float height = static_cast<float>(screenHeight);

    // Full-screen transparent window that hosts the title + buttons.
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##MainMenuOverlay", nullptr, kOverlayFlags);

    // ------------------------------------------------------------------ Title
    if (auto* titleFont = m_imguiLayer->GetTitleFont()) {
        ImGui::PushFont(titleFont);
    }
    const char* titleText = "CAT ANNIHILATION";
    const ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    const float titleY = height * 0.18F;
    ImGui::SetCursorPos(ImVec2((width - titleSize.x) * 0.5F, titleY));
    ImGui::TextColored(ImVec4(1.00F, 0.80F, 0.10F, 1.00F), "%s", titleText);
    if (m_imguiLayer->GetTitleFont() != nullptr) {
        ImGui::PopFont();
    }

    // --------------------------------------------------------------- Subtitle
    if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
        ImGui::PushFont(regularFont);
    }
    const char* subtitleText = "Survive the Waves";
    const ImVec2 subSize = ImGui::CalcTextSize(subtitleText);
    ImGui::SetCursorPos(ImVec2((width - subSize.x) * 0.5F, titleY + titleSize.y + 4.0F));
    ImGui::TextColored(ImVec4(0.80F, 0.80F, 0.90F, 0.90F), "%s", subtitleText);
    if (m_imguiLayer->GetRegularFont() != nullptr) {
        ImGui::PopFont();
    }

    // ----------------------------------------------------------------- Buttons
    if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
        ImGui::PushFont(boldFont);
    }
    const float buttonWidth = 360.0F;
    const float buttonHeight = 60.0F;
    const float buttonSpacing = 16.0F;
    const float totalButtonsHeight = (buttonHeight + buttonSpacing) * static_cast<float>(m_buttons.size()) - buttonSpacing;
    float cursorY = height * 0.48F;
    const float buttonX = (width - buttonWidth) * 0.5F;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        auto& button = m_buttons[i];
        ImGui::SetCursorPos(ImVec2(buttonX, cursorY));
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!button.enabled);
        if (ImGui::Button(button.text.c_str(), ImVec2(buttonWidth, buttonHeight))) {
            m_audio.playMenuClick();
            if (button.callback) {
                button.callback();
            }
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        ImGui::PopID();

        button.hovered = hovered;
        if (hovered) {
            m_hoveredButtonIndex = static_cast<int32_t>(i);
        }
        cursorY += buttonHeight + buttonSpacing;
    }
    // Suppress unused-variable warning while also documenting layout intent.
    (void)totalButtonsHeight;
    if (m_imguiLayer->GetBoldFont() != nullptr) {
        ImGui::PopFont();
    }

    // ----------------------------------------------------------------- Version
    if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
        ImGui::PushFont(regularFont);
    }
    const ImVec2 versionSize = ImGui::CalcTextSize(m_versionString.c_str());
    ImGui::SetCursorPos(ImVec2(width - versionSize.x - 20.0F, height - versionSize.y - 12.0F));
    ImGui::TextColored(ImVec4(0.55F, 0.55F, 0.60F, 0.8F), "%s", m_versionString.c_str());

    const char* credits = "Made with CatEngine";
    ImGui::SetCursorPos(ImVec2(20.0F, height - ImGui::CalcTextSize(credits).y - 12.0F));
    ImGui::TextColored(ImVec4(0.45F, 0.45F, 0.55F, 0.7F), "%s", credits);
    if (m_imguiLayer->GetRegularFont() != nullptr) {
        ImGui::PopFont();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Draw the settings panel last so it layers on top of the overlay window.
    drawSettingsPanel(m_audio);
}

void MainMenu::handleInput() {
    if (!m_initialized) {
        return;
    }

    // Keyboard navigation
    if (m_input.isKeyPressed(Engine::Input::Key::Down)) {
        int32_t startIndex = m_selectedButtonIndex;
        do {
            m_selectedButtonIndex = (m_selectedButtonIndex + 1) % static_cast<int32_t>(m_buttons.size());
        } while (!m_buttons[m_selectedButtonIndex].enabled && m_selectedButtonIndex != startIndex);

        if (m_selectedButtonIndex != startIndex) {
            m_audio.playMenuHover();
        }
    }

    if (m_input.isKeyPressed(Engine::Input::Key::Up)) {
        int32_t startIndex = m_selectedButtonIndex;
        do {
            m_selectedButtonIndex = (m_selectedButtonIndex - 1 + static_cast<int32_t>(m_buttons.size()))
                                  % static_cast<int32_t>(m_buttons.size());
        } while (!m_buttons[m_selectedButtonIndex].enabled && m_selectedButtonIndex != startIndex);

        if (m_selectedButtonIndex != startIndex) {
            m_audio.playMenuHover();
        }
    }

    // Activate button with Enter or Space
    if (m_input.isKeyPressed(Engine::Input::Key::Enter) ||
        m_input.isKeyPressed(Engine::Input::Key::Space)) {

        if (m_selectedButtonIndex >= 0 &&
            m_selectedButtonIndex < static_cast<int32_t>(m_buttons.size()) &&
            m_buttons[m_selectedButtonIndex].enabled) {
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
    Engine::f64 mouseX = 0.0;
    Engine::f64 mouseY = 0.0;
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

void MainMenu::renderBackground(CatEngine::Renderer::UIPass& uiPass) {
    // Dark gradient background
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 0.0F;
    bgQuad.y = 0.0F;
    bgQuad.width = static_cast<float>(m_screenWidth);
    bgQuad.height = static_cast<float>(m_screenHeight);
    bgQuad.r = 0.05F;
    bgQuad.g = 0.05F;
    bgQuad.b = 0.12F;
    bgQuad.a = 1.0F;
    bgQuad.depth = 0.0F;
    bgQuad.texture = nullptr;
    uiPass.DrawQuad(bgQuad);

    // Animated stars/particles
    constexpr int starCount = 50;
    float animOffset = std::sin(m_backgroundAnimTimer) * 20.0F;

    for (int i = 0; i < starCount; ++i) {
        auto seedX = static_cast<float>(i * 37);
        auto seedY = static_cast<float>(i * 53);

        float x = std::fmod(seedX, static_cast<float>(m_screenWidth));
        float y = std::fmod(seedY + animOffset, static_cast<float>(m_screenHeight));
        if (y < 0.0F) {
            y += static_cast<float>(m_screenHeight);
        }

        float alpha = (std::sin((m_backgroundAnimTimer * 2.0F) + static_cast<float>(i)) + 1.0F) * 0.25F;
        float size = 2.0F + (static_cast<float>(i % 3) * 1.0F);

        CatEngine::Renderer::UIPass::QuadDesc star;
        star.x = x - (size / 2.0F);
        star.y = y - (size / 2.0F);
        star.width = size;
        star.height = size;
        star.r = 1.0F;
        star.g = 1.0F;
        star.b = 1.0F;
        star.a = alpha;
        star.depth = 0.05F;
        star.texture = nullptr;
        uiPass.DrawQuad(star);
    }

    // Subtle gradient overlay at bottom
    CatEngine::Renderer::UIPass::QuadDesc gradientOverlay;
    gradientOverlay.x = 0.0F;
    gradientOverlay.y = static_cast<float>(m_screenHeight) * 0.7F;
    gradientOverlay.width = static_cast<float>(m_screenWidth);
    gradientOverlay.height = static_cast<float>(m_screenHeight) * 0.3F;
    gradientOverlay.r = 0.1F;
    gradientOverlay.g = 0.05F;
    gradientOverlay.b = 0.15F;
    gradientOverlay.a = 0.5F;
    gradientOverlay.depth = 0.06F;
    gradientOverlay.texture = nullptr;
    uiPass.DrawQuad(gradientOverlay);
}

void MainMenu::renderTitle(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;

    // Animated title bounce
    float bounce = std::sin(m_titleAnimTimer * 2.0F) * 8.0F;
    float titleY = (static_cast<float>(m_screenHeight) * 0.18F) + bounce;

    // Title shadow
    CatEngine::Renderer::UIPass::TextDesc titleShadow;
    titleShadow.text = "CAT ANNIHILATION";
    titleShadow.x = centerX - 280.0F + 3.0F;
    titleShadow.y = titleY + 3.0F;
    titleShadow.fontSize = 56.0F;
    titleShadow.r = 0.0F;
    titleShadow.g = 0.0F;
    titleShadow.b = 0.0F;
    titleShadow.a = 0.6F;
    titleShadow.depth = 0.09F;
    titleShadow.fontAtlas = nullptr;
    uiPass.DrawText(titleShadow);

    // Main title with pulsing effect
    float pulse = (std::sin(m_titleAnimTimer * 3.0F) + 1.0F) * 0.5F;
    float titleR = 1.0F;
    float titleG = 0.7F + (pulse * 0.2F);
    float titleB = 0.0F + (pulse * 0.1F);

    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = "CAT ANNIHILATION";
    titleText.x = centerX - 280.0F;
    titleText.y = titleY;
    titleText.fontSize = 56.0F;
    titleText.r = titleR;
    titleText.g = titleG;
    titleText.b = titleB;
    titleText.a = 1.0F;
    titleText.depth = 0.1F;
    titleText.fontAtlas = nullptr;
    uiPass.DrawText(titleText);

    // Subtitle
    float subtitleY = titleY + 70.0F;
    CatEngine::Renderer::UIPass::TextDesc subtitleText;
    subtitleText.text = "Survive the Waves";
    subtitleText.x = centerX - 120.0F;
    subtitleText.y = subtitleY;
    subtitleText.fontSize = 24.0F;
    subtitleText.r = 0.7F;
    subtitleText.g = 0.7F;
    subtitleText.b = 0.8F;
    subtitleText.a = 0.9F;
    subtitleText.depth = 0.1F;
    subtitleText.fontAtlas = nullptr;
    uiPass.DrawText(subtitleText);
}

void MainMenu::renderButtons(CatEngine::Renderer::UIPass& uiPass) {
    static int renderCount = 0;
    renderCount++;
    
    if (renderCount <= 3) {
        std::cout << "[MainMenu::renderButtons] Drawing " << m_buttons.size() << " buttons\n";
    }
    
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const auto& button = m_buttons[i];
        bool isSelected = (static_cast<int32_t>(i) == m_selectedButtonIndex);
        bool isHovered = button.hovered;
        
        if (renderCount <= 3) {
            std::cout << "[MainMenu::renderButtons] Button " << i << ": pos=(" << button.position[0] << "," << button.position[1] 
                      << "), size=(" << button.size[0] << "x" << button.size[1] << "), text=" << button.text << "\n";
        }

        // Button background
        float bgR = 0.25F;
        float bgG = 0.25F;
        float bgB = 0.3F;
        float bgA = 0.8F;
        if (!button.enabled) {
            bgR = 0.2F;
            bgG = 0.2F;
            bgB = 0.2F;
            bgA = 0.4F;
        } else if (isHovered || isSelected) {
            bgR = 0.6F;
            bgG = 0.4F;
            bgB = 0.1F;
            bgA = 0.9F;
        }

        CatEngine::Renderer::UIPass::QuadDesc buttonBg;
        buttonBg.x = button.position[0];
        buttonBg.y = button.position[1];
        buttonBg.width = button.size[0];
        buttonBg.height = button.size[1];
        buttonBg.r = bgR;
        buttonBg.g = bgG;
        buttonBg.b = bgB;
        buttonBg.a = bgA;
        buttonBg.depth = 0.2F;
        buttonBg.texture = nullptr;
        uiPass.DrawQuad(buttonBg);

        // Selection/highlight border
        if (isSelected && button.enabled) {
            float borderThickness = 3.0F;

            // Top border
            CatEngine::Renderer::UIPass::QuadDesc topBorder;
            topBorder.x = button.position[0];
            topBorder.y = button.position[1];
            topBorder.width = button.size[0];
            topBorder.height = borderThickness;
            topBorder.r = 1.0F;
            topBorder.g = 0.8F;
            topBorder.b = 0.0F;
            topBorder.a = 1.0F;
            topBorder.depth = 0.25F;
            topBorder.texture = nullptr;
            uiPass.DrawQuad(topBorder);

            // Bottom border
            CatEngine::Renderer::UIPass::QuadDesc bottomBorder;
            bottomBorder.x = button.position[0];
            bottomBorder.y = button.position[1] + button.size[1] - borderThickness;
            bottomBorder.width = button.size[0];
            bottomBorder.height = borderThickness;
            bottomBorder.r = 1.0F;
            bottomBorder.g = 0.8F;
            bottomBorder.b = 0.0F;
            bottomBorder.a = 1.0F;
            bottomBorder.depth = 0.25F;
            bottomBorder.texture = nullptr;
            uiPass.DrawQuad(bottomBorder);

            // Left border
            CatEngine::Renderer::UIPass::QuadDesc leftBorder;
            leftBorder.x = button.position[0];
            leftBorder.y = button.position[1];
            leftBorder.width = borderThickness;
            leftBorder.height = button.size[1];
            leftBorder.r = 1.0F;
            leftBorder.g = 0.8F;
            leftBorder.b = 0.0F;
            leftBorder.a = 1.0F;
            leftBorder.depth = 0.25F;
            leftBorder.texture = nullptr;
            uiPass.DrawQuad(leftBorder);

            // Right border
            CatEngine::Renderer::UIPass::QuadDesc rightBorder;
            rightBorder.x = button.position[0] + button.size[0] - borderThickness;
            rightBorder.y = button.position[1];
            rightBorder.width = borderThickness;
            rightBorder.height = button.size[1];
            rightBorder.r = 1.0F;
            rightBorder.g = 0.8F;
            rightBorder.b = 0.0F;
            rightBorder.a = 1.0F;
            rightBorder.depth = 0.25F;
            rightBorder.texture = nullptr;
            uiPass.DrawQuad(rightBorder);

            // Selection indicator arrow
            CatEngine::Renderer::UIPass::TextDesc arrow;
            arrow.text = ">";
            arrow.x = button.position[0] - 30.0F;
            arrow.y = button.position[1] + (button.size[1] / 2.0F) - 12.0F;
            arrow.fontSize = 24.0F;
            arrow.r = 1.0F;
            arrow.g = 0.8F;
            arrow.b = 0.0F;
            arrow.a = 1.0F;
            arrow.depth = 0.25F;
            arrow.fontAtlas = nullptr;
            uiPass.DrawText(arrow);
        }

        // Button text
        float textR = 1.0F;
        float textG = 1.0F;
        float textB = 1.0F;
        float textA = 1.0F;
        if (!button.enabled) {
            textR = 0.5F;
            textG = 0.5F;
            textB = 0.5F;
            textA = 0.7F;
        }

        // Estimate text centering
        float textWidth = static_cast<float>(button.text.length()) * 10.0F;
        float textX = button.position[0] + ((button.size[0] - textWidth) / 2.0F);
        float textY = button.position[1] + ((button.size[1] - 20.0F) / 2.0F);

        CatEngine::Renderer::UIPass::TextDesc buttonText;
        buttonText.text = button.text.c_str();
        buttonText.x = textX;
        buttonText.y = textY;
        buttonText.fontSize = 22.0F;
        buttonText.r = textR;
        buttonText.g = textG;
        buttonText.b = textB;
        buttonText.a = textA;
        buttonText.depth = 0.3F;
        buttonText.fontAtlas = nullptr;
        uiPass.DrawText(buttonText);
    }
}

void MainMenu::renderVersion(CatEngine::Renderer::UIPass& uiPass) {
    float versionX = static_cast<float>(m_screenWidth) - 120.0F;
    float versionY = static_cast<float>(m_screenHeight) - 40.0F;

    CatEngine::Renderer::UIPass::TextDesc versionText;
    versionText.text = m_versionString.c_str();
    versionText.x = versionX;
    versionText.y = versionY;
    versionText.fontSize = 14.0F;
    versionText.r = 0.5F;
    versionText.g = 0.5F;
    versionText.b = 0.5F;
    versionText.a = 0.7F;
    versionText.depth = 0.1F;
    versionText.fontAtlas = nullptr;
    uiPass.DrawText(versionText);

    // Credits
    CatEngine::Renderer::UIPass::TextDesc creditsText;
    creditsText.text = "Made with CatEngine";
    creditsText.x = 20.0F;
    creditsText.y = static_cast<float>(m_screenHeight) - 40.0F;
    creditsText.fontSize = 12.0F;
    creditsText.r = 0.4F;
    creditsText.g = 0.4F;
    creditsText.b = 0.5F;
    creditsText.a = 0.6F;
    creditsText.depth = 0.1F;
    creditsText.fontAtlas = nullptr;
    uiPass.DrawText(creditsText);
}

bool MainMenu::isMouseOverButton(const MenuButton& button) const {
    Engine::f64 mouseX = 0.0;
    Engine::f64 mouseY = 0.0;
    m_input.getMousePosition(mouseX, mouseY);

    return mouseX >= button.position[0] &&
           mouseX <= button.position[0] + button.size[0] &&
           mouseY >= button.position[1] &&
           mouseY <= button.position[1] + button.size[1];
}

} // namespace Game

#include "PauseMenu.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/ui/ImGuiLayer.hpp"

#include "imgui.h"

namespace Game {

// Settings panel state — same design as MainMenu's panel. The two menus
// deliberately share neither code nor storage so the pause-menu overlay can
// evolve independently (e.g. add "Apply & Return to Combat" logic later).
namespace {

struct PauseSettingsState {
    bool  open             = false;
    float masterVolume     = 0.80F;
    float musicVolume      = 0.70F;
    float sfxVolume        = 0.90F;
    float mouseSensitivity = 1.00F;
    bool  fullscreen       = false;
    bool  vsync            = true;
    bool  invertY          = false;
};

PauseSettingsState& pauseSettingsState() {
    static PauseSettingsState state;
    return state;
}

void drawPauseSettingsPanel() {
    PauseSettingsState& state = pauseSettingsState();
    if (!state.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480.0F, 360.0F), ImGuiCond_Appearing);
    if (ImGui::Begin("Settings", &state.open)) {
        ImGui::TextUnformatted("Audio");
        ImGui::Separator();
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
    resumeButton.position = {400.0F, 250.0F};
    resumeButton.size = {200.0F, 50.0F};
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
    restartButton.position = {400.0F, 310.0F};
    restartButton.size = {200.0F, 50.0F};
    restartButton.enabled = true;
    restartButton.requiresConfirmation = true;
    restartButton.callback = [this]() {
        showConfirmation("Restart current wave?", m_restartCallback);
    };
    m_buttons.push_back(restartButton);

    // Settings button
    MenuButton settingsButton;
    settingsButton.text = "Settings";
    settingsButton.position = {400.0F, 370.0F};
    settingsButton.size = {200.0F, 50.0F};
    settingsButton.enabled = true;
    settingsButton.callback = [this]() {
        if (m_settingsCallback) {
            m_settingsCallback();
        } else {
            // Toggle the in-pause settings window. A consumer-supplied
            // callback (e.g. to open a dedicated scene) still takes precedence.
            pauseSettingsState().open = !pauseSettingsState().open;
        }
    };
    m_buttons.push_back(settingsButton);

    // Main Menu button
    MenuButton mainMenuButton;
    mainMenuButton.text = "Main Menu";
    mainMenuButton.position = {400.0F, 430.0F};
    mainMenuButton.size = {200.0F, 50.0F};
    mainMenuButton.enabled = true;
    mainMenuButton.requiresConfirmation = true;
    mainMenuButton.callback = [this]() {
        showConfirmation("Return to main menu? (Progress will be lost)", m_mainMenuCallback);
    };
    m_buttons.push_back(mainMenuButton);

    // Quit button
    MenuButton quitButton;
    quitButton.text = "Quit Game";
    quitButton.position = {400.0F, 490.0F};
    quitButton.size = {200.0F, 50.0F};
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

void PauseMenu::update(float /*deltaTime*/) {
    if (!m_initialized) {
        return;
    }

    // Update button states
    updateButtons();
}

void PauseMenu::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized) {
        return;
    }

    // Cache screen dimensions
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    (void)uiPass; // legacy helpers below are unreachable; unused in the ImGui path

    // Require the ImGui layer — the legacy UIPass path is dead now (same as MainMenu).
    if (m_imguiLayer == nullptr) {
        return;
    }

    const float width = static_cast<float>(screenWidth);
    const float height = static_cast<float>(screenHeight);

    // ------------------------------------------------------------- Dim overlay
    // Full-screen transparent window that darkens the 3D scene behind.
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.55F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##PauseMenuOverlay", nullptr, kOverlayFlags);

    // --------------------------------------------------------------- Title
    if (auto* titleFont = m_imguiLayer->GetTitleFont()) {
        ImGui::PushFont(titleFont);
    }
    const char* titleText = "PAUSED";
    const ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    const float titleY = height * 0.22F;
    ImGui::SetCursorPos(ImVec2((width - titleSize.x) * 0.5F, titleY));
    ImGui::TextColored(ImVec4(1.00F, 0.92F, 0.55F, 1.00F), "%s", titleText);
    if (m_imguiLayer->GetTitleFont() != nullptr) {
        ImGui::PopFont();
    }

    // -------------------------------------------------------------- Buttons
    if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
        ImGui::PushFont(boldFont);
    }
    constexpr float buttonWidth = 340.0F;
    constexpr float buttonHeight = 56.0F;
    constexpr float buttonSpacing = 14.0F;
    float cursorY = height * 0.36F;
    const float buttonX = (width - buttonWidth) * 0.5F;

    const bool confirmBlocking = m_confirmationActive;
    ImGui::BeginDisabled(confirmBlocking);

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        auto& button = m_buttons[i];
        button.position[0] = buttonX;
        button.position[1] = cursorY;
        button.size[0] = buttonWidth;
        button.size[1] = buttonHeight;

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

    ImGui::EndDisabled();

    if (m_imguiLayer->GetBoldFont() != nullptr) {
        ImGui::PopFont();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // ----------------------------------------------------- Confirmation modal
    if (m_confirmationActive) {
        constexpr float dlgWidth = 460.0F;
        constexpr float dlgHeight = 170.0F;
        ImGui::SetNextWindowPos(
            ImVec2((width - dlgWidth) * 0.5F, (height - dlgHeight) * 0.5F));
        ImGui::SetNextWindowSize(ImVec2(dlgWidth, dlgHeight));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14F, 0.14F, 0.20F, 0.98F));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);

        constexpr ImGuiWindowFlags kDlgFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("##PauseConfirmDialog", nullptr, kDlgFlags);

        if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
            ImGui::PushFont(boldFont);
        }
        const ImVec2 msgSize = ImGui::CalcTextSize(m_confirmationMessage.c_str());
        ImGui::SetCursorPos(ImVec2((dlgWidth - msgSize.x) * 0.5F, 24.0F));
        ImGui::TextUnformatted(m_confirmationMessage.c_str());
        if (m_imguiLayer->GetBoldFont() != nullptr) {
            ImGui::PopFont();
        }

        constexpr float btnW = 130.0F;
        constexpr float btnH = 44.0F;
        constexpr float btnGap = 24.0F;
        const float btnsY = dlgHeight - btnH - 22.0F;
        const float btnsTotal = btnW * 2.0F + btnGap;
        const float btnsX = (dlgWidth - btnsTotal) * 0.5F;

        ImGui::SetCursorPos(ImVec2(btnsX, btnsY));
        if (ImGui::Button("Yes (Y)", ImVec2(btnW, btnH))) {
            m_audio.playMenuClick();
            if (m_confirmationCallback) {
                m_confirmationCallback();
            }
            hideConfirmation();
        }
        ImGui::SetCursorPos(ImVec2(btnsX + btnW + btnGap, btnsY));
        if (ImGui::Button("No (N)", ImVec2(btnW, btnH))) {
            m_audio.playMenuClick();
            hideConfirmation();
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // Settings window layers on top of everything else when open.
    drawPauseSettingsPanel();
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
        m_selectedButtonIndex = (m_selectedButtonIndex + 1) % static_cast<int32_t>(m_buttons.size());

        // Skip disabled buttons
        int32_t attempts = 0;
        while (!m_buttons[static_cast<size_t>(m_selectedButtonIndex)].enabled &&
               attempts < static_cast<int32_t>(m_buttons.size())) {
            m_selectedButtonIndex = (m_selectedButtonIndex + 1) % static_cast<int32_t>(m_buttons.size());
            attempts++;
        }

        m_audio.playMenuHover();
    }

    if (m_input.isKeyPressed(Engine::Input::Key::Up)) {
        m_selectedButtonIndex = (m_selectedButtonIndex - 1 + static_cast<int32_t>(m_buttons.size())) %
                                static_cast<int32_t>(m_buttons.size());

        // Skip disabled buttons
        int32_t attempts = 0;
        while (!m_buttons[static_cast<size_t>(m_selectedButtonIndex)].enabled &&
               attempts < static_cast<int32_t>(m_buttons.size())) {
            m_selectedButtonIndex = (m_selectedButtonIndex - 1 + static_cast<int32_t>(m_buttons.size())) %
                                    static_cast<int32_t>(m_buttons.size());
            attempts++;
        }

        m_audio.playMenuHover();
    }

    // Activate button with Enter or Space
    if (m_input.isKeyPressed(Engine::Input::Key::Enter) ||
        m_input.isKeyPressed(Engine::Input::Key::Space)) {

        if (m_buttons[static_cast<size_t>(m_selectedButtonIndex)].enabled) {
            m_audio.playMenuClick();
            if (m_buttons[static_cast<size_t>(m_selectedButtonIndex)].callback) {
                m_buttons[static_cast<size_t>(m_selectedButtonIndex)].callback();
            }
        }
    }

    // Mouse click
    if (m_input.isMouseButtonPressed(Engine::Input::MouseButton::Left)) {
        if (m_hoveredButtonIndex >= 0 &&
            m_hoveredButtonIndex < static_cast<int32_t>(m_buttons.size())) {

            auto& button = m_buttons[static_cast<size_t>(m_hoveredButtonIndex)];
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
    Engine::f64 mouseX = 0.0;
    Engine::f64 mouseY = 0.0;
    m_input.getMousePosition(mouseX, mouseY);

    int32_t previousHoveredIndex = m_hoveredButtonIndex;
    m_hoveredButtonIndex = -1;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        auto& button = m_buttons[i];

        // Check if mouse is over button
        bool isOver = mouseX >= static_cast<double>(button.position[0]) &&
                      mouseX <= static_cast<double>(button.position[0] + button.size[0]) &&
                      mouseY >= static_cast<double>(button.position[1]) &&
                      mouseY <= static_cast<double>(button.position[1] + button.size[1]);

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

void PauseMenu::renderBackground(CatEngine::Renderer::UIPass& uiPass) {
    // Semi-transparent overlay to dim the game
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(m_screenWidth);
    overlay.height = static_cast<float>(m_screenHeight);
    overlay.r = 0.0F;
    overlay.g = 0.0F;
    overlay.b = 0.0F;
    overlay.a = 0.7F;
    overlay.depth = 0.0F;
    overlay.texture = nullptr;
    uiPass.DrawQuad(overlay);

    // Panel background
    float panelWidth = 350.0F;
    float panelHeight = 400.0F;
    float panelX = (static_cast<float>(m_screenWidth) - panelWidth) / 2.0F;
    float panelY = (static_cast<float>(m_screenHeight) - panelHeight) / 2.0F;

    CatEngine::Renderer::UIPass::QuadDesc panel;
    panel.x = panelX;
    panel.y = panelY;
    panel.width = panelWidth;
    panel.height = panelHeight;
    panel.r = 0.15F;
    panel.g = 0.15F;
    panel.b = 0.2F;
    panel.a = 0.95F;
    panel.depth = 0.1F;
    panel.texture = nullptr;
    uiPass.DrawQuad(panel);
}

void PauseMenu::renderTitle(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;
    float titleY = static_cast<float>(m_screenHeight) * 0.25F;

    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = "PAUSED";
    titleText.x = centerX - 80.0F;
    titleText.y = titleY;
    titleText.fontSize = 42.0F;
    titleText.r = 1.0F;
    titleText.g = 1.0F;
    titleText.b = 1.0F;
    titleText.a = 1.0F;
    titleText.depth = 0.2F;
    titleText.fontAtlas = nullptr;
    uiPass.DrawText(titleText);
}

void PauseMenu::renderButtons(CatEngine::Renderer::UIPass& uiPass) {
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const auto& button = m_buttons[i];

        // Button background color
        float bgR = 0.3F;
        float bgG = 0.3F;
        float bgB = 0.4F;
        float bgA = 0.8F;

        if (!button.enabled) {
            bgR = 0.2F;
            bgG = 0.2F;
            bgB = 0.2F;
            bgA = 0.5F;
        } else if (button.hovered || static_cast<int32_t>(i) == m_selectedButtonIndex) {
            bgR = 0.5F;
            bgG = 0.5F;
            bgB = 0.7F;
            bgA = 0.9F;
        }

        // Button background
        CatEngine::Renderer::UIPass::QuadDesc buttonBg;
        buttonBg.x = button.position[0];
        buttonBg.y = button.position[1];
        buttonBg.width = button.size[0];
        buttonBg.height = button.size[1];
        buttonBg.r = bgR;
        buttonBg.g = bgG;
        buttonBg.b = bgB;
        buttonBg.a = bgA;
        buttonBg.depth = 0.3F;
        buttonBg.texture = nullptr;
        uiPass.DrawQuad(buttonBg);

        // Selection border
        if (static_cast<int32_t>(i) == m_selectedButtonIndex) {
            float borderWidth = 2.0F;

            // All four borders
            CatEngine::Renderer::UIPass::QuadDesc border;
            border.r = 1.0F;
            border.g = 1.0F;
            border.b = 1.0F;
            border.a = 1.0F;
            border.depth = 0.35F;
            border.texture = nullptr;

            // Top
            border.x = button.position[0];
            border.y = button.position[1];
            border.width = button.size[0];
            border.height = borderWidth;
            uiPass.DrawQuad(border);

            // Bottom
            border.y = button.position[1] + button.size[1] - borderWidth;
            uiPass.DrawQuad(border);

            // Left
            border.x = button.position[0];
            border.y = button.position[1];
            border.width = borderWidth;
            border.height = button.size[1];
            uiPass.DrawQuad(border);

            // Right
            border.x = button.position[0] + button.size[0] - borderWidth;
            uiPass.DrawQuad(border);
        }

        // Button text
        float textAlpha = button.enabled ? 1.0F : 0.5F;

        CatEngine::Renderer::UIPass::TextDesc buttonText;
        buttonText.text = button.text.c_str();
        buttonText.x = button.position[0] + (button.size[0] / 2.0F) - (static_cast<float>(button.text.length()) * 5.0F);
        buttonText.y = button.position[1] + (button.size[1] / 2.0F) - 10.0F;
        buttonText.fontSize = 20.0F;
        buttonText.r = 1.0F;
        buttonText.g = 1.0F;
        buttonText.b = 1.0F;
        buttonText.a = textAlpha;
        buttonText.depth = 0.4F;
        buttonText.fontAtlas = nullptr;
        uiPass.DrawText(buttonText);
    }
}

void PauseMenu::renderConfirmationDialog(CatEngine::Renderer::UIPass& uiPass) {
    // Additional darkening overlay
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(m_screenWidth);
    overlay.height = static_cast<float>(m_screenHeight);
    overlay.r = 0.0F;
    overlay.g = 0.0F;
    overlay.b = 0.0F;
    overlay.a = 0.5F;
    overlay.depth = 0.5F;
    overlay.texture = nullptr;
    uiPass.DrawQuad(overlay);

    // Dialog box
    float dialogWidth = 400.0F;
    float dialogHeight = 150.0F;
    float dialogX = (static_cast<float>(m_screenWidth) - dialogWidth) / 2.0F;
    float dialogY = (static_cast<float>(m_screenHeight) - dialogHeight) / 2.0F;

    CatEngine::Renderer::UIPass::QuadDesc dialog;
    dialog.x = dialogX;
    dialog.y = dialogY;
    dialog.width = dialogWidth;
    dialog.height = dialogHeight;
    dialog.r = 0.2F;
    dialog.g = 0.2F;
    dialog.b = 0.3F;
    dialog.a = 1.0F;
    dialog.depth = 0.6F;
    dialog.texture = nullptr;
    uiPass.DrawQuad(dialog);

    // Border
    float borderWidth = 2.0F;
    CatEngine::Renderer::UIPass::QuadDesc border;
    border.r = 1.0F;
    border.g = 1.0F;
    border.b = 1.0F;
    border.a = 1.0F;
    border.depth = 0.65F;
    border.texture = nullptr;

    border.x = dialogX;
    border.y = dialogY;
    border.width = dialogWidth;
    border.height = borderWidth;
    uiPass.DrawQuad(border);

    border.y = dialogY + dialogHeight - borderWidth;
    uiPass.DrawQuad(border);

    border.x = dialogX;
    border.y = dialogY;
    border.width = borderWidth;
    border.height = dialogHeight;
    uiPass.DrawQuad(border);

    border.x = dialogX + dialogWidth - borderWidth;
    uiPass.DrawQuad(border);

    // Message text
    CatEngine::Renderer::UIPass::TextDesc messageText;
    messageText.text = m_confirmationMessage.c_str();
    messageText.x = dialogX + 20.0F;
    messageText.y = dialogY + 40.0F;
    messageText.fontSize = 18.0F;
    messageText.r = 1.0F;
    messageText.g = 1.0F;
    messageText.b = 1.0F;
    messageText.a = 1.0F;
    messageText.depth = 0.7F;
    messageText.fontAtlas = nullptr;
    uiPass.DrawText(messageText);

    // Hint text
    CatEngine::Renderer::UIPass::TextDesc hintText;
    hintText.text = "Y - Confirm    N - Cancel";
    hintText.x = dialogX + (dialogWidth / 2.0F) - 100.0F;
    hintText.y = dialogY + dialogHeight - 40.0F;
    hintText.fontSize = 14.0F;
    hintText.r = 0.8F;
    hintText.g = 0.8F;
    hintText.b = 0.8F;
    hintText.a = 1.0F;
    hintText.depth = 0.7F;
    hintText.fontAtlas = nullptr;
    uiPass.DrawText(hintText);
}

void PauseMenu::showConfirmation(const std::string& message, ButtonCallback onConfirm) {
    m_confirmationActive = true;
    m_confirmationMessage = message;
    m_confirmationCallback = std::move(onConfirm);
}

void PauseMenu::hideConfirmation() {
    m_confirmationActive = false;
    m_confirmationMessage.clear();
    m_confirmationCallback = nullptr;
}

bool PauseMenu::isMouseOverButton(const MenuButton& button) const {
    Engine::f64 mouseX = 0.0;
    Engine::f64 mouseY = 0.0;
    m_input.getMousePosition(mouseX, mouseY);

    return mouseX >= static_cast<double>(button.position[0]) &&
           mouseX <= static_cast<double>(button.position[0] + button.size[0]) &&
           mouseY >= static_cast<double>(button.position[1]) &&
           mouseY <= static_cast<double>(button.position[1] + button.size[1]);
}

} // namespace Game

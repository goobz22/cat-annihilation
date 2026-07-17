#include "MainMenu.hpp"
#include "../audio/GameAudio.hpp"
#include "../config/GameConfig.hpp"
#include "../../engine/audio/AudioEngine.hpp"
#include "../../engine/audio/AudioMixer.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/core/Window.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include "../../engine/ui/ImGuiLayer.hpp"

#include "imgui.h"

#include <cmath>

namespace Game {

// All pre-game strings and the fur-swatch table come from the web-parity
// table so MainMenu can never drift from the web reference on its own —
// see the "Pre-game menu" section of game/config/WebParityConfig.hpp.
namespace WebParity = CatGame::WebParity;

// ---------------------------------------------------------------------------
// Settings panel state
//
// The panel owns its open/closed flag here (a file-local singleton so the
// toggle survives across render() calls). Every other value lives in the
// GameConfig the MainMenu was bound to — the panel edits it live so any
// subsystem that reads GameConfig picks up the new values on the next
// query, and saveConfig writes them out when the user closes the panel.
// ---------------------------------------------------------------------------
namespace {

bool& panelOpenFlag() {
    static bool open = false;
    return open;
}

// Track the last applied values so we only push updates to the real
// subsystems on the frame they change. Reading back via getMasterVolume()
// each frame would be cheap but it's cleaner to only call the setters on
// a real edge — several audio drivers log on volume change, and we don't
// want to spam that log every frame.
struct LastApplied {
    float masterVolume     = -1.0F;
    float musicVolume      = -1.0F;
    float sfxVolume        = -1.0F;
    float mouseSensitivity = -1.0F;
    int   fullscreen       = -1;
    int   vsync            = -1;
    int   invertY          = -1;
};

LastApplied& lastApplied() {
    static LastApplied s;
    return s;
}

void drawSettingsPanel(GameAudio& audio,
                       Engine::Window* window,
                       CatEngine::Renderer::Renderer* renderer,
                       GameConfig* gameConfig) {
    bool& open = panelOpenFlag();
    if (!open) {
        return;
    }
    if (gameConfig == nullptr) {
        // Without a config to mutate, nothing would stick across frames —
        // and every one of the real subsystem setters below would have no
        // authoritative source to read from. Surface this once via ImGui
        // rather than silently draw a dead panel.
        ImGui::SetNextWindowSize(ImVec2(480.0F, 120.0F), ImGuiCond_Appearing);
        if (ImGui::Begin("Settings", &open)) {
            ImGui::TextWrapped("Settings are not wired to a GameConfig instance. "
                               "Call MainMenu::setSettingsBindings() during init.");
        }
        ImGui::End();
        return;
    }

    LastApplied& last = lastApplied();

    ImGui::SetNextWindowSize(ImVec2(480.0F, 360.0F), ImGuiCond_Appearing);
    if (ImGui::Begin("Settings", &open)) {
        ImGui::TextUnformatted("Audio");
        ImGui::Separator();

        ImGui::SliderFloat("Master Volume", &gameConfig->audio.masterVolume, 0.0F, 1.0F, "%.2f");
        if (gameConfig->audio.masterVolume != last.masterVolume) {
            audio.getMixer().setMasterVolume(gameConfig->audio.masterVolume);
            last.masterVolume = gameConfig->audio.masterVolume;
        }

        ImGui::SliderFloat("Music Volume", &gameConfig->audio.musicVolume, 0.0F, 1.0F, "%.2f");
        if (gameConfig->audio.musicVolume != last.musicVolume) {
            // GameAudio routes music through the Music mixer channel; the
            // mixer is the single source of truth for per-channel gain so
            // every music source (menu / gameplay / victory / defeat) picks
            // up the change on the next updateVolumes() pass.
            audio.getMixer().setChannelVolume(CatEngine::AudioMixer::Channel::Music,
                                              gameConfig->audio.musicVolume);
            last.musicVolume = gameConfig->audio.musicVolume;
        }

        ImGui::SliderFloat("SFX Volume", &gameConfig->audio.sfxVolume, 0.0F, 1.0F, "%.2f");
        if (gameConfig->audio.sfxVolume != last.sfxVolume) {
            audio.getMixer().setChannelVolume(CatEngine::AudioMixer::Channel::SFX,
                                              gameConfig->audio.sfxVolume);
            last.sfxVolume = gameConfig->audio.sfxVolume;
        }

        ImGui::Dummy(ImVec2(0.0F, 6.0F));
        ImGui::TextUnformatted("Input");
        ImGui::Separator();

        ImGui::SliderFloat("Mouse Sensitivity", &gameConfig->controls.mouseSensitivity,
                           0.1F, 2.0F, "%.2f");
        if (gameConfig->controls.mouseSensitivity != last.mouseSensitivity) {
            // The FPS camera controller reads controls.mouseSensitivity out
            // of GameConfig at input time, so updating the config is all
            // that's needed here — no setter call required.
            last.mouseSensitivity = gameConfig->controls.mouseSensitivity;
        }

        ImGui::Checkbox("Invert Y Axis", &gameConfig->controls.invertMouseY);
        const int invertYNow = gameConfig->controls.invertMouseY ? 1 : 0;
        if (invertYNow != last.invertY) {
            // Camera reads this flag from GameConfig per-frame — same
            // pattern as mouseSensitivity above.
            last.invertY = invertYNow;
        }

        ImGui::Dummy(ImVec2(0.0F, 6.0F));
        ImGui::TextUnformatted("Display");
        ImGui::Separator();

        ImGui::Checkbox("Fullscreen", &gameConfig->graphics.fullscreen);
        const int fullscreenNow = gameConfig->graphics.fullscreen ? 1 : 0;
        if (fullscreenNow != last.fullscreen && window != nullptr) {
            window->setFullscreen(gameConfig->graphics.fullscreen);
            last.fullscreen = fullscreenNow;
        }

        ImGui::Checkbox("VSync", &gameConfig->graphics.vsync);
        const int vsyncNow = gameConfig->graphics.vsync ? 1 : 0;
        if (vsyncNow != last.vsync && renderer != nullptr) {
            // Renderer::SetVSync recreates the swapchain so it's heavier
            // than the other toggles; only call it on the edge.
            renderer->SetVSync(gameConfig->graphics.vsync);
            last.vsync = vsyncNow;
        }

        ImGui::Dummy(ImVec2(0.0F, 12.0F));
        if (ImGui::Button("Close", ImVec2(120.0F, 0.0F))) {
            open = false;
            // Persist on close so settings survive a restart. The main
            // loop also saves on exit, but saving here means pre-quit
            // crashes don't lose the user's tweaks.
            gameConfig->save("config.json");
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

    // Create the mode-select page's button list. The two web mode cards
    // come first, in the web's order (GameModeSelection.tsx:316-354), then
    // the native-desktop extras (Continue / Settings / Quit) the web has
    // no equivalent for. Reusing the MenuButton list for the cards —
    // rather than drawing the page ad hoc — keeps ONE model that the
    // keyboard navigation (m_selectedButtonIndex in handleInput) and the
    // hover hit-testing (updateButtons) already operate on; the cards
    // only differ visually, which render handles via the subtitle/hint
    // fields. Geometry is intentionally not set here: render() writes the
    // real on-screen ImGui rect back into each button every frame.
    m_buttons.clear();

    // Survival Mode card — web parity: clicking it opens the customize
    // screen (GameModeSelection.tsx:46-49 handleSurvivalMode sets
    // showCustomization); the run itself only starts from that page's
    // START GAME, so this callback just flips the page.
    MenuButton survivalButton;
    survivalButton.text = WebParity::kSurvivalCardTitle;
    survivalButton.subtitle = WebParity::kSurvivalCardSubtitle;
    survivalButton.enabled = true;
    survivalButton.callback = [this]() {
        m_currentPage = MenuPage::Customize;
    };
    m_buttons.push_back(survivalButton);

    // Story Mode card — present so the menu mirrors the web's two-card
    // layout, but disabled: story mode is P3-deferred until survival is
    // 1:1 (docs/parity/PARITY_MATRIX.md), so the card renders greyed with
    // the hint below and no callback.
    MenuButton storyButton;
    storyButton.text = WebParity::kStoryCardTitle;
    storyButton.subtitle = WebParity::kStoryCardSubtitle;
    storyButton.hint = "Coming soon";
    storyButton.enabled = false;
    m_buttons.push_back(storyButton);

    // Continue button
    MenuButton continueButton;
    continueButton.text = "Continue";
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
    settingsButton.enabled = true;
    settingsButton.callback = [this]() {
        if (m_settingsCallback) {
            m_settingsCallback();
        } else {
            // Toggle the in-menu settings window. A consumer-supplied callback
            // (e.g. to open a dedicated scene) still takes precedence.
            panelOpenFlag() = !panelOpenFlag();
        }
    };
    m_buttons.push_back(settingsButton);

    // Quit button
    MenuButton quitButton;
    quitButton.text = "Quit";
    quitButton.enabled = true;
    quitButton.callback = [this]() {
        if (m_quitCallback) {
            m_quitCallback();
        }
    };
    m_buttons.push_back(quitButton);

    m_selectedButtonIndex = 0;
    m_currentPage = MenuPage::ModeSelect;

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

    // Update animations (drives the starfield drift/twinkle in
    // renderBackground; the ImGui text needs no per-frame animation state)
    m_backgroundAnimTimer += deltaTime * 0.5F;

    // Update button states — mode-select page only. On the customize page
    // the MenuButton list isn't drawn, so hit-testing its (now off-screen)
    // rects would fire phantom hover sounds while the mouse crosses where
    // the cards used to be.
    if (m_currentPage == MenuPage::ModeSelect) {
        updateButtons();
    }
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

    // ------------------------------------------------------------------- Pages
    // The pre-game flow is two pages inside the one overlay window,
    // mirroring the web's showCustomization branch
    // (GameModeSelection.tsx:175/308). The background, version footer, and
    // settings panel below are shared by both pages.
    if (m_currentPage == MenuPage::ModeSelect) {
        renderModeSelectPage(width, height);
    } else {
        renderCustomizePage(width, height);
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
    drawSettingsPanel(m_audio, m_settingsWindow, m_settingsRenderer, m_settingsConfig);
}

void MainMenu::handleInput() {
    if (!m_initialized) {
        return;
    }

    // ---- Customize page ----------------------------------------------------
    // Mouse interaction lives entirely in renderCustomizePage's ImGui
    // widgets; the keyboard gets the two page-level actions. This branch
    // MUST run before the mode-select handling below (with an early
    // return): if it were checked after, the very Enter press that
    // activates the Survival card — which flips m_currentPage — would
    // fall through and instantly START the game in the same frame.
    if (m_currentPage == MenuPage::Customize) {
        // ESC is free in the MainMenu game state (GameUI only consumes it
        // in Playing/Paused), so it maps naturally onto the web's "< Back".
        if (m_input.isKeyPressed(Engine::Input::Key::Escape)) {
            m_audio.playMenuClick();
            m_currentPage = MenuPage::ModeSelect;
        } else if (m_input.isKeyPressed(Engine::Input::Key::Enter) ||
                   m_input.isKeyPressed(Engine::Input::Key::Space)) {
            m_audio.playMenuClick();
            confirmStartGame();
        }
        return;
    }

    // ---- Mode-select page --------------------------------------------------

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

    // Mouse activation is deliberately NOT handled here. ImGui::Button in
    // renderModeSelectPage already fires the callback on click, and now
    // that render() writes the real on-screen rects back into each
    // MenuButton (so updateButtons' hover detection is truthful), a
    // second Engine::Input-driven click path would double-activate every
    // button: once on press (edge-triggered isMouseButtonPressed) and
    // again on release (ImGui's click semantics). For toggles like
    // Settings that double-fire is a visible bug — the panel would open
    // and instantly close on one click.
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

void MainMenu::renderModeSelectPage(float width, float height) {
    // ------------------------------------------------------------------ Title
    // Web parity: the web's menu heads itself "Cat Warriors" / "Choose
    // your adventure" (GameModeSelection.tsx:312-313) — deliberately NOT
    // the app name; the strings live in the parity table.
    if (auto* titleFont = m_imguiLayer->GetTitleFont()) {
        ImGui::PushFont(titleFont);
    }
    const char* titleText = WebParity::kMenuHeading;
    const ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    const float titleY = height * 0.13F;
    ImGui::SetCursorPos(ImVec2((width - titleSize.x) * 0.5F, titleY));
    ImGui::TextColored(ImVec4(1.00F, 0.80F, 0.10F, 1.00F), "%s", titleText);
    if (m_imguiLayer->GetTitleFont() != nullptr) {
        ImGui::PopFont();
    }

    // --------------------------------------------------------------- Subtitle
    if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
        ImGui::PushFont(regularFont);
    }
    const char* subtitleText = WebParity::kMenuSubheading;
    const ImVec2 subSize = ImGui::CalcTextSize(subtitleText);
    ImGui::SetCursorPos(ImVec2((width - subSize.x) * 0.5F, titleY + titleSize.y + 4.0F));
    ImGui::TextColored(ImVec4(0.80F, 0.80F, 0.90F, 0.90F), "%s", subtitleText);
    if (m_imguiLayer->GetRegularFont() != nullptr) {
        ImGui::PopFont();
    }

    // ----------------------------------------------------------------- Buttons
    // One MenuButton model, two visual treatments: the mode cards
    // (subtitle set) draw a taller ID-only button with title / subtitle /
    // hint overlaid, the native-desktop extras keep ImGui's own centered
    // label — exactly the split between the web's .game-mode-option cards
    // and plain buttons.
    if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
        ImGui::PushFont(boldFont);
    }
    const float buttonWidth = 360.0F;
    const float plainButtonHeight = 60.0F;
    const float cardButtonHeight = 96.0F;
    const float buttonSpacing = 16.0F;
    float cursorY = height * 0.32F;
    const float buttonX = (width - buttonWidth) * 0.5F;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        auto& button = m_buttons[i];
        const bool isCard = !button.subtitle.empty();
        const float buttonHeight = isCard ? cardButtonHeight : plainButtonHeight;

        ImGui::SetCursorPos(ImVec2(buttonX, cursorY));
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!button.enabled);
        // Cards use an ID-only label so the overlay text below fully
        // controls the typography (two fonts on one button is beyond
        // ImGui::Button's single label).
        const bool clicked = ImGui::Button(isCard ? "##card" : button.text.c_str(),
                                           ImVec2(buttonWidth, buttonHeight));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();
        ImGui::EndDisabled();
        ImGui::PopID();

        if (clicked) {
            m_audio.playMenuClick();
            if (button.callback) {
                button.callback();
            }
        }

        // Write the real drawn geometry back into the model so
        // updateButtons() hover-tests what is actually on screen (the same
        // sync PauseMenu::render does). Item-rect coords are absolute
        // screen coords, which match Engine::Input's window-relative mouse
        // because this overlay window fills the screen from (0,0).
        button.position = {rectMin.x, rectMin.y};
        button.size = {rectMax.x - rectMin.x, rectMax.y - rectMin.y};

        button.hovered = hovered;
        if (hovered) {
            m_hoveredButtonIndex = static_cast<int32_t>(i);
        }

        // Keyboard-selection ring: handleInput moves m_selectedButtonIndex
        // with Up/Down, and without a visual that path is unusable. Gold
        // matches the selection border this menu used pre-ImGui.
        if (static_cast<int32_t>(i) == m_selectedButtonIndex && button.enabled) {
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(rectMin.x - 3.0F, rectMin.y - 3.0F),
                ImVec2(rectMax.x + 3.0F, rectMax.y + 3.0F),
                IM_COL32(255, 204, 26, 255), 4.0F, 0, 3.0F);
        }

        // Card overlay text, drawn after the button so it layers on top;
        // Text items carry no ID so they never steal the button's hover.
        // Colors are explicit rather than BeginDisabled-driven so the
        // disabled story card greys its text to match its button face.
        if (isCard) {
            const ImVec4 titleColor = button.enabled
                ? ImVec4(1.00F, 1.00F, 1.00F, 1.00F)
                : ImVec4(0.55F, 0.55F, 0.60F, 0.85F);
            const ImVec4 subtitleColor = button.enabled
                ? ImVec4(0.80F, 0.80F, 0.90F, 0.90F)
                : ImVec4(0.50F, 0.50F, 0.55F, 0.80F);

            const ImVec2 cardTitleSize = ImGui::CalcTextSize(button.text.c_str());
            ImGui::SetCursorPos(ImVec2(buttonX + (buttonWidth - cardTitleSize.x) * 0.5F,
                                       cursorY + 14.0F));
            ImGui::TextColored(titleColor, "%s", button.text.c_str());

            if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
                ImGui::PushFont(regularFont);
            }
            const ImVec2 cardSubSize = ImGui::CalcTextSize(button.subtitle.c_str());
            ImGui::SetCursorPos(ImVec2(buttonX + (buttonWidth - cardSubSize.x) * 0.5F,
                                       cursorY + 14.0F + cardTitleSize.y + 4.0F));
            ImGui::TextColored(subtitleColor, "%s", button.subtitle.c_str());

            if (!button.hint.empty()) {
                // Amber so "Coming soon" reads as a status tag, not body copy.
                const ImVec2 hintSize = ImGui::CalcTextSize(button.hint.c_str());
                ImGui::SetCursorPos(
                    ImVec2(buttonX + (buttonWidth - hintSize.x) * 0.5F,
                           cursorY + buttonHeight - hintSize.y - 8.0F));
                ImGui::TextColored(ImVec4(0.85F, 0.70F, 0.30F, 0.90F), "%s",
                                   button.hint.c_str());
            }
            if (m_imguiLayer->GetRegularFont() != nullptr) {
                ImGui::PopFont();
            }
        }

        cursorY += buttonHeight + buttonSpacing;
    }
    if (m_imguiLayer->GetBoldFont() != nullptr) {
        ImGui::PopFont();
    }
}

void MainMenu::renderCustomizePage(float width, float height) {
    // ------------------------------------------------------------------ Title
    // Web parity: "Customize Your Cat" / "Survival Warrior"
    // (GameModeSelection.tsx:179-180, survival path). Same heading
    // treatment as the mode-select page so the two pages read as one menu.
    if (auto* titleFont = m_imguiLayer->GetTitleFont()) {
        ImGui::PushFont(titleFont);
    }
    const char* titleText = WebParity::kCustomizeHeading;
    const ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    const float titleY = height * 0.13F;
    ImGui::SetCursorPos(ImVec2((width - titleSize.x) * 0.5F, titleY));
    ImGui::TextColored(ImVec4(1.00F, 0.80F, 0.10F, 1.00F), "%s", titleText);
    if (m_imguiLayer->GetTitleFont() != nullptr) {
        ImGui::PopFont();
    }

    if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
        ImGui::PushFont(regularFont);
    }
    const char* subtitleText = WebParity::kCustomizeSubheading;
    const ImVec2 subSize = ImGui::CalcTextSize(subtitleText);
    ImGui::SetCursorPos(ImVec2((width - subSize.x) * 0.5F, titleY + titleSize.y + 4.0F));
    ImGui::TextColored(ImVec4(0.80F, 0.80F, 0.90F, 0.90F), "%s", subtitleText);
    if (m_imguiLayer->GetRegularFont() != nullptr) {
        ImGui::PopFont();
    }

    // ------------------------------------------------------------ Fur swatches
    // The web's fur picker (GameModeSelection.tsx:196-209) renders
    // colors.fur as a grid of unlabeled color buttons with the current
    // choice outlined. Same here: a 5x2 grid of ImGui color buttons over
    // the parity table's kFurSwatches, gold ring on the selection.
    constexpr int kSwatchColumns = 5;
    constexpr float kSwatchSize = 56.0F;
    constexpr float kSwatchGap = 14.0F;
    const float gridWidth = static_cast<float>(kSwatchColumns) * kSwatchSize +
                            static_cast<float>(kSwatchColumns - 1) * kSwatchGap;
    const float gridX = (width - gridWidth) * 0.5F;
    const float gridY = height * 0.40F;

    if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
        ImGui::PushFont(boldFont);
    }
    const char* furLabel = "FUR COLOR";
    const ImVec2 furLabelSize = ImGui::CalcTextSize(furLabel);
    ImGui::SetCursorPos(ImVec2((width - furLabelSize.x) * 0.5F,
                               gridY - furLabelSize.y - 14.0F));
    ImGui::TextColored(ImVec4(1.00F, 0.80F, 0.10F, 1.00F), "%s", furLabel);
    if (m_imguiLayer->GetBoldFont() != nullptr) {
        ImGui::PopFont();
    }

    for (int i = 0; i < WebParity::kFurSwatchCount; ++i) {
        const auto& swatch = WebParity::kFurSwatches[i];
        const int column = i % kSwatchColumns;
        const int row = i / kSwatchColumns;
        ImGui::SetCursorPos(
            ImVec2(gridX + static_cast<float>(column) * (kSwatchSize + kSwatchGap),
                   gridY + static_cast<float>(row) * (kSwatchSize + kSwatchGap)));

        // ImGui hands style/widget colors to the backend as-authored (no
        // color-space conversion), so the swatch face takes the raw web
        // sRGB bytes and displays exactly the web hex. Only the value fed
        // onward to the tint push constant is linear-decoded — that's
        // getSelectedFurLinear's job, not the preview's.
        const ImVec4 faceColor(static_cast<float>(swatch.red) / 255.0F,
                               static_cast<float>(swatch.green) / 255.0F,
                               static_cast<float>(swatch.blue) / 255.0F,
                               1.0F);
        ImGui::PushID(i);
        if (ImGui::ColorButton("##fur", faceColor,
                               ImGuiColorEditFlags_NoTooltip |
                                   ImGuiColorEditFlags_NoDragDrop |
                                   ImGuiColorEditFlags_NoAlpha,
                               ImVec2(kSwatchSize, kSwatchSize))) {
            m_audio.playMenuClick();
            m_selectedFurIndex = i;
        }
        // Our own tooltip (the swatch's parity-table name) instead of
        // ColorButton's default r/g/b readout, which is picker chrome
        // that means nothing to a player.
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", swatch.name);
        }
        ImGui::PopID();

        // Selection ring — the ImGui take on the web's `.selected` outline
        // (GameModeSelection.tsx:203). Same gold as the keyboard ring on
        // the mode-select page.
        if (i == m_selectedFurIndex) {
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(rectMin.x - 3.0F, rectMin.y - 3.0F),
                ImVec2(rectMax.x + 3.0F, rectMax.y + 3.0F),
                IM_COL32(255, 204, 26, 255), 4.0F, 0, 3.0F);
        }
    }

    // Selected swatch name. The web shows the choice on a live 3D cat
    // preview beside the grid; the native menu draws no 3D scene (a noted
    // parity delta), so naming the selection is the stand-in feedback.
    if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
        ImGui::PushFont(regularFont);
    }
    const char* selectedName = WebParity::kFurSwatches[m_selectedFurIndex].name;
    const ImVec2 nameSize = ImGui::CalcTextSize(selectedName);
    const float nameY = gridY + 2.0F * kSwatchSize + kSwatchGap + 16.0F;
    ImGui::SetCursorPos(ImVec2((width - nameSize.x) * 0.5F, nameY));
    ImGui::TextColored(ImVec4(0.80F, 0.80F, 0.90F, 0.90F), "%s", selectedName);
    if (m_imguiLayer->GetRegularFont() != nullptr) {
        ImGui::PopFont();
    }

    // ----------------------------------------------------------------- Actions
    // Mirrors the web's footer pair — "← Back" (GameModeSelection.tsx:291)
    // and "Start Game" (tsx:304); uppercase to match this menu's native
    // button voice.
    if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
        ImGui::PushFont(boldFont);
    }
    constexpr float kBackWidth = 170.0F;
    constexpr float kStartWidth = 230.0F;
    constexpr float kActionHeight = 56.0F;
    constexpr float kActionGap = 24.0F;
    const float actionsY = nameY + nameSize.y + 32.0F;
    const float actionsX = (width - (kBackWidth + kActionGap + kStartWidth)) * 0.5F;

    ImGui::SetCursorPos(ImVec2(actionsX, actionsY));
    if (ImGui::Button("< BACK", ImVec2(kBackWidth, kActionHeight))) {
        m_audio.playMenuClick();
        m_currentPage = MenuPage::ModeSelect;
    }

    ImGui::SetCursorPos(ImVec2(actionsX + kBackWidth + kActionGap, actionsY));
    if (ImGui::Button("START GAME", ImVec2(kStartWidth, kActionHeight))) {
        m_audio.playMenuClick();
        confirmStartGame();
    }
    if (m_imguiLayer->GetBoldFont() != nullptr) {
        ImGui::PopFont();
    }
}

void MainMenu::confirmStartGame() {
    // Latch the choice BEFORE firing the callback: the game layer reads
    // hasSelectedFurColor() / getSelectedFurLinear() from inside its
    // start-game path to seed the player entity's tint.
    m_furColorConfirmed = true;
    // Reset to mode-select so returning to the menu later (death,
    // quit-to-menu) lands on the mode cards again — the web equivalent is
    // GameModeSelection remounting with fresh page state.
    m_currentPage = MenuPage::ModeSelect;
    if (m_startGameCallback) {
        m_startGameCallback();
    }
}

void MainMenu::getSelectedFurLinear(float& r, float& g, float& b) const {
    const auto& swatch = WebParity::kFurSwatches[m_selectedFurIndex];
    r = WebParity::srgbChannelToLinear(swatch.red);
    g = WebParity::srgbChannelToLinear(swatch.green);
    b = WebParity::srgbChannelToLinear(swatch.blue);
}

} // namespace Game

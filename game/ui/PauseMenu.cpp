#include "PauseMenu.hpp"
#include "../audio/GameAudio.hpp"
#include "../config/GameConfig.hpp"
#include "../../engine/audio/AudioEngine.hpp"
#include "../../engine/audio/AudioMixer.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/core/Window.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include "../../engine/ui/ImGuiLayer.hpp"

#include "imgui.h"

#include <cmath>   // std::round — snap the slider to the web's 0.1 step
#include <cstdio>  // std::snprintf — one-decimal value chip

namespace Game {

// The pause-modal ranges, copy and chrome colours all come from the
// web-parity table so PauseMenu can never drift from the web reference on its
// own — see the "Pause menu" section of game/config/WebParityConfig.hpp.
namespace WebParity = CatGame::WebParity;

// Settings panel — same wiring shape as MainMenu. The two menus keep
// independent open/closed flags (so closing the pause-menu settings
// doesn't also close the main-menu settings the next time a different
// build state opens it), but both mutate the shared GameConfig instance
// so edits in either screen persist and stay in sync.
namespace {

// Pack a web sRGB UiColor into an ImGui colour. Pause-modal chrome is handed
// to ImGui RAW (no srgb->linear decode) — the same UI-chrome path the menu /
// HUD colours use (see the color-space note in WebParityConfig.hpp) — so this
// is a straight byte copy plus an optional CSS rgba() alpha. Mirrors the
// identical helper in MainMenu.cpp; kept file-local (each UI TU owns its own)
// rather than exported, since it is a two-line packing detail, not shared API.
ImU32 toImCol(const WebParity::UiColor& color, float alpha = 1.0F) {
    return IM_COL32(color.red, color.green, color.blue,
                    static_cast<int>(alpha * 255.0F + 0.5F));
}

// ImGui's DrawList can round a rect OR gradient-fill it, never both. The web
// pause modal / control panels are subtle 145deg gradients on ROUNDED boxes,
// so we fill with the MIDPOINT of the two stops — visually indistinguishable
// from the #2d2d2d→#1a1a1a-class ramps at this scale while keeping the rounded
// corners the card look depends on. Same trick as MainMenu.cpp's midFill.
ImU32 midFill(const WebParity::UiColor& top, const WebParity::UiColor& bottom,
              float alpha = 1.0F) {
    return IM_COL32((top.red + bottom.red) / 2,
                    (top.green + bottom.green) / 2,
                    (top.blue + bottom.blue) / 2,
                    static_cast<int>(alpha * 255.0F + 0.5F));
}

bool& pausePanelOpenFlag() {
    static bool open = false;
    return open;
}

// Edge-trigger tracking identical to MainMenu.cpp — keep in sync with the
// setters there when adding new rows.
struct PauseLastApplied {
    float masterVolume     = -1.0F;
    float musicVolume      = -1.0F;
    float sfxVolume        = -1.0F;
    float mouseSensitivity = -1.0F;
    int   fullscreen       = -1;
    int   vsync            = -1;
    int   invertY          = -1;
};

PauseLastApplied& pauseLastApplied() {
    static PauseLastApplied s;
    return s;
}

void drawPauseSettingsPanel(GameAudio& audio,
                            Engine::Window* window,
                            CatEngine::Renderer::Renderer* renderer,
                            GameConfig* gameConfig) {
    bool& open = pausePanelOpenFlag();
    if (!open) {
        return;
    }
    if (gameConfig == nullptr) {
        ImGui::SetNextWindowSize(ImVec2(480.0F, 120.0F), ImGuiCond_Appearing);
        if (ImGui::Begin("Settings", &open)) {
            ImGui::TextWrapped("Settings are not wired to a GameConfig instance. "
                               "Call PauseMenu::setSettingsBindings() during init.");
        }
        ImGui::End();
        return;
    }

    PauseLastApplied& last = pauseLastApplied();

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
            last.mouseSensitivity = gameConfig->controls.mouseSensitivity;
        }

        ImGui::Checkbox("Invert Y Axis", &gameConfig->controls.invertMouseY);
        const int invertYNow = gameConfig->controls.invertMouseY ? 1 : 0;
        if (invertYNow != last.invertY) {
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
            renderer->SetVSync(gameConfig->graphics.vsync);
            last.vsync = vsyncNow;
        }

        ImGui::Dummy(ImVec2(0.0F, 12.0F));
        if (ImGui::Button("Close", ImVec2(120.0F, 0.0F))) {
            open = false;
            gameConfig->save("config.json");
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

    // ---------------------------------------------------------------------
    // Web-parity note — the pause menu is a DELIBERATE SUPERSET of the web's
    // (surfaced for docs/parity/PARITY_MATRIX.md "Deliberate divergences";
    // both sides DO have a pause menu — the earlier "web has none" hypothesis
    // was wrong, web PauseMenu.tsx:80-225).
    //
    // Web pause face (PauseMenu.tsx): title "PAUSED", a TURN SENSITIVITY
    // slider (0.1-2.0), a MOVEMENT SPEED slider (0.5-2.0), a read-only CONTROLS
    // list, and two buttons — "Resume Game" and "Quit Game" (a SOFT quit that
    // returns to the mode-select screen, handleQuitGame :59-76).
    //
    // Native keeps that and adds three things the web has no button for, all
    // recorded here as intentional divergences:
    //   • Restart Wave (re-run the current wave with confirmation),
    //   • a Settings sub-panel (Master/Music/SFX volume, mouse sensitivity,
    //     invert-Y, fullscreen, VSync) — the web has no in-pause audio/display
    //     settings at all, and
    //   • "Quit Game" == EXIT TO DESKTOP (a native app has a real process to
    //     quit; the browser build does not). The web's soft "Quit Game"
    //     (return to menu) maps onto native's separate "Main Menu" button.
    //
    // The web's two gameplay sliders are NOT reproduced here on purpose. They
    // scale the control scheme at runtime via a persisted multiplier
    // (spinSensitivity / moveSpeed, PauseMenu.tsx:8-16,43-53); the native
    // control scheme reads WebParity::kPlayerTurnSpeed / kPlayerWalkSpeed as
    // compile-time constants inside PlayerControlSystem (a different ownership
    // area) and GameConfig has no turn/move multiplier field, so a FUNCTIONAL
    // slider is a cross-cutting change owned elsewhere — not something this
    // menu can wire. Adding an INERT slider that visibly does nothing would
    // violate the repo's no-placeholder bar, so the parity path is tracked as
    // a follow-up rather than faked here. Only the exact web button label
    // "Resume Game" is adopted below.
    // ---------------------------------------------------------------------

    // Resume button — web label is exactly "Resume Game" (PauseMenu.tsx:204).
    MenuButton resumeButton;
    resumeButton.text = "Resume Game";
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
            pausePanelOpenFlag() = !pausePanelOpenFlag();
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

    // Under web parity the pause face is the rebuilt dark modal (the web
    // PauseMenu.tsx card), which owns its own dim overlay, sliders, controls
    // grid and Resume/Quit buttons. The legacy stacked-button layout below is
    // preserved verbatim for the !kEnabled native-flavor branch. This is a
    // compile-time branch (kEnabled is constexpr) so only one path is built.
    if constexpr (WebParity::kEnabled) {
        renderWebParityModal(width, height);
        return;
    }

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
    drawPauseSettingsPanel(m_audio, m_settingsWindow, m_settingsRenderer, m_settingsConfig);
}

void PauseMenu::renderWebParityModal(float width, float height) {
    namespace WP = WebParity;

    // Fonts, with the same title→bold→regular→default fallback MainMenu uses,
    // so an AddText never dereferences a null atlas pointer.
    ImFont* titleFont = m_imguiLayer->GetTitleFont();
    ImFont* boldFont = m_imguiLayer->GetBoldFont();
    ImFont* regularFont = m_imguiLayer->GetRegularFont();
    if (titleFont == nullptr) { titleFont = ImGui::GetFont(); }
    if (boldFont == nullptr) { boldFont = ImGui::GetFont(); }
    if (regularFont == nullptr) { regularFont = ImGui::GetFont(); }

    // ----------------------------------------------------- Full-screen window
    // A borderless, zero-padding window spanning the framebuffer. WindowBg is
    // transparent because we paint the dim overlay ourselves with the DrawList
    // (so we control its exact rgba(0,0,0,0.85) opacity); the window still
    // takes mouse input so the sliders and buttons inside are clickable.
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##PauseMenuOverlay", nullptr, kOverlayFlags);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Heavy dim of the live world behind the modal — rgba(0,0,0,0.85). The
    // orchestrator renders the scene while Paused, so this is what makes the
    // world read as "dimmed but present" rather than a black screen.
    dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(width, height),
                      toImCol(WP::kPauseOverlayColor,
                              static_cast<float>(WP::kPauseOverlayAlpha) / 255.0F));

    // Local text helpers — AddText with an explicit size lets us hit the web's
    // px scale off the atlas fonts (same technique as MainMenu).
    auto measure = [](ImFont* font, float size, const char* text) -> float {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0F, text).x;
    };
    auto drawText = [&](ImFont* font, float size, float x, float y,
                        const WP::UiColor& col, const char* text) {
        dl->AddText(font, size, ImVec2(x, y), toImCol(col), text);
    };
    auto drawCentered = [&](ImFont* font, float size, float centerX, float y,
                            const WP::UiColor& col, const char* text) {
        dl->AddText(font, size, ImVec2(centerX - measure(font, size, text) * 0.5F, y),
                    toImCol(col), text);
    };
    // A "key chip" — the small rounded #444/#333 pill the web wraps around a
    // key name (W A S D, SHIFT, ESC ...). Drawn right-anchored at rightX and
    // vertically centred on centerY; returns its left edge so callers can lay
    // out a run of chips. bg is passed so the controls grid (#444) and the
    // resume hint (#333) can share one drawer.
    auto drawKeyChipRight = [&](float rightX, float centerY, const char* text,
                                const WP::UiColor& bg, const WP::UiColor& fg) -> float {
        const float chipTextSize = 13.0F;
        const float textW = measure(regularFont, chipTextSize, text);
        const float chipW = textW + 14.0F;
        const float chipH = chipTextSize + 8.0F;
        const float chipX = rightX - chipW;
        const float chipY = centerY - chipH * 0.5F;
        dl->AddRectFilled(ImVec2(chipX, chipY), ImVec2(chipX + chipW, chipY + chipH),
                          toImCol(bg), 3.0F);
        dl->AddText(regularFont, chipTextSize,
                    ImVec2(chipX + 7.0F, chipY + 4.0F), toImCol(fg), text);
        return chipX;
    };

    // -------------------------------------------------------------- Card box
    // Fixed 500 px card (min(500px, 90vw), menus.css:16), centred. The height
    // is the sum of the bands below; kept as a single literal computed here so
    // the vertical centring stays correct if a band is retuned.
    const float cardW = std::min(500.0F, width * 0.9F);
    const float titleBarH = 52.0F;
    const float pad = 20.0F;          // .pause-content padding (menus.css:41)
    const float sectionTitleH = 20.0F;
    const float sectionGap = 10.0F;   // title → panel
    const float panelH = 52.0F;       // slider / grid inset height
    const float gridH = 76.0F;        // 2-row controls grid
    const float sectionMargin = 18.0F;// .pause-section margin-bottom (menus.css:45)
    const float buttonsH = 48.0F;
    const float buttonsMargin = 16.0F;// .pause-buttons margin-bottom (menus.css:142)
    const float hintH = 40.0F;

    const float contentH = pad +
        (sectionTitleH + sectionGap + panelH + sectionMargin) +   // TURN
        (sectionTitleH + sectionGap + panelH + sectionMargin) +   // MOVE
        (sectionTitleH + sectionGap + gridH + sectionMargin) +    // CONTROLS
        (buttonsH + buttonsMargin) +                              // buttons
        hintH + pad;
    const float cardH = titleBarH + contentH;

    const float cardX = (width - cardW) * 0.5F;
    const float cardY = std::max(20.0F, (height - cardH) * 0.5F);
    const ImVec2 cardMin(cardX, cardY);
    const ImVec2 cardMax(cardX + cardW, cardY + cardH);

    // Card fill (145deg #2d2d2d→#1a1a1a, midpoint) + 3px #444 border, 12px
    // rounded (menus.css:17-19).
    dl->AddRectFilled(cardMin, cardMax, midFill(WP::kPauseModalTop, WP::kPauseModalBottom),
                      12.0F);
    dl->AddRect(cardMin, cardMax, toImCol(WP::kPauseModalBorder), 12.0F, 0, 3.0F);

    // Title band — lighter #555→#333 with a 2px #666 bottom rule (menus.css:
    // 25-26), rounded only at the top so it hugs the card's top corners.
    const float titleBarBottom = cardY + titleBarH;
    dl->AddRectFilled(cardMin, ImVec2(cardMax.x, titleBarBottom),
                      midFill(WP::kPauseTitleBarTop, WP::kPauseTitleBarBottom),
                      12.0F, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(cardX, titleBarBottom - 2.0F),
                      ImVec2(cardX + cardW, titleBarBottom),
                      toImCol(WP::kPauseTitleBarRule));
    const float titleSize = 24.0F;   // clamp(18px,4vw,24px), menus.css:33
    drawCentered(titleFont, titleSize, cardX + cardW * 0.5F,
                 cardY + (titleBarH - titleSize) * 0.5F,
                 WP::kPauseTitleColor, WP::kPauseTitle);

    // Content column geometry.
    const float contentX = cardX + pad;
    const float contentW = cardW - pad * 2.0F;
    float cursorY = titleBarBottom + pad;

    // Draws one "section title + inset panel" and returns the panel's top Y so
    // the caller can lay the row's contents inside it.
    auto beginSection = [&](const char* title) -> float {
        drawText(boldFont, 15.0F, contentX, cursorY, WP::kPauseSectionTitleColor, title);
        cursorY += sectionTitleH + sectionGap;
        const float panelTop = cursorY;
        dl->AddRectFilled(ImVec2(contentX, panelTop),
                          ImVec2(contentX + contentW, panelTop + panelH),
                          midFill(WP::kPauseControlPanelTop, WP::kPauseControlPanelBottom),
                          6.0F);
        dl->AddRect(ImVec2(contentX, panelTop),
                    ImVec2(contentX + contentW, panelTop + panelH),
                    toImCol(WP::kPauseControlPanelBorder), 6.0F, 0, 1.0F);
        return panelTop;
    };

    // Draws a SLOW … [slider] … FAST [value] row inside a panel, wires the
    // ImGui slider to `value` and fires `onChange` (snapping to the web's 0.1
    // step). Returns nothing; advances cursorY past the section.
    auto drawSliderRow = [&](const char* sectionTitle, float* value,
                             float minV, float maxV, const ValueCallback& onChange) {
        const float panelTop = beginSection(sectionTitle);
        const float rowCenterY = panelTop + panelH * 0.5F;
        const float innerL = contentX + 12.0F;
        const float innerR = contentX + contentW - 12.0F;
        const float labelSize = 12.0F;

        // SLOW (left) and the value chip (far right) frame the slider.
        drawText(regularFont, labelSize, innerL, rowCenterY - labelSize * 0.5F,
                 WP::kPauseSensitivityLabelColor, WP::kPauseSlowLabel);
        const float slowRight = innerL + measure(regularFont, labelSize, WP::kPauseSlowLabel);

        // Value chip: fixed 48 px pill showing the one-decimal value.
        const float chipW = 48.0F;
        const float chipH = 24.0F;
        const float chipX = innerR - chipW;
        const float chipY = rowCenterY - chipH * 0.5F;
        dl->AddRectFilled(ImVec2(chipX, chipY), ImVec2(chipX + chipW, chipY + chipH),
                          toImCol(WP::kPauseValueChipBg), 4.0F);
        char valueText[8];
        std::snprintf(valueText, sizeof(valueText), "%.1f", static_cast<double>(*value));
        drawCentered(boldFont, 13.0F, chipX + chipW * 0.5F, chipY + 5.0F,
                     WP::kPauseValueChipText, valueText);

        // FAST label sits just left of the chip.
        const float fastSize = 12.0F;
        const float fastW = measure(regularFont, fastSize, WP::kPauseFastLabel);
        const float fastX = chipX - 10.0F - fastW;
        drawText(regularFont, fastSize, fastX, rowCenterY - fastSize * 0.5F,
                 WP::kPauseSensitivityLabelColor, WP::kPauseFastLabel);

        // The interactive ImGui slider spans between the SLOW and FAST labels.
        // It is styled to the web colours: the track is the #555 FrameBg and
        // the grab is the #4a90e2 thumb. ImGui's grab is a rounded rect, not
        // the web's filled-left-track + circular thumb, so this APPROXIMATES
        // the web look — the thumb colour and track colour match exactly, the
        // "blue fill to the left of the thumb" is not reproduced (a DrawList
        // slider would be needed for that; the mission calls for a styled
        // ImGui SliderFloat). The inline value text is hidden (format "")
        // because the chip above is the readout.
        const float sliderL = slowRight + 12.0F;
        const float sliderW = fastX - 12.0F - sliderL;
        ImGui::PushFont(regularFont);
        const float frameH = ImGui::GetFrameHeight();
        ImGui::SetCursorPos(ImVec2(sliderL, rowCenterY - frameH * 0.5F));
        ImGui::SetNextItemWidth(sliderW > 40.0F ? sliderW : 40.0F);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImColor(toImCol(WP::kPauseSliderTrack)).Value);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImColor(toImCol(WP::kPauseSliderTrack)).Value);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImColor(toImCol(WP::kPauseSliderTrack)).Value);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImColor(toImCol(WP::kPauseSliderThumb)).Value);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImColor(toImCol(WP::kPauseSliderFill)).Value);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 10.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0F);
        // Unique ID off the section title so the two sliders don't collide.
        ImGui::PushID(sectionTitle);
        if (ImGui::SliderFloat("##slider", value, minV, maxV, "")) {
            // Snap to the web's step=0.1 so the chip and the applied multiplier
            // move in the same 0.1 increments the <input step="0.1"> does.
            *value = std::round(*value * 10.0F) / 10.0F;
            if (*value < minV) { *value = minV; }
            if (*value > maxV) { *value = maxV; }
            if (onChange) { onChange(*value); }
        }
        ImGui::PopID();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
        ImGui::PopFont();

        cursorY = panelTop + panelH + sectionMargin;
    };

    drawSliderRow(WP::kPauseTurnSensitivityLabel, &m_turnSensitivity,
                  WP::kPauseTurnSensitivityMin, WP::kPauseTurnSensitivityMax,
                  m_turnSensitivityCallback);
    drawSliderRow(WP::kPauseMovementSpeedLabel, &m_moveSpeed,
                  WP::kPauseMoveSpeedMin, WP::kPauseMoveSpeedMax,
                  m_moveSpeedCallback);

    // ------------------------------------------------------------- CONTROLS
    // Section title + a 2x2 read-only grid: action label (left of each cell) +
    // a right-anchored key chip, matching the web's space-between rows.
    drawText(boldFont, 15.0F, contentX, cursorY, WP::kPauseSectionTitleColor,
             WP::kPauseControlsLabel);
    cursorY += sectionTitleH + sectionGap;
    const float gridTop = cursorY;
    dl->AddRectFilled(ImVec2(contentX, gridTop),
                      ImVec2(contentX + contentW, gridTop + gridH),
                      midFill(WP::kPauseControlPanelTop, WP::kPauseControlPanelBottom), 6.0F);
    dl->AddRect(ImVec2(contentX, gridTop),
                ImVec2(contentX + contentW, gridTop + gridH),
                toImCol(WP::kPauseControlPanelBorder), 6.0F, 0, 1.0F);
    {
        const float actionSize = 14.0F;
        const float leftActionX = contentX + 14.0F;
        const float leftKeyRight = contentX + contentW * 0.5F - 8.0F;
        const float rightActionX = contentX + contentW * 0.5F + 8.0F;
        const float rightKeyRight = contentX + contentW - 14.0F;
        const float row0Y = gridTop + gridH * 0.30F;
        const float row1Y = gridTop + gridH * 0.70F;

        auto cell = [&](float actionX, float keyRight, float rowY,
                        const char* action, const char* key) {
            drawText(regularFont, actionSize, actionX, rowY - actionSize * 0.5F,
                     WP::kPauseControlActionColor, action);
            drawKeyChipRight(keyRight, rowY, key,
                             WP::kPauseControlKeyBg, WP::kPauseControlKeyText);
        };
        cell(leftActionX, leftKeyRight, row0Y,
             WP::kPauseControlMoveAction, WP::kPauseControlMoveKey);
        cell(rightActionX, rightKeyRight, row0Y,
             WP::kPauseControlRunAction, WP::kPauseControlRunKey);
        cell(leftActionX, leftKeyRight, row1Y,
             WP::kPauseControlAttackAction, WP::kPauseControlAttackKey);
        cell(rightActionX, rightKeyRight, row1Y,
             WP::kPauseControlSlotsAction, WP::kPauseControlSlotsKey);
    }
    cursorY = gridTop + gridH + sectionMargin;

    // -------------------------------------------------------------- Buttons
    // Green Resume + red Quit, side by side. Real ImGui buttons (so the mouse
    // clicks and hovers work); each gets a 4px drop-shadow drawn behind it via
    // the DrawList to approximate the web's `box-shadow: 0 4px 0 <dark>` raised
    // look. Under parity the red "Quit Game" is the web's SOFT quit — it
    // returns to the mode-select screen and resets the run — so it fires
    // m_mainMenuCallback (wired to CatAnnihilation::quitToMenu), NOT the
    // desktop-exit m_quitCallback the legacy layout uses.
    const float buttonGap = 12.0F;
    const float buttonW = (contentW - buttonGap) * 0.5F;
    const float buttonsTop = cursorY;
    const float shadowDrop = 4.0F;

    auto drawShadow = [&](float x, const WP::UiColor& shadow) {
        dl->AddRectFilled(ImVec2(x, buttonsTop + shadowDrop),
                          ImVec2(x + buttonW, buttonsTop + buttonsH + shadowDrop),
                          toImCol(shadow), 6.0F);
    };
    drawShadow(contentX, WP::kPauseResumeShadow);
    drawShadow(contentX + buttonW + buttonGap, WP::kPauseQuitShadow);

    ImGui::PushFont(boldFont);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));

    // Resume (green).
    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(toImCol(WP::kPauseResumeTop)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(toImCol(WP::kPauseResumeHover)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(toImCol(WP::kPauseResumeBottom)).Value);
    ImGui::SetCursorPos(ImVec2(contentX, buttonsTop));
    if (ImGui::Button(WP::kPauseResumeLabel, ImVec2(buttonW, buttonsH))) {
        m_audio.playMenuClick();
        if (m_resumeCallback) { m_resumeCallback(); }
    }
    ImGui::PopStyleColor(3);

    // Quit (red) → soft quit to mode-select (m_mainMenuCallback).
    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(toImCol(WP::kPauseQuitTop)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(toImCol(WP::kPauseQuitHover)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(toImCol(WP::kPauseQuitBottom)).Value);
    ImGui::SetCursorPos(ImVec2(contentX + buttonW + buttonGap, buttonsTop));
    if (ImGui::Button(WP::kPauseQuitLabel, ImVec2(buttonW, buttonsH))) {
        m_audio.playMenuClick();
        if (m_mainMenuCallback) { m_mainMenuCallback(); }
    }
    ImGui::PopStyleColor(3);

    ImGui::PopStyleColor();   // Text
    ImGui::PopStyleVar();     // FrameRounding
    ImGui::PopFont();

    cursorY = buttonsTop + buttonsH + buttonsMargin;

    // ---------------------------------------------------------- Instructions
    // A top rule, then "Press [ESC] or [P] to resume" centred, with ESC and P
    // as #333 key-hint chips. Widths are measured up front so the whole run
    // centres as one unit.
    dl->AddRectFilled(ImVec2(contentX, cursorY),
                      ImVec2(contentX + contentW, cursorY + 1.0F),
                      toImCol(WP::kPauseInstructionsRule));
    const float hintSize = 14.0F;
    const float hintTextY = cursorY + 14.0F;
    const float chipTextSize = 13.0F;
    auto chipWidth = [&](const char* text) {
        return measure(regularFont, chipTextSize, text) + 14.0F;
    };
    const float prefixW = measure(regularFont, hintSize, WP::kPauseHintPrefix);
    const float orW = measure(regularFont, hintSize, WP::kPauseHintOr);
    const float suffixW = measure(regularFont, hintSize, WP::kPauseHintSuffix);
    const float escChipW = chipWidth(WP::kPauseHintEscKey);
    const float pChipW = chipWidth(WP::kPauseHintPKey);
    const float space = 6.0F;
    const float totalW = prefixW + space + escChipW + space + orW + space +
                         pChipW + space + suffixW;
    float runX = cardX + cardW * 0.5F - totalW * 0.5F;
    const float chipCenterY = hintTextY + hintSize * 0.5F;

    drawText(regularFont, hintSize, runX, hintTextY, WP::kPauseInstructionsColor,
             WP::kPauseHintPrefix);
    runX += prefixW + space;
    // drawKeyChipRight anchors on the RIGHT edge, so pass runX + chipW.
    drawKeyChipRight(runX + escChipW, chipCenterY, WP::kPauseHintEscKey,
                     WP::kPauseKeyHintBg, WP::kPauseKeyHintText);
    runX += escChipW + space;
    drawText(regularFont, hintSize, runX, hintTextY, WP::kPauseInstructionsColor,
             WP::kPauseHintOr);
    runX += orW + space;
    drawKeyChipRight(runX + pChipW, chipCenterY, WP::kPauseHintPKey,
                     WP::kPauseKeyHintBg, WP::kPauseKeyHintText);
    runX += pChipW + space;
    drawText(regularFont, hintSize, runX, hintTextY, WP::kPauseInstructionsColor,
             WP::kPauseHintSuffix);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void PauseMenu::handleInput() {
    if (!m_initialized) {
        return;
    }

    // Under web parity every pause-menu interaction lives in the ImGui widgets
    // built by renderWebParityModal (the sliders + Resume/Quit buttons handle
    // their own mouse hit-testing), and ESC/P resume is owned by the
    // authoritative CatAnnihilation::handleInput. Running the legacy keyboard
    // navigation here would drive the hidden legacy m_buttons vector (Restart /
    // Settings / Main Menu / Quit) — e.g. Enter would fire a Restart-Wave
    // confirmation the parity modal never shows — so this handler is a no-op
    // under parity. The full legacy path below is kept for !kEnabled.
    if constexpr (WebParity::kEnabled) {
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

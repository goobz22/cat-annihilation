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

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

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

    // The web menu backdrop is a STATIC navy gradient (no starfield), so
    // there is no per-frame background animation to advance anymore. Hover
    // feedback is now owned by ImGui (IsItemHovered on the card / footer
    // widgets in render), so there is no MenuButton hit-test poll here
    // either — both the animated starfield and the manual hover pass were
    // retired in the 2026-07-17 presentation rebuild.

    // Reset-Progress confirm window: the web arms "click again to confirm"
    // for 3 s then auto-clears (GameModeSelection.tsx:41). Count it down so a
    // player who armed it and walked away isn't left one stray click from a
    // wipe.
    if (m_resetConfirmPending) {
        m_resetConfirmTimer -= deltaTime;
        if (m_resetConfirmTimer <= 0.0F) {
            m_resetConfirmPending = false;
        }
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

    // ---------------------------------------------------------------- Backdrop
    // The web overlay is a single deep-navy gradient — menus.css:1120,
    // `linear-gradient(135deg, #1a1a2e, #16213e, #0f3460)` — with NO star
    // specks (the pre-rebuild native menu drew a 50-star field that has no
    // web analog). ImGui's DrawList has no rounded gradient primitive but
    // AddRectFilledMultiColor does a flat 4-corner blend; laying the three
    // stops across the diagonal (top-left brightest, bottom-right deepest)
    // reproduces the 135deg wash closely enough that a side-by-side read
    // matches. Drawn first inside the overlay window so it sits behind the
    // card; the UIPass base fill (renderBackground) only shows if ImGui is
    // absent.
    {
        ImDrawList* backdrop = ImGui::GetWindowDrawList();
        const ImU32 topLeft  = IM_COL32(WebParity::kMenuBgTop.red,
                                        WebParity::kMenuBgTop.green,
                                        WebParity::kMenuBgTop.blue, 255);
        const ImU32 midTone  = IM_COL32(WebParity::kMenuBgMid.red,
                                        WebParity::kMenuBgMid.green,
                                        WebParity::kMenuBgMid.blue, 255);
        const ImU32 bottomRt = IM_COL32(WebParity::kMenuBgBottom.red,
                                        WebParity::kMenuBgBottom.green,
                                        WebParity::kMenuBgBottom.blue, 255);
        backdrop->AddRectFilledMultiColor(ImVec2(0.0F, 0.0F), ImVec2(width, height),
                                          topLeft, midTone, bottomRt, midTone);
    }

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
    // The web menu is mouse-driven: the two mode CARDS and the footer buttons
    // are clicked (renderModeSelectPage owns those ImGui hit-tests). Keyboard
    // support is kept to the ONE primary action a controller/keyboard player
    // expects — Enter/Space opens the Survival customize screen, the web's
    // handleSurvivalMode (GameModeSelection.tsx:46-49). Story is the only
    // other card and it is coming-soon (no keyboard target), so the vertical
    // Up/Down button-list navigation the plain-button menu used is gone with
    // the button list itself. Mouse clicks are handled inside the render pass
    // by ImGui, never here, so a card can't double-activate (press + release).
    if (m_input.isKeyPressed(Engine::Input::Key::Enter) ||
        m_input.isKeyPressed(Engine::Input::Key::Space)) {
        m_audio.playMenuClick();
        m_currentPage = MenuPage::Customize;
    }
}

// ============================================================================
// Private Methods
// ============================================================================

namespace {

// Pack a web sRGB UiColor into an ImGui colour. The optional alpha
// reproduces the CSS rgba() opacity the web uses on translucent panels /
// borders. Chrome colours are handed to ImGui RAW (no srgb->linear decode)
// — see the color-space note in WebParityConfig.hpp — so this is a straight
// byte copy plus the alpha.
ImU32 toImCol(const WebParity::UiColor& color, float alpha = 1.0F) {
    return IM_COL32(color.red, color.green, color.blue,
                    static_cast<int>(alpha * 255.0F + 0.5F));
}

// The web card / mode-card / panel fills are 145deg gradients, but ImGui's
// DrawList can round a rect OR gradient-fill it, never both. For those
// rounded surfaces we fill with the MIDPOINT of the two gradient stops —
// visually indistinguishable from the subtle #2d2d2d→#1a1a1a-class ramps at
// this scale, and it preserves the rounded corners the card look depends on.
// (The one surface where a true gradient reads — the full-screen, un-rounded
// navy backdrop — uses AddRectFilledMultiColor in render() instead.)
ImU32 midFill(const WebParity::UiColor& top, const WebParity::UiColor& bottom,
              float alpha = 1.0F) {
    return IM_COL32((top.red + bottom.red) / 2,
                    (top.green + bottom.green) / 2,
                    (top.blue + bottom.blue) / 2,
                    static_cast<int>(alpha * 255.0F + 0.5F));
}

// Word-wrap `text` to lines that each fit within maxWidth at (font, size).
// Used for the mode-card description paragraphs, which the web centres and
// wraps (text-align:center inside a fixed card). ImGui's AddText can wrap but
// only LEFT-aligns; splitting into lines here lets the caller centre each
// one, matching the web. A single word longer than maxWidth is kept whole on
// its own line rather than dropped.
std::vector<std::string> wrapToWidth(const ImFont* font, float size,
                                     const std::string& text, float maxWidth) {
    std::vector<std::string> lines;
    std::string current;
    size_t index = 0;
    while (index < text.size()) {
        const size_t space = text.find(' ', index);
        const std::string word =
            text.substr(index, space == std::string::npos ? std::string::npos : space - index);
        const std::string trial = current.empty() ? word : current + " " + word;
        if (current.empty() ||
            font->CalcTextSizeA(size, FLT_MAX, 0.0F, trial.c_str()).x <= maxWidth) {
            current = trial;
        } else {
            lines.push_back(current);
            current = word;
        }
        if (space == std::string::npos) {
            break;
        }
        index = space + 1;
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

} // namespace

void MainMenu::renderBackground(CatEngine::Renderer::UIPass& uiPass) {
    // Flat navy base fill only. The web menu's real backdrop is a three-stop
    // navy gradient with NO star specks (menus.css:1120); that gradient — and
    // the card on top of it — are drawn with the ImGui DrawList inside
    // render(). This UIPass quad is just the fallback that keeps the screen
    // navy (never black) on the harness path where no ImGui layer is
    // attached. The pre-rebuild 50-star field + bottom gradient overlay had
    // no web analog and are gone.
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 0.0F;
    bgQuad.y = 0.0F;
    bgQuad.width = static_cast<float>(m_screenWidth);
    bgQuad.height = static_cast<float>(m_screenHeight);
    // Middle gradient stop (#16213e) so the flat fallback reads as the same
    // navy family as the gradient's centre rather than either extreme.
    bgQuad.r = static_cast<float>(WebParity::kMenuBgMid.red) / 255.0F;
    bgQuad.g = static_cast<float>(WebParity::kMenuBgMid.green) / 255.0F;
    bgQuad.b = static_cast<float>(WebParity::kMenuBgMid.blue) / 255.0F;
    bgQuad.a = 1.0F;
    bgQuad.depth = 0.0F;
    bgQuad.texture = nullptr;
    uiPass.DrawQuad(bgQuad);
}

void MainMenu::renderModeSelectPage(float width, float height) {
    // The web mode-select screen (GameModeSelection.tsx:308-364 +
    // menus.css) is a dark rounded CARD centred on the navy backdrop:
    // a header ("🐱 Cat Warriors" / "Choose your adventure" + rule), two
    // side-by-side mode cards (Survival red-edged / Story teal-edged), a
    // "Development Status" banner, and a footer. We rebuild that whole box
    // with the ImGui DrawList — real card, not a stack of buttons — so a
    // side-by-side screenshot reads as the same layout. Emoji glyphs (🐱 ⚔️
    // 📜 🚧) have no font atlas coverage, so each is approximated with drawn
    // shapes (documented at each draw site) rather than a missing-glyph box.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImFont* titleFont = m_imguiLayer->GetTitleFont();
    ImFont* boldFont = m_imguiLayer->GetBoldFont();
    ImFont* regularFont = m_imguiLayer->GetRegularFont();
    // Fonts are non-null after ImGuiLayer init (it falls back title→bold→
    // regular→default), but guard so an AddText never dereferences null.
    if (titleFont == nullptr) { titleFont = ImGui::GetFont(); }
    if (boldFont == nullptr) { boldFont = ImGui::GetFont(); }
    if (regularFont == nullptr) { regularFont = ImGui::GetFont(); }

    // Local text helpers (DrawList AddText with an explicit font SIZE lets us
    // hit the web's px scale — the atlas fonts are 80/30/20 px, downscaled
    // here for crisp glyphs). measure() centres; drawText() left-aligns.
    auto measure = [](const ImFont* font, float size, const char* text) -> ImVec2 {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0F, text);
    };
    auto drawText = [&](const ImFont* font, float size, float x, float y,
                        ImU32 col, const char* text) {
        drawList->AddText(font, size, ImVec2(x, y), col, text);
    };
    auto drawCentered = [&](const ImFont* font, float size, float centerX, float y,
                            ImU32 col, const char* text) {
        const float textWidth = font->CalcTextSizeA(size, FLT_MAX, 0.0F, text).x;
        drawList->AddText(font, size, ImVec2(centerX - textWidth * 0.5F, y), col, text);
    };

    // -------------------------------------------------------------- Card box
    // Card sized to content and centred. The height is the SUM of every band
    // below (header + body pad + mode cards + notice + footer), so the card
    // always fits its content instead of guessing a fraction of the screen.
    const float cardWidth = std::min(880.0F, width * 0.92F);
    const float headerHeight = 96.0F;
    const float bodyPadX = 28.0F;
    const float bodyPadY = 26.0F;
    const float columnGap = 20.0F;
    const float modeCardHeight = 344.0F;
    const float afterCardsGap = 18.0F;
    const float noticeHeight = 66.0F;
    const float afterNoticeGap = 16.0F;
    const float footerHeight = 52.0F;
    const float bottomPad = 20.0F;
    const float cardHeight = headerHeight + bodyPadY + modeCardHeight + afterCardsGap +
                             noticeHeight + afterNoticeGap + footerHeight + bottomPad;
    const float cardX = (width - cardWidth) * 0.5F;
    const float cardY = std::max(20.0F, (height - cardHeight) * 0.5F);
    const ImVec2 cardMin(cardX, cardY);
    const ImVec2 cardMax(cardX + cardWidth, cardY + cardHeight);

    // Card body (rounded solid mid-tone of the #2d2d2d→#1a1a1a gradient) +
    // 3px #444 border — menus.css:1177-1180.
    drawList->AddRectFilled(cardMin, cardMax,
                            midFill(WebParity::kCardTop, WebParity::kCardBottom), 20.0F);
    drawList->AddRect(cardMin, cardMax, toImCol(WebParity::kCardBorder),
                      20.0F, 0, 3.0F);

    // ----------------------------------------------------------------- Header
    // Rounded-top band (#444→#333) with a 3px #555 bottom rule — menus.css:
    // 1201-1202. Solid mid-tone for the same round-vs-gradient reason as the
    // card body.
    const float headerBottom = cardY + headerHeight;
    drawList->AddRectFilled(cardMin, ImVec2(cardMax.x, headerBottom),
                            midFill(WebParity::kHeaderTop, WebParity::kHeaderBottom),
                            20.0F, ImDrawFlags_RoundCornersTop);
    drawList->AddRectFilled(ImVec2(cardX, headerBottom - 3.0F),
                            ImVec2(cardMax.x, headerBottom),
                            toImCol(WebParity::kHeaderRule));

    // Header content: a cat-face glyph approximation + "Cat Warriors" as one
    // centred group, then the subheading. Title size 46 ≈ the web's clamp
    // 42px header (menus.css:1207), downscaled from the 80px atlas → crisp.
    const float titleSize = 46.0F;
    const ImVec2 titleDim = measure(titleFont, titleSize, WebParity::kMenuHeading);
    const float catIconSize = 40.0F;
    const float catIconGap = 14.0F;
    const float groupWidth = catIconSize + catIconGap + titleDim.x;
    const float groupX = cardX + (cardWidth - groupWidth) * 0.5F;
    const float titleTop = cardY + 18.0F;
    // Cat-face icon (🐱 stand-in): warm-orange head circle + two triangle
    // ears + dark eyes + pink nose. Not the emoji — the atlas has none — but
    // an unmistakable cat silhouette that reads at header scale.
    {
        const ImU32 fur = IM_COL32(0xF0, 0x9A, 0x3E, 255);
        const ImU32 dark = IM_COL32(0x33, 0x24, 0x10, 255);
        const float iconX = groupX;
        const float iconY = titleTop + (titleDim.y - catIconSize) * 0.5F;
        const ImVec2 center(iconX + catIconSize * 0.5F, iconY + catIconSize * 0.58F);
        const float radius = catIconSize * 0.34F;
        drawList->AddTriangleFilled(
            ImVec2(center.x - radius * 0.95F, center.y - radius * 0.45F),
            ImVec2(center.x - radius * 0.15F, center.y - radius * 1.2F),
            ImVec2(center.x - radius * 0.05F, center.y - radius * 0.25F), fur);
        drawList->AddTriangleFilled(
            ImVec2(center.x + radius * 0.95F, center.y - radius * 0.45F),
            ImVec2(center.x + radius * 0.15F, center.y - radius * 1.2F),
            ImVec2(center.x + radius * 0.05F, center.y - radius * 0.25F), fur);
        drawList->AddCircleFilled(center, radius, fur, 28);
        drawList->AddCircleFilled(ImVec2(center.x - radius * 0.4F, center.y - radius * 0.05F),
                                  radius * 0.13F, dark, 12);
        drawList->AddCircleFilled(ImVec2(center.x + radius * 0.4F, center.y - radius * 0.05F),
                                  radius * 0.13F, dark, 12);
        drawList->AddTriangleFilled(
            ImVec2(center.x - radius * 0.13F, center.y + radius * 0.22F),
            ImVec2(center.x + radius * 0.13F, center.y + radius * 0.22F),
            ImVec2(center.x, center.y + radius * 0.42F), IM_COL32(0xE0, 0x6A, 0x8A, 255));
    }
    // Title in WHITE (menus.css:1206) — the exact regression the pre-audit
    // gold title violated — and the grey subheading below.
    drawText(titleFont, titleSize, groupX + catIconSize + catIconGap, titleTop,
             toImCol(WebParity::kMenuTitleColor), WebParity::kMenuHeading);
    drawCentered(regularFont, 18.0F, cardX + cardWidth * 0.5F,
                 titleTop + titleDim.y + 8.0F, toImCol(WebParity::kMenuSubtitleColor),
                 WebParity::kMenuSubheading);

    // ------------------------------------------------------------- Mode cards
    // Two equal columns, gap 20 (menus.css:1223-1225). Survival is clickable
    // → customize; Story is coming-soon and does nothing on click.
    const float bodyTop = headerBottom + bodyPadY;
    const float modeCardWidth = (cardWidth - 2.0F * bodyPadX - columnGap) * 0.5F;
    const float leftCardX = cardX + bodyPadX;
    const float rightCardX = leftCardX + modeCardWidth + columnGap;

    // drawModeCard renders one card and returns whether it was clicked this
    // frame. Enabled cards hit-test via an InvisibleButton (ImGui owns the
    // click/hover, so no double-activation); a disabled card skips the button
    // so its clicks are inert — the web's coming-soon Story behaviour.
    auto drawModeCard = [&](float cardLeft, const char* titleStr, const char* subtitleStr,
                            const char* description, const char* const* features,
                            int featureCount, const WebParity::UiColor& accent,
                            bool enabled, bool isSurvival) -> bool {
        const ImVec2 topLeft(cardLeft, bodyTop);
        const ImVec2 bottomRight(cardLeft + modeCardWidth, bodyTop + modeCardHeight);

        bool clicked = false;
        bool hovered = false;
        if (enabled) {
            ImGui::SetCursorPos(topLeft);
            ImGui::PushID(cardLeft < rightCardX - 1.0F ? "##survivalCard" : "##card2");
            clicked = ImGui::InvisibleButton(isSurvival ? "##survival" : "##mode",
                                             ImVec2(modeCardWidth, modeCardHeight));
            hovered = ImGui::IsItemHovered();
            ImGui::PopID();
        }

        // Card fill + border. The border is #555 at rest and the accent
        // (survival red / story teal) on hover — menus.css:1246-1254.
        drawList->AddRectFilled(topLeft, bottomRight,
                                midFill(WebParity::kModeCardTop, WebParity::kModeCardBottom),
                                15.0F);
        drawList->AddRect(topLeft, bottomRight,
                          hovered ? toImCol(accent) : toImCol(WebParity::kModeCardBorder),
                          15.0F, 0, 2.0F);

        const float centerX = cardLeft + modeCardWidth * 0.5F;
        const float innerPad = 18.0F;
        // Greyed text for the disabled (coming-soon) card so it reads as
        // inactive, matching the web's dimmed story card.
        const float textAlpha = enabled ? 1.0F : 0.55F;

        // Icon (⚔️ crossed swords / 📜 scroll approximations).
        const float iconCenterY = bodyTop + 40.0F;
        if (isSurvival) {
            const ImU32 blade = IM_COL32(0xC8, 0xCE, 0xD8,
                                         static_cast<int>(textAlpha * 255.0F));
            const ImU32 hilt = IM_COL32(0x8A, 0x6A, 0x3A,
                                        static_cast<int>(textAlpha * 255.0F));
            const float halfLen = 17.0F;
            drawList->AddLine(ImVec2(centerX - halfLen, iconCenterY - halfLen),
                              ImVec2(centerX + halfLen, iconCenterY + halfLen), blade, 4.0F);
            drawList->AddLine(ImVec2(centerX + halfLen, iconCenterY - halfLen),
                              ImVec2(centerX - halfLen, iconCenterY + halfLen), blade, 4.0F);
            drawList->AddCircleFilled(ImVec2(centerX - halfLen, iconCenterY + halfLen), 4.0F, hilt);
            drawList->AddCircleFilled(ImVec2(centerX + halfLen, iconCenterY + halfLen), 4.0F, hilt);
        } else {
            const ImU32 parchment = IM_COL32(0xE8, 0xC9, 0x7A,
                                             static_cast<int>(textAlpha * 255.0F));
            const ImU32 lineCol = IM_COL32(0xB8, 0x94, 0x4A,
                                           static_cast<int>(textAlpha * 255.0F));
            const float halfW = 14.0F;
            const float halfH = 18.0F;
            drawList->AddRectFilled(ImVec2(centerX - halfW, iconCenterY - halfH),
                                    ImVec2(centerX + halfW, iconCenterY + halfH), parchment, 3.0F);
            drawList->AddRectFilled(ImVec2(centerX - halfW - 3.0F, iconCenterY - halfH - 4.0F),
                                    ImVec2(centerX + halfW + 3.0F, iconCenterY - halfH + 3.0F),
                                    lineCol, 2.0F);
            drawList->AddRectFilled(ImVec2(centerX - halfW - 3.0F, iconCenterY + halfH - 3.0F),
                                    ImVec2(centerX + halfW + 3.0F, iconCenterY + halfH + 4.0F),
                                    lineCol, 2.0F);
            for (int k = 0; k < 3; ++k) {
                const float lineY = iconCenterY - 8.0F + static_cast<float>(k) * 8.0F;
                drawList->AddLine(ImVec2(centerX - halfW * 0.6F, lineY),
                                  ImVec2(centerX + halfW * 0.6F, lineY), lineCol, 1.5F);
            }
        }

        // Title (bold 24, white), subtitle (regular 15, #bbb) — menus.css:
        // 1263/1271.
        drawCentered(boldFont, 24.0F, centerX, bodyTop + 62.0F,
                     toImCol(WebParity::kModeTitleColor, textAlpha), titleStr);
        drawCentered(regularFont, 15.0F, centerX, bodyTop + 96.0F,
                     toImCol(WebParity::kModeSubtitleColor, textAlpha), subtitleStr);

        // Description paragraph — centred + word-wrapped inside the card
        // (menus.css:1277-1283, text-align:center). Each line centred.
        const float descTop = bodyTop + 122.0F;
        const float descLineHeight = 19.0F;
        const std::vector<std::string> descLines =
            wrapToWidth(regularFont, 14.0F, description, modeCardWidth - 2.0F * innerPad);
        for (size_t line = 0; line < descLines.size(); ++line) {
            drawCentered(regularFont, 14.0F, centerX,
                         descTop + static_cast<float>(line) * descLineHeight,
                         toImCol(WebParity::kModeDescColor, textAlpha),
                         descLines[line].c_str());
        }

        // Feature panel — inset black@0.3 box with a 3px accent LEFT edge and
        // a bulleted list (menus.css:1285-1302). The web "•" glyph is absent
        // from the atlas, so each bullet is a small filled dot.
        const float panelBottom = bodyTop + modeCardHeight - innerPad;
        const float panelHeight = 108.0F;
        const float panelTop = panelBottom - panelHeight;
        const ImVec2 panelMin(cardLeft + innerPad, panelTop);
        const ImVec2 panelMax(cardLeft + modeCardWidth - innerPad, panelBottom);
        drawList->AddRectFilled(panelMin, panelMax,
                                toImCol(WebParity::kFeaturePanelFill, 0.3F), 8.0F);
        drawList->AddRectFilled(panelMin, ImVec2(panelMin.x + 3.0F, panelMax.y),
                                toImCol(accent, textAlpha), 2.0F);
        for (int feature = 0; feature < featureCount; ++feature) {
            const float rowY = panelTop + 12.0F + static_cast<float>(feature) * 21.0F;
            drawList->AddCircleFilled(ImVec2(panelMin.x + 14.0F, rowY + 8.0F), 2.0F,
                                      toImCol(WebParity::kModeFeatureColor, textAlpha));
            drawText(regularFont, 13.0F, panelMin.x + 22.0F, rowY,
                     toImCol(WebParity::kModeFeatureColor, textAlpha), features[feature]);
        }

        // Coming-soon tag for the disabled card (amber, over the icon area).
        if (!enabled) {
            drawCentered(regularFont, 13.0F, centerX, bodyTop + modeCardHeight * 0.5F - 6.0F,
                         IM_COL32(0xE0, 0xB4, 0x4C, 230), WebParity::kStoryComingSoon);
        }
        return clicked;
    };

    const bool survivalClicked = drawModeCard(
        leftCardX, WebParity::kSurvivalCardTitle, WebParity::kSurvivalCardSubtitle,
        WebParity::kSurvivalCardDescription, WebParity::kSurvivalFeatures,
        WebParity::kSurvivalFeatureCount, WebParity::kSurvivalAccent, true, true);
    // Story card: drawn for the two-card parity layout but non-functional
    // (P3-deferred). Its return is intentionally ignored — no callback fires.
    (void)drawModeCard(
        rightCardX, WebParity::kStoryCardTitle, WebParity::kStoryCardSubtitle,
        WebParity::kStoryCardDescription, WebParity::kStoryFeatures,
        WebParity::kStoryFeatureCount, WebParity::kStoryAccent, false, false);

    if (survivalClicked) {
        m_audio.playMenuClick();
        m_currentPage = MenuPage::Customize;
    }

    // ------------------------------------------------------- Development note
    // Subtle grey banner (menus.css:1305-1315): faint fill, #999@0.3 border,
    // a 🚧 barricade approximation, and two copy lines with a bold lead word.
    const float noticeTop = bodyTop + modeCardHeight + afterCardsGap;
    const ImVec2 noticeMin(cardX + bodyPadX, noticeTop);
    const ImVec2 noticeMax(cardX + cardWidth - bodyPadX, noticeTop + noticeHeight);
    drawList->AddRectFilled(noticeMin, noticeMax, IM_COL32(0x66, 0x66, 0x66, 36), 10.0F);
    drawList->AddRect(noticeMin, noticeMax, toImCol(WebParity::kDevNoticeBorder, 0.3F),
                      10.0F, 0, 1.0F);
    {
        // 🚧 barricade stand-in: an amber rounded bar with black hazard slashes.
        const float barX = noticeMin.x + 14.0F;
        const float barCenterY = noticeTop + noticeHeight * 0.5F;
        const float barW = 26.0F;
        const float barH = 16.0F;
        drawList->AddRectFilled(ImVec2(barX, barCenterY - barH * 0.5F),
                                ImVec2(barX + barW, barCenterY + barH * 0.5F),
                                IM_COL32(0xE4, 0xB4, 0x3A, 255), 2.0F);
        for (int slash = 0; slash < 3; ++slash) {
            const float sx = barX + 3.0F + static_cast<float>(slash) * 8.0F;
            drawList->AddLine(ImVec2(sx, barCenterY + barH * 0.4F),
                              ImVec2(sx + 6.0F, barCenterY - barH * 0.4F),
                              IM_COL32(0x22, 0x22, 0x22, 255), 2.5F);
        }
    }
    const float noticeTextX = noticeMin.x + 14.0F + 26.0F + 14.0F;
    const float noticeRow1Y = noticeTop + 12.0F;
    const ImVec2 headingDim = measure(boldFont, 14.0F, WebParity::kDevStatusHeading);
    drawText(boldFont, 14.0F, noticeTextX, noticeRow1Y,
             toImCol(WebParity::kDevNoticeStrong), WebParity::kDevStatusHeading);
    drawText(regularFont, 13.0F, noticeTextX + headingDim.x + 6.0F, noticeRow1Y + 1.0F,
             toImCol(WebParity::kDevNoticeText), WebParity::kDevStatusLine1);
    drawText(regularFont, 13.0F, noticeTextX, noticeRow1Y + 24.0F,
             toImCol(WebParity::kDevNoticeText), WebParity::kDevStatusLine2);

    // ----------------------------------------------------------------- Footer
    // A top rule, then the "Reset Progress" control (menus.css:2140-2189, the
    // web's two-click confirm) centred, plus the ONE deliberate desktop-exit
    // divergence: a small Quit affordance the web has no equivalent for.
    const float footerTop = noticeTop + noticeHeight + afterNoticeGap;
    drawList->AddLine(ImVec2(cardX + bodyPadX, footerTop),
                      ImVec2(cardX + cardWidth - bodyPadX, footerTop),
                      IM_COL32(255, 255, 255, 26), 1.0F);

    ImGui::PushFont(regularFont);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);

    // Reset Progress — grey normally, red once armed. The label swaps to the
    // confirm copy while armed (matching the web's "⚠️ Click again to
    // confirm", emoji dropped). A second click while armed fires the wired
    // reset callback if present; with none wired it is INERT (there is no
    // persisted native leveling save to clear today — documented seam).
    const float resetWidth = 210.0F;
    const float buttonHeight = 40.0F;
    const float footerButtonsY = footerTop + (footerHeight - buttonHeight) * 0.5F + 4.0F;
    const WebParity::UiColor resetTop =
        m_resetConfirmPending ? WebParity::kDeathAccent : WebParity::kResetButtonTop;
    const WebParity::UiColor resetBottom =
        m_resetConfirmPending ? WebParity::kDeathAccentDark : WebParity::kResetButtonBottom;
    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(toImCol(resetTop)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImColor(toImCol(WebParity::kDeathAccent, 0.9F)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(toImCol(resetBottom)).Value);
    ImGui::SetCursorPos(ImVec2(cardX + (cardWidth - resetWidth) * 0.5F, footerButtonsY));
    const char* resetLabel =
        m_resetConfirmPending ? WebParity::kResetConfirmLabel : WebParity::kResetProgressLabel;
    if (ImGui::Button(resetLabel, ImVec2(resetWidth, buttonHeight))) {
        m_audio.playMenuClick();
        if (m_resetConfirmPending) {
            m_resetConfirmPending = false;
            if (m_resetProgressCallback) {
                m_resetProgressCallback();
            }
        } else {
            m_resetConfirmPending = true;
            m_resetConfirmTimer = 3.0F;
        }
    }
    ImGui::PopStyleColor(3);

    // Small Quit — the deliberate desktop-exit divergence (web has none).
    const float quitWidth = 90.0F;
    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(toImCol(WebParity::kBackButtonTop)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImColor(toImCol(WebParity::kBackButtonTop, 0.85F)).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImColor(toImCol(WebParity::kBackButtonBottom)).Value);
    ImGui::SetCursorPos(ImVec2(cardX + cardWidth - bodyPadX - quitWidth, footerButtonsY));
    if (ImGui::Button(WebParity::kQuitLabel, ImVec2(quitWidth, buttonHeight))) {
        m_audio.playMenuClick();
        if (m_quitCallback) {
            m_quitCallback();
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::PopStyleVar();
    ImGui::PopFont();
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

    // ---------------------------------------------------------- Colour pickers
    // The web customize screen stacks two identical pickers — Fur Color
    // (GameModeSelection.tsx:196-209) then Eye Color (:211-224) — each a
    // labeled section over a grid of unlabeled colour buttons with the
    // active choice outlined. Because the two controls are structurally the
    // same, ONE lambda draws both instead of duplicating the
    // ColorButton / selection-ring / tooltip logic twice; it returns the
    // index clicked this frame (or -1) and reports the grid's bottom Y so
    // the next section can stack beneath it.
    constexpr int kSwatchColumns = 5;
    constexpr float kSwatchSize = 56.0F;
    constexpr float kSwatchGap = 14.0F;
    const float gridWidth = static_cast<float>(kSwatchColumns) * kSwatchSize +
                            static_cast<float>(kSwatchColumns - 1) * kSwatchGap;
    const float gridX = (width - gridWidth) * 0.5F;
    // The whole customize stack starts at 0.30h (was 0.40h before the eye
    // picker existed): the added eye row needs vertical room, and starting
    // higher keeps BACK / START on screen down to 720p.
    const float furGridY = height * 0.30F;

    auto drawSwatchGrid = [&](const char* label,
                              const WebParity::ColorSwatch* swatches,
                              int swatchCount,
                              int columns,
                              float swatchSize,
                              int selectedIndex,
                              float originX,
                              float originY,
                              float& outBottomY) -> int {
        // Section label (gold, bold) centred above the grid.
        if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
            ImGui::PushFont(boldFont);
        }
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        ImGui::SetCursorPos(ImVec2((width - labelSize.x) * 0.5F,
                                   originY - labelSize.y - 14.0F));
        ImGui::TextColored(ImVec4(1.00F, 0.80F, 0.10F, 1.00F), "%s", label);
        if (m_imguiLayer->GetBoldFont() != nullptr) {
            ImGui::PopFont();
        }

        int clickedIndex = -1;
        int rowsDrawn = 0;
        for (int i = 0; i < swatchCount; ++i) {
            const auto& swatch = swatches[i];
            const int column = i % columns;
            const int row = i / columns;
            rowsDrawn = row + 1;
            ImGui::SetCursorPos(
                ImVec2(originX + static_cast<float>(column) * (swatchSize + kSwatchGap),
                       originY + static_cast<float>(row) * (swatchSize + kSwatchGap)));

            // ImGui hands widget colours to the backend as-authored (no
            // colour-space conversion), so the swatch face shows the raw
            // web sRGB bytes — exactly the web hex. Only the value fed
            // onward to a tint push constant is linear-decoded (that is
            // getSelected*Linear's job, not the preview's).
            const ImVec4 faceColor(static_cast<float>(swatch.red) / 255.0F,
                                   static_cast<float>(swatch.green) / 255.0F,
                                   static_cast<float>(swatch.blue) / 255.0F,
                                   1.0F);
            // Two-level ID scope: the section label disambiguates the fur
            // grid from the eye grid (both draw a "##swatch" button), and
            // the index disambiguates swatches within one grid — without the
            // label push the two grids' same-index buttons would collide.
            ImGui::PushID(label);
            ImGui::PushID(i);
            if (ImGui::ColorButton("##swatch", faceColor,
                                   ImGuiColorEditFlags_NoTooltip |
                                       ImGuiColorEditFlags_NoDragDrop |
                                       ImGuiColorEditFlags_NoAlpha,
                                   ImVec2(swatchSize, swatchSize))) {
                m_audio.playMenuClick();
                clickedIndex = i;
            }
            // Our own tooltip (the swatch's parity-table name) rather than
            // ColorButton's default r/g/b readout, which is picker chrome
            // that means nothing to a player.
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", swatch.name);
            }
            ImGui::PopID();
            ImGui::PopID();

            // Selection ring — the ImGui take on the web's `.selected`
            // outline (GameModeSelection.tsx:203 fur / :218 eye). Same gold
            // as the keyboard ring on the mode-select page.
            if (i == selectedIndex) {
                const ImVec2 rectMin = ImGui::GetItemRectMin();
                const ImVec2 rectMax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRect(
                    ImVec2(rectMin.x - 3.0F, rectMin.y - 3.0F),
                    ImVec2(rectMax.x + 3.0F, rectMax.y + 3.0F),
                    IM_COL32(255, 204, 26, 255), 4.0F, 0, 3.0F);
            }
        }
        outBottomY = rowsDrawn > 0
            ? originY + static_cast<float>(rowsDrawn) * swatchSize +
                  static_cast<float>(rowsDrawn - 1) * kSwatchGap
            : originY;
        return clickedIndex;
    };

    // FUR COLOR — 5x2 grid over kFurSwatches.
    float furGridBottom = furGridY;
    const int furClicked = drawSwatchGrid(
        "FUR COLOR", WebParity::kFurSwatches, WebParity::kFurSwatchCount,
        kSwatchColumns, kSwatchSize, m_selectedFurIndex, gridX, furGridY,
        furGridBottom);
    if (furClicked >= 0) {
        m_selectedFurIndex = furClicked;
    }

    // EYE COLOR — eight swatches in one row directly below the fur grid,
    // mirroring the web ordering (fur section, then eye section). The eye
    // swatches are a touch smaller so eight of them span roughly the same
    // width as the five-wide fur grid above.
    constexpr int kEyeColumns = 8;
    constexpr float kEyeSwatchSize = 44.0F;
    const float eyeGridWidth = static_cast<float>(kEyeColumns) * kEyeSwatchSize +
                               static_cast<float>(kEyeColumns - 1) * kSwatchGap;
    const float eyeGridX = (width - eyeGridWidth) * 0.5F;
    const float eyeGridY = furGridBottom + 48.0F; // leave room for the EYE COLOR label
    float eyeGridBottom = eyeGridY;
    const int eyeClicked = drawSwatchGrid(
        "EYE COLOR", WebParity::kEyeSwatches, WebParity::kEyeSwatchCount,
        kEyeColumns, kEyeSwatchSize, m_selectedEyeIndex, eyeGridX, eyeGridY,
        eyeGridBottom);
    if (eyeClicked >= 0) {
        m_selectedEyeIndex = eyeClicked;
    }

    // Selected-choice readout. The web shows the choices on a live 3D cat
    // preview beside the grids; the native menu draws no 3D scene (a noted
    // parity delta), so naming both selections is the stand-in feedback.
    if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
        ImGui::PushFont(regularFont);
    }
    const std::string selectionText =
        std::string("Fur: ") + WebParity::kFurSwatches[m_selectedFurIndex].name +
        "    Eyes: " + WebParity::kEyeSwatches[m_selectedEyeIndex].name;
    const ImVec2 nameSize = ImGui::CalcTextSize(selectionText.c_str());
    const float nameY = eyeGridBottom + 20.0F;
    ImGui::SetCursorPos(ImVec2((width - nameSize.x) * 0.5F, nameY));
    ImGui::TextColored(ImVec4(0.80F, 0.80F, 0.90F, 0.90F), "%s", selectionText.c_str());
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

void MainMenu::getSelectedEyeLinear(float& r, float& g, float& b) const {
    // Identical decode to getSelectedFurLinear, over the eye palette. See
    // the header's getSelectedEyeLinear note for why applying this to the
    // native cat's eyes is currently asset/shader-gated (the baked Meshy eye
    // texture); the value itself is a truthful latched player choice.
    const auto& swatch = WebParity::kEyeSwatches[m_selectedEyeIndex];
    r = WebParity::srgbChannelToLinear(swatch.red);
    g = WebParity::srgbChannelToLinear(swatch.green);
    b = WebParity::srgbChannelToLinear(swatch.blue);
}

} // namespace Game

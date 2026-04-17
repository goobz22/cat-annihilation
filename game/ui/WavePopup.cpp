#include "WavePopup.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/ui/ImGuiLayer.hpp"

#include "imgui.h"

#include <cmath>
#include <algorithm>

namespace Game {

namespace {
    constexpr float PI = 3.14159265358979323846F;
}

WavePopup::WavePopup(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

WavePopup::~WavePopup() {
    shutdown();
}

bool WavePopup::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("WavePopup already initialized");
        return true;
    }

    m_state = PopupState::Hidden;
    m_isVisible = false;

    m_initialized = true;
    Engine::Logger::info("WavePopup initialized successfully");
    return true;
}

void WavePopup::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_initialized = false;
    Engine::Logger::info("WavePopup shutdown");
}

void WavePopup::update(float deltaTime) {
    if (!m_initialized || !m_isVisible) {
        return;
    }

    updateAnimation(deltaTime);

    // Handle state-specific updates
    switch (m_state) {
        case PopupState::WaveComplete:
            m_displayTimer += deltaTime;

            // Auto-dismiss after delay
            if (m_displayTimer >= m_autoDismissDelay) {
                dismiss();
            }
            break;

        case PopupState::Countdown:
            m_countdownTimer += deltaTime;

            // Auto-dismiss when countdown reaches zero
            if (m_countdownTimer >= m_countdownDuration) {
                dismiss();
            }
            break;

        default:
            break;
    }
}

void WavePopup::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized || !m_isVisible) {
        return;
    }

    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    (void)uiPass; // Legacy helpers (renderWaveComplete/renderCountdown) are unreachable.

    if (m_imguiLayer == nullptr) {
        return;
    }

    const float width = static_cast<float>(screenWidth);
    const float height = static_cast<float>(screenHeight);

    // Fullscreen transparent window with a dim overlay tinted by the fade alpha.
    const float overlayAlpha =
        (m_state == PopupState::WaveComplete ? 0.60F : 0.50F) * m_fadeAlpha;

    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, overlayAlpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs; // popup is passive — WavePopup::handleInput drives it

    ImGui::Begin("##WavePopupOverlay", nullptr, kOverlayFlags);

    if (m_state == PopupState::WaveComplete) {
        // ----- Title: "WAVE N COMPLETE!" (yellow, title font)
        if (auto* titleFont = m_imguiLayer->GetTitleFont()) {
            ImGui::PushFont(titleFont);
        }
        const std::string title = "WAVE " + std::to_string(m_completedWave) + " COMPLETE!";
        const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
        const float titleY = height * 0.36F;
        ImGui::SetCursorPos(ImVec2((width - titleSize.x) * 0.5F, titleY));
        ImGui::TextColored(ImVec4(1.0F, 0.80F, 0.05F, m_fadeAlpha), "%s", title.c_str());
        if (m_imguiLayer->GetTitleFont() != nullptr) {
            ImGui::PopFont();
        }

        // ----- Stats block
        if (auto* regularFont = m_imguiLayer->GetRegularFont()) {
            ImGui::PushFont(regularFont);
        }
        float y = titleY + titleSize.y + 32.0F;
        auto writeCentered = [&](const std::string& str, ImVec4 color) {
            const ImVec2 sz = ImGui::CalcTextSize(str.c_str());
            ImGui::SetCursorPos(ImVec2((width - sz.x) * 0.5F, y));
            ImGui::TextColored(color, "%s", str.c_str());
            y += sz.y + 8.0F;
        };
        writeCentered("Enemies Killed: " + std::to_string(m_enemiesKilled),
                      ImVec4(1.0F, 1.0F, 1.0F, m_fadeAlpha));
        writeCentered("Time: " + std::to_string(static_cast<int>(m_timeTaken)) + "s",
                      ImVec4(1.0F, 1.0F, 1.0F, m_fadeAlpha));
        y += 12.0F;
        writeCentered("Next Wave: " + std::to_string(m_nextWaveEnemyCount) + " Enemies",
                      ImVec4(0.80F, 0.80F, 1.0F, m_fadeAlpha));
        if (m_imguiLayer->GetRegularFont() != nullptr) {
            ImGui::PopFont();
        }

        // ----- Blinking prompt
        const float promptAlpha =
            (std::sin(m_animationTimer * 4.0F) + 1.0F) * 0.5F * m_fadeAlpha;
        const char* prompt = "Press SPACE to continue";
        const ImVec2 pSize = ImGui::CalcTextSize(prompt);
        ImGui::SetCursorPos(ImVec2((width - pSize.x) * 0.5F, y + 40.0F));
        ImGui::TextColored(ImVec4(0.85F, 0.85F, 0.90F, promptAlpha), "%s", prompt);

    } else if (m_state == PopupState::Countdown) {
        // ----- "WAVE N"
        if (auto* titleFont = m_imguiLayer->GetTitleFont()) {
            ImGui::PushFont(titleFont);
        }
        const std::string title = "WAVE " + std::to_string(m_startingWave);
        const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
        const float titleY = height * 0.36F;
        ImGui::SetCursorPos(ImVec2((width - titleSize.x) * 0.5F, titleY));
        ImGui::TextColored(ImVec4(1.0F, 0.80F, 0.05F, m_fadeAlpha), "%s", title.c_str());
        if (m_imguiLayer->GetTitleFont() != nullptr) {
            ImGui::PopFont();
        }

        // ----- "N Enemies Incoming"
        if (auto* boldFont = m_imguiLayer->GetBoldFont()) {
            ImGui::PushFont(boldFont);
        }
        const std::string subtitle = std::to_string(m_waveEnemyCount) + " Enemies Incoming";
        const ImVec2 subSize = ImGui::CalcTextSize(subtitle.c_str());
        ImGui::SetCursorPos(ImVec2((width - subSize.x) * 0.5F, titleY + titleSize.y + 10.0F));
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.20F, m_fadeAlpha), "%s", subtitle.c_str());
        if (m_imguiLayer->GetBoldFont() != nullptr) {
            ImGui::PopFont();
        }

        // ----- Big countdown number, red in the last 3 seconds
        const float remaining = std::max(0.0F, m_countdownDuration - m_countdownTimer);
        const int countNum = static_cast<int>(std::ceil(remaining));
        const ImVec4 countColor = (remaining <= 3.0F)
            ? ImVec4(1.0F, 0.2F, 0.2F, m_fadeAlpha)
            : ImVec4(1.0F, 1.0F, 1.0F, m_fadeAlpha);

        if (auto* titleFont = m_imguiLayer->GetTitleFont()) {
            ImGui::PushFont(titleFont);
        }
        const std::string numStr = std::to_string(countNum);
        const ImVec2 numSize = ImGui::CalcTextSize(numStr.c_str());
        ImGui::SetCursorPos(ImVec2((width - numSize.x) * 0.5F, height * 0.55F));
        ImGui::TextColored(countColor, "%s", numStr.c_str());
        if (m_imguiLayer->GetTitleFont() != nullptr) {
            ImGui::PopFont();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void WavePopup::handleInput() {
    if (!m_initialized || !m_isVisible) {
        return;
    }

    // Dismiss on any key press (for wave complete)
    if (m_state == PopupState::WaveComplete) {
        if (m_input.isKeyPressed(Engine::Input::Key::Space) ||
            m_input.isKeyPressed(Engine::Input::Key::Enter) ||
            m_input.isMouseButtonPressed(Engine::Input::MouseButton::Left)) {
            m_audio.playMenuClick();
            dismiss();
        }
    }

    // Countdown can be skipped with Space/Enter
    if (m_state == PopupState::Countdown) {
        if (m_input.isKeyPressed(Engine::Input::Key::Space) ||
            m_input.isKeyPressed(Engine::Input::Key::Enter)) {
            m_audio.playMenuClick();
            dismiss();
        }
    }
}

// ============================================================================
// Display Control
// ============================================================================

void WavePopup::showWaveComplete(uint32_t waveNumber,
                                 uint32_t enemiesKilled,
                                 float timeTaken,
                                 uint32_t nextWaveEnemyCount) {
    m_state = PopupState::WaveComplete;
    m_isVisible = true;

    m_completedWave = waveNumber;
    m_enemiesKilled = enemiesKilled;
    m_timeTaken = timeTaken;
    m_nextWaveEnemyCount = nextWaveEnemyCount;

    m_displayTimer = 0.0F;
    m_animationTimer = 0.0F;
    m_fadeAlpha = 0.0F;

    Engine::Logger::info("Wave " + std::to_string(waveNumber) + " complete popup shown");
}

void WavePopup::showWaveStart(uint32_t waveNumber, uint32_t enemyCount) {
    m_state = PopupState::Countdown;
    m_isVisible = true;

    m_startingWave = waveNumber;
    m_waveEnemyCount = enemyCount;

    m_countdownTimer = 0.0F;
    m_animationTimer = 0.0F;
    m_fadeAlpha = 0.0F;

    m_audio.playWaveStart();
    Engine::Logger::info("Wave " + std::to_string(waveNumber) + " countdown started");
}

void WavePopup::dismiss() {
    if (!m_isVisible) {
        return;
    }

    m_isVisible = false;
    m_state = PopupState::Hidden;

    if (m_dismissCallback) {
        m_dismissCallback();
    }

    Engine::Logger::info("Wave popup dismissed");
}

// ============================================================================
// Private Methods
// ============================================================================

void WavePopup::renderWaveComplete(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;
    float centerY = static_cast<float>(m_screenHeight) / 2.0F;

    // Semi-transparent overlay
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(m_screenWidth);
    overlay.height = static_cast<float>(m_screenHeight);
    overlay.r = 0.0F;
    overlay.g = 0.0F;
    overlay.b = 0.0F;
    overlay.a = 0.6F * m_fadeAlpha;
    overlay.depth = 0.0F;
    overlay.texture = nullptr;
    uiPass.DrawQuad(overlay);

    // Title with pulsing effect
    float titleScale = 1.0F + std::sin(m_animationTimer * 3.0F) * 0.1F;
    std::string titleStr = "WAVE " + std::to_string(m_completedWave) + " COMPLETE!";

    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = titleStr.c_str();
    titleText.x = centerX - 200.0F;
    titleText.y = centerY - 100.0F;
    titleText.fontSize = 42.0F * titleScale;
    titleText.r = 1.0F;
    titleText.g = 0.8F;
    titleText.b = 0.0F;
    titleText.a = m_fadeAlpha;
    titleText.depth = 0.1F;
    titleText.fontAtlas = nullptr;
    uiPass.DrawText(titleText);

    // Stats
    std::string killsStr = "Enemies Killed: " + std::to_string(m_enemiesKilled);
    CatEngine::Renderer::UIPass::TextDesc killsText;
    killsText.text = killsStr.c_str();
    killsText.x = centerX - 100.0F;
    killsText.y = centerY - 30.0F;
    killsText.fontSize = 20.0F;
    killsText.r = 1.0F;
    killsText.g = 1.0F;
    killsText.b = 1.0F;
    killsText.a = m_fadeAlpha;
    killsText.depth = 0.1F;
    killsText.fontAtlas = nullptr;
    uiPass.DrawText(killsText);

    std::string timeStr = "Time: " + std::to_string(static_cast<int>(m_timeTaken)) + "s";
    CatEngine::Renderer::UIPass::TextDesc timeText;
    timeText.text = timeStr.c_str();
    timeText.x = centerX - 50.0F;
    timeText.y = centerY;
    timeText.fontSize = 20.0F;
    timeText.r = 1.0F;
    timeText.g = 1.0F;
    timeText.b = 1.0F;
    timeText.a = m_fadeAlpha;
    timeText.depth = 0.1F;
    timeText.fontAtlas = nullptr;
    uiPass.DrawText(timeText);

    // Next wave preview
    std::string nextStr = "Next Wave: " + std::to_string(m_nextWaveEnemyCount) + " Enemies";
    CatEngine::Renderer::UIPass::TextDesc nextText;
    nextText.text = nextStr.c_str();
    nextText.x = centerX - 120.0F;
    nextText.y = centerY + 50.0F;
    nextText.fontSize = 18.0F;
    nextText.r = 0.8F;
    nextText.g = 0.8F;
    nextText.b = 1.0F;
    nextText.a = m_fadeAlpha;
    nextText.depth = 0.1F;
    nextText.fontAtlas = nullptr;
    uiPass.DrawText(nextText);

    // Prompt with blinking effect
    float promptAlpha = (std::sin(m_animationTimer * 4.0F) + 1.0F) * 0.5F * m_fadeAlpha;

    CatEngine::Renderer::UIPass::TextDesc promptText;
    promptText.text = "Press SPACE to continue";
    promptText.x = centerX - 120.0F;
    promptText.y = centerY + 100.0F;
    promptText.fontSize = 16.0F;
    promptText.r = 0.8F;
    promptText.g = 0.8F;
    promptText.b = 0.8F;
    promptText.a = promptAlpha;
    promptText.depth = 0.1F;
    promptText.fontAtlas = nullptr;
    uiPass.DrawText(promptText);
}

void WavePopup::renderCountdown(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;
    float centerY = static_cast<float>(m_screenHeight) / 2.0F;

    // Semi-transparent overlay
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(m_screenWidth);
    overlay.height = static_cast<float>(m_screenHeight);
    overlay.r = 0.0F;
    overlay.g = 0.0F;
    overlay.b = 0.0F;
    overlay.a = 0.5F * m_fadeAlpha;
    overlay.depth = 0.0F;
    overlay.texture = nullptr;
    uiPass.DrawQuad(overlay);

    // Wave title
    std::string waveStr = "WAVE " + std::to_string(m_startingWave);
    CatEngine::Renderer::UIPass::TextDesc waveText;
    waveText.text = waveStr.c_str();
    waveText.x = centerX - 80.0F;
    waveText.y = centerY - 80.0F;
    waveText.fontSize = 42.0F;
    waveText.r = 1.0F;
    waveText.g = 0.8F;
    waveText.b = 0.0F;
    waveText.a = m_fadeAlpha;
    waveText.depth = 0.1F;
    waveText.fontAtlas = nullptr;
    uiPass.DrawText(waveText);

    // Enemy count subtitle
    std::string enemyStr = std::to_string(m_waveEnemyCount) + " Enemies Incoming";
    CatEngine::Renderer::UIPass::TextDesc enemyText;
    enemyText.text = enemyStr.c_str();
    enemyText.x = centerX - 100.0F;
    enemyText.y = centerY - 30.0F;
    enemyText.fontSize = 24.0F;
    enemyText.r = 1.0F;
    enemyText.g = 0.6F;
    enemyText.b = 0.2F;
    enemyText.a = m_fadeAlpha;
    enemyText.depth = 0.1F;
    enemyText.fontAtlas = nullptr;
    uiPass.DrawText(enemyText);

    // Countdown number
    float remainingTime = m_countdownDuration - m_countdownTimer;
    int countdownNum = static_cast<int>(std::ceil(remainingTime));
    if (countdownNum < 0) countdownNum = 0;

    float pulsePhase = remainingTime - std::floor(remainingTime);
    float countdownScale = 1.5F + ((1.0F - pulsePhase) * 0.5F);

    // Color changes to red in last 3 seconds
    float countR = 1.0F;
    float countG = 1.0F;
    float countB = 1.0F;
    if (remainingTime <= 3.0F) {
        countR = 1.0F;
        countG = 0.2F;
        countB = 0.2F;
    }

    std::string countStr = std::to_string(countdownNum);
    CatEngine::Renderer::UIPass::TextDesc countText;
    countText.text = countStr.c_str();
    countText.x = centerX - (20.0F * countdownScale);
    countText.y = centerY + 20.0F;
    countText.fontSize = 48.0F * countdownScale;
    countText.r = countR;
    countText.g = countG;
    countText.b = countB;
    countText.a = m_fadeAlpha;
    countText.depth = 0.2F;
    countText.fontAtlas = nullptr;
    uiPass.DrawText(countText);

    // Skip hint
    CatEngine::Renderer::UIPass::TextDesc skipText;
    skipText.text = "Press SPACE to skip";
    skipText.x = centerX - 90.0F;
    skipText.y = centerY + 120.0F;
    skipText.fontSize = 14.0F;
    skipText.r = 0.6F;
    skipText.g = 0.6F;
    skipText.b = 0.6F;
    skipText.a = m_fadeAlpha * 0.7F;
    skipText.depth = 0.1F;
    skipText.fontAtlas = nullptr;
    uiPass.DrawText(skipText);
}

void WavePopup::updateAnimation(float deltaTime) {
    m_animationTimer += deltaTime;

    // Fade in effect
    float fadeInDuration = 0.3F;
    float currentTimer = (m_state == PopupState::WaveComplete) ? m_displayTimer : m_countdownTimer;

    if (currentTimer < fadeInDuration) {
        m_fadeAlpha = std::min(currentTimer / fadeInDuration, 1.0F);
    } else {
        m_fadeAlpha = 1.0F;
    }
}

} // namespace Game

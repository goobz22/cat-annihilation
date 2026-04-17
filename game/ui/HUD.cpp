#include "HUD.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/ui/ImGuiLayer.hpp"

#include "imgui.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Game {

HUD::HUD(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

HUD::~HUD() {
    shutdown();
}

bool HUD::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("HUD already initialized");
        return true;
    }

    // Initialize with default values
    m_currentHealth = 100.0F;
    m_maxHealth = 100.0F;
    m_currentWave = 1;
    m_remainingEnemies = 0;
    m_totalEnemies = 0;
    m_score = 0;
    m_combo = 0;

    m_initialized = true;
    Engine::Logger::info("HUD initialized successfully");
    return true;
}

void HUD::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_damageIndicators.clear();
    m_damageNumbers.clear();

    m_initialized = false;
    Engine::Logger::info("HUD shutdown");
}

void HUD::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update damage indicators
    updateDamageIndicators(deltaTime);

    // Update damage numbers
    updateDamageNumbers(deltaTime);

    // Update notifications
    updateNotifications(deltaTime);

    // Update low health warning pulse
    if (m_lowHealthWarning) {
        m_lowHealthPulse += deltaTime * 4.0F;
    }

    // Update combo display timer
    if (m_combo > 0) {
        m_comboDisplayTime += deltaTime;
    } else {
        m_comboDisplayTime = 0.0F;
    }
}

void HUD::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized) {
        return;
    }

    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // All HUD rendering goes through Dear ImGui now — proper fonts, drawlist
    // primitives, and immediate-mode layout. UIPass quads aren't used.
    (void)uiPass;
    if (m_imguiLayer == nullptr) {
        return;
    }

    const float width = static_cast<float>(screenWidth);
    const float height = static_cast<float>(screenHeight);
    const float healthRatio = (m_maxHealth > 0.0F)
        ? std::clamp(m_currentHealth / m_maxHealth, 0.0F, 1.0F)
        : 0.0F;

    ImFont* regular = m_imguiLayer->GetRegularFont();
    ImFont* bold = m_imguiLayer->GetBoldFont();

    // Full-screen transparent overlay for the HUD so we can place widgets anywhere.
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));

    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;

    ImGui::Begin("##HUDOverlay", nullptr, kOverlayFlags);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // -------------------------------------------------- Health bar (bottom-left).
    // Keep well clear of the Windows taskbar in windowed mode — the client area can
    // be ~40px shorter than the reported screenHeight.
    {
        const float barWidth = 360.0F;
        const float barHeight = 32.0F;
        const float x = 32.0F;
        const float y = height - barHeight - 120.0F;
        const ImU32 bgColor = IM_COL32(20, 20, 30, 200);
        const ImU32 fillColor = (healthRatio < 0.3F)
            ? IM_COL32(230, 60, 50, 230)
            : IM_COL32(60, 200, 90, 230);
        const ImU32 borderColor = IM_COL32(255, 255, 255, 160);

        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + barWidth, y + barHeight), bgColor, 6.0F);
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + barWidth * healthRatio, y + barHeight), fillColor, 6.0F);
        draw->AddRect(ImVec2(x, y), ImVec2(x + barWidth, y + barHeight), borderColor, 6.0F, 0, 2.0F);

        if (regular != nullptr) {
            ImGui::PushFont(regular);
        }
        char label[64];
        std::snprintf(label, sizeof(label), "HP  %d / %d",
                      static_cast<int>(m_currentHealth), static_cast<int>(m_maxHealth));
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        draw->AddText(ImVec2(x + (barWidth - labelSize.x) * 0.5F, y + (barHeight - labelSize.y) * 0.5F),
                      IM_COL32(255, 255, 255, 240), label);
        if (regular != nullptr) {
            ImGui::PopFont();
        }
    }

    // ---------------------------------------------- Wave / enemies (top-center)
    {
        if (bold != nullptr) {
            ImGui::PushFont(bold);
        }
        char waveText[64];
        std::snprintf(waveText, sizeof(waveText), "WAVE  %u", m_currentWave);
        const ImVec2 waveSize = ImGui::CalcTextSize(waveText);
        draw->AddText(ImVec2((width - waveSize.x) * 0.5F, 20.0F),
                      IM_COL32(255, 220, 80, 255), waveText);
        if (bold != nullptr) {
            ImGui::PopFont();
        }

        if (regular != nullptr) {
            ImGui::PushFont(regular);
        }
        char enemyText[64];
        std::snprintf(enemyText, sizeof(enemyText), "Dogs remaining: %u / %u",
                      m_remainingEnemies, m_totalEnemies);
        const ImVec2 enemySize = ImGui::CalcTextSize(enemyText);
        draw->AddText(ImVec2((width - enemySize.x) * 0.5F, 20.0F + waveSize.y + 4.0F),
                      IM_COL32(220, 220, 230, 220), enemyText);
        if (regular != nullptr) {
            ImGui::PopFont();
        }
    }

    // ------------------------------------------------------- Score (top-right)
    {
        if (bold != nullptr) {
            ImGui::PushFont(bold);
        }
        char scoreText[64];
        std::snprintf(scoreText, sizeof(scoreText), "SCORE  %u", m_score);
        const ImVec2 scoreSize = ImGui::CalcTextSize(scoreText);
        draw->AddText(ImVec2(width - scoreSize.x - 24.0F, 20.0F),
                      IM_COL32(255, 255, 255, 240), scoreText);
        if (bold != nullptr) {
            ImGui::PopFont();
        }
    }

    // ----------------------------------------------------- Combo under score
    if (m_combo > 1) {
        if (regular != nullptr) {
            ImGui::PushFont(regular);
        }
        char comboText[48];
        std::snprintf(comboText, sizeof(comboText), "Combo x%u", m_combo);
        const ImVec2 comboSize = ImGui::CalcTextSize(comboText);
        draw->AddText(ImVec2(width - comboSize.x - 24.0F, 60.0F),
                      IM_COL32(255, 180, 40, 230), comboText);
        if (regular != nullptr) {
            ImGui::PopFont();
        }
    }

    // -------------------------------------------------------- Crosshair (center)
    if (m_showCrosshair) {
        const float cx = width * 0.5F;
        const float cy = height * 0.5F;
        const ImU32 crossColor = IM_COL32(255, 255, 255, 180);
        draw->AddLine(ImVec2(cx - 10.0F, cy), ImVec2(cx - 3.0F, cy), crossColor, 2.0F);
        draw->AddLine(ImVec2(cx + 3.0F, cy), ImVec2(cx + 10.0F, cy), crossColor, 2.0F);
        draw->AddLine(ImVec2(cx, cy - 10.0F), ImVec2(cx, cy - 3.0F), crossColor, 2.0F);
        draw->AddLine(ImVec2(cx, cy + 3.0F), ImVec2(cx, cy + 10.0F), crossColor, 2.0F);
    }

    // --------------------------------------------------- FPS counter (top-left)
    if (m_showFPS) {
        if (regular != nullptr) {
            ImGui::PushFont(regular);
        }
        char fpsText[32];
        std::snprintf(fpsText, sizeof(fpsText), "%.0f FPS", m_fps);
        draw->AddText(ImVec2(16.0F, 16.0F), IM_COL32(180, 180, 200, 220), fpsText);
        if (regular != nullptr) {
            ImGui::PopFont();
        }
    }

    // ------------------------------------- Low-health vignette (pulses red)
    if (m_lowHealthWarning) {
        const float pulse = (std::sin(m_lowHealthPulse * 6.0F) * 0.5F) + 0.5F;
        const ImU32 vignette = IM_COL32(180, 30, 30, static_cast<int>(60 + pulse * 80));
        draw->AddRect(ImVec2(0.0F, 0.0F), ImVec2(width, height), vignette, 0.0F, 0, 40.0F);
    }

    // --------------------------------------- Notifications (top-left column)
    {
        if (regular != nullptr) {
            ImGui::PushFont(regular);
        }
        float notifY = 80.0F;
        for (const auto& notification : m_notifications) {
            const float fade = std::clamp(1.0F - (notification.elapsed / notification.duration), 0.0F, 1.0F);
            const auto baseColor = getNotificationColor(notification.type);
            const ImU32 color = IM_COL32(
                static_cast<int>(baseColor[0] * 255.0F),
                static_cast<int>(baseColor[1] * 255.0F),
                static_cast<int>(baseColor[2] * 255.0F),
                static_cast<int>(baseColor[3] * 255.0F * fade));
            draw->AddText(ImVec2(24.0F, notifY), color, notification.message.c_str());
            notifY += 26.0F;
        }
        if (regular != nullptr) {
            ImGui::PopFont();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ============================================================================
// Data Setters
// ============================================================================

void HUD::setHealth(float current, float max) {
    m_currentHealth = current;
    m_maxHealth = max;

    // Auto-enable low health warning below 30%
    float healthPercent = current / max;
    if (healthPercent < 0.3F && healthPercent > 0.0F) {
        setLowHealthWarning(true);
    } else {
        setLowHealthWarning(false);
    }
}

void HUD::setWave(uint32_t wave) {
    m_currentWave = wave;
}

void HUD::setEnemyCount(uint32_t remaining, uint32_t total) {
    m_remainingEnemies = remaining;
    m_totalEnemies = total;
}

void HUD::setScore(uint32_t score) {
    m_score = score;
}

void HUD::setCombo(uint32_t combo) {
    m_combo = combo;
    if (combo > 0) {
        m_comboDisplayTime = 0.0F;
    }
}

// ============================================================================
// Visual Effects
// ============================================================================

void HUD::showDamageIndicator(const std::array<float, 2>& direction, float intensity) {
    DamageIndicator indicator;
    indicator.direction = direction;
    indicator.intensity = intensity;
    indicator.lifetime = 0.0F;

    m_damageIndicators.push_back(indicator);
}

void HUD::showDamageNumber(float damage,
                           const std::array<float, 2>& screenPosition,
                           bool isCritical) {
    DamageNumber number;
    number.amount = damage;
    number.position = screenPosition;
    number.velocity = {0.0F, -50.0F};
    number.lifetime = 0.0F;
    number.isCritical = isCritical;
    number.isHeal = false;

    m_damageNumbers.push_back(number);
}

void HUD::showHealNumber(float amount, const std::array<float, 2>& screenPosition) {
    DamageNumber number;
    number.amount = amount;
    number.position = screenPosition;
    number.velocity = {0.0F, -50.0F};
    number.lifetime = 0.0F;
    number.isCritical = false;
    number.isHeal = true;

    m_damageNumbers.push_back(number);
}

void HUD::setLowHealthWarning(bool enable) {
    m_lowHealthWarning = enable;
    if (enable) {
        m_lowHealthPulse = 0.0F;
    }
}

void HUD::setFPS(float fps) {
    m_fps = fps;
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void HUD::renderHealthBar(CatEngine::Renderer::UIPass& uiPass) {
    float healthPercent = m_currentHealth / m_maxHealth;

    // Health bar background (top-left)
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 20.0F;
    bgQuad.y = 20.0F;
    bgQuad.width = 200.0F;
    bgQuad.height = 30.0F;
    bgQuad.r = 0.2F;
    bgQuad.g = 0.2F;
    bgQuad.b = 0.2F;
    bgQuad.a = 0.8F;
    bgQuad.depth = 0.0F;
    uiPass.DrawQuad(bgQuad);

    // Health bar fill
    CatEngine::Renderer::UIPass::QuadDesc fillQuad;
    fillQuad.x = 20.0F;
    fillQuad.y = 20.0F;
    fillQuad.width = 200.0F * healthPercent;
    fillQuad.height = 30.0F;
    if (healthPercent > 0.5F) {
        fillQuad.r = 0.0F;
        fillQuad.g = 0.8F;
        fillQuad.b = 0.0F;
    } else {
        fillQuad.r = 0.8F;
        fillQuad.g = 0.0F;
        fillQuad.b = 0.0F;
    }
    fillQuad.a = 1.0F;
    fillQuad.depth = 0.1F;
    uiPass.DrawQuad(fillQuad);

    // Health text
    std::string healthText = std::to_string(static_cast<int>(m_currentHealth)) + " / " +
                             std::to_string(static_cast<int>(m_maxHealth));
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = healthText.c_str();
    textDesc.x = 25.0F;
    textDesc.y = 25.0F;
    textDesc.fontSize = 16.0F;
    textDesc.r = 1.0F;
    textDesc.g = 1.0F;
    textDesc.b = 1.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.2F;
    uiPass.DrawText(textDesc);
}

void HUD::renderWaveCounter(CatEngine::Renderer::UIPass& uiPass) {
    std::string waveText = "WAVE " + std::to_string(m_currentWave);
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = waveText.c_str();
    textDesc.x = (static_cast<float>(m_screenWidth) / 2.0F) - 50.0F;
    textDesc.y = 20.0F;
    textDesc.fontSize = 24.0F;
    textDesc.r = 1.0F;
    textDesc.g = 1.0F;
    textDesc.b = 0.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);
}

void HUD::renderEnemyCounter(CatEngine::Renderer::UIPass& uiPass) {
    std::string enemyText = "Enemies: " + std::to_string(m_remainingEnemies) +
                           " / " + std::to_string(m_totalEnemies);
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = enemyText.c_str();
    textDesc.x = static_cast<float>(m_screenWidth) - 200.0F;
    textDesc.y = 20.0F;
    textDesc.fontSize = 18.0F;
    textDesc.r = 1.0F;
    textDesc.g = 0.5F;
    textDesc.b = 0.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);
}

void HUD::renderScore(CatEngine::Renderer::UIPass& uiPass) {
    std::string scoreText = "Score: " + std::to_string(m_score);
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = scoreText.c_str();
    textDesc.x = 20.0F;
    textDesc.y = 60.0F;
    textDesc.fontSize = 18.0F;
    textDesc.r = 1.0F;
    textDesc.g = 1.0F;
    textDesc.b = 1.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);

    // Render combo if active
    if (m_combo > 1 && m_comboDisplayTime < m_comboFadeTime) {
        float fadeAlpha = 1.0F - (m_comboDisplayTime / m_comboFadeTime);
        std::string comboText = "COMBO x" + std::to_string(m_combo);
        CatEngine::Renderer::UIPass::TextDesc comboDesc;
        comboDesc.text = comboText.c_str();
        comboDesc.x = 20.0F;
        comboDesc.y = 90.0F;
        comboDesc.fontSize = 22.0F;
        comboDesc.r = 1.0F;
        comboDesc.g = 0.8F;
        comboDesc.b = 0.0F;
        comboDesc.a = fadeAlpha;
        comboDesc.depth = 0.0F;
        uiPass.DrawText(comboDesc);
    }
}

void HUD::renderCrosshair(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;
    float centerY = static_cast<float>(m_screenHeight) / 2.0F;
    float size = 10.0F;
    float thickness = 2.0F;

    // Horizontal line
    CatEngine::Renderer::UIPass::QuadDesc hLine;
    hLine.x = centerX - size;
    hLine.y = centerY - (thickness / 2.0F);
    hLine.width = size * 2.0F;
    hLine.height = thickness;
    hLine.r = 1.0F;
    hLine.g = 1.0F;
    hLine.b = 1.0F;
    hLine.a = 0.8F;
    hLine.depth = 0.0F;
    uiPass.DrawQuad(hLine);

    // Vertical line
    CatEngine::Renderer::UIPass::QuadDesc vLine;
    vLine.x = centerX - (thickness / 2.0F);
    vLine.y = centerY - size;
    vLine.width = thickness;
    vLine.height = size * 2.0F;
    vLine.r = 1.0F;
    vLine.g = 1.0F;
    vLine.b = 1.0F;
    vLine.a = 0.8F;
    vLine.depth = 0.0F;
    uiPass.DrawQuad(vLine);
}

void HUD::renderDamageIndicators(CatEngine::Renderer::UIPass& uiPass) {
    float screenCenterX = static_cast<float>(m_screenWidth) / 2.0F;
    float screenCenterY = static_cast<float>(m_screenHeight) / 2.0F;
    float edgeDistance = std::min(static_cast<float>(m_screenWidth),
                                   static_cast<float>(m_screenHeight)) * 0.4F;

    for (const auto& indicator : m_damageIndicators) {
        float alpha = 1.0F - (indicator.lifetime / indicator.maxLifetime);
        alpha *= indicator.intensity;

        // Calculate edge position based on direction
        float angle = std::atan2(indicator.direction[1], indicator.direction[0]);
        float edgeX = screenCenterX + std::cos(angle) * edgeDistance;
        float edgeY = screenCenterY + std::sin(angle) * edgeDistance;

        // Draw damage indicator as red quad
        CatEngine::Renderer::UIPass::QuadDesc indicatorQuad;
        indicatorQuad.x = edgeX - 15.0F;
        indicatorQuad.y = edgeY - 15.0F;
        indicatorQuad.width = 30.0F;
        indicatorQuad.height = 30.0F;
        indicatorQuad.r = 1.0F;
        indicatorQuad.g = 0.0F;
        indicatorQuad.b = 0.0F;
        indicatorQuad.a = alpha;
        indicatorQuad.depth = 0.5F;
        uiPass.DrawQuad(indicatorQuad);
    }
}

void HUD::renderDamageNumbers(CatEngine::Renderer::UIPass& uiPass) {
    for (const auto& number : m_damageNumbers) {
        float alpha = 1.0F - (number.lifetime / number.maxLifetime);

        float r, g, b;
        if (number.isHeal) {
            r = 0.0F; g = 1.0F; b = 0.0F;
        } else if (number.isCritical) {
            r = 1.0F; g = 0.5F; b = 0.0F;
        } else {
            r = 1.0F; g = 1.0F; b = 1.0F;
        }

        std::string text = number.isCritical ?
            std::to_string(static_cast<int>(number.amount)) + "!" :
            std::to_string(static_cast<int>(number.amount));

        float fontSize = number.isCritical ? 28.0F : 20.0F;

        CatEngine::Renderer::UIPass::TextDesc textDesc;
        textDesc.text = text.c_str();
        textDesc.x = number.position[0];
        textDesc.y = number.position[1];
        textDesc.fontSize = fontSize;
        textDesc.r = r;
        textDesc.g = g;
        textDesc.b = b;
        textDesc.a = alpha;
        textDesc.depth = 0.8F;
        uiPass.DrawText(textDesc);
    }
}

void HUD::renderLowHealthWarning(CatEngine::Renderer::UIPass& uiPass) {
    // Pulsing red glow at screen edges using vignette quads
    float pulseIntensity = (std::sin(m_lowHealthPulse) + 1.0F) * 0.5F;
    float alpha = 0.3F * pulseIntensity;

    // Top edge
    CatEngine::Renderer::UIPass::QuadDesc topEdge;
    topEdge.x = 0.0F;
    topEdge.y = 0.0F;
    topEdge.width = static_cast<float>(m_screenWidth);
    topEdge.height = 50.0F;
    topEdge.r = 1.0F;
    topEdge.g = 0.0F;
    topEdge.b = 0.0F;
    topEdge.a = alpha;
    topEdge.depth = 0.9F;
    uiPass.DrawQuad(topEdge);

    // Bottom edge
    CatEngine::Renderer::UIPass::QuadDesc bottomEdge;
    bottomEdge.x = 0.0F;
    bottomEdge.y = static_cast<float>(m_screenHeight) - 50.0F;
    bottomEdge.width = static_cast<float>(m_screenWidth);
    bottomEdge.height = 50.0F;
    bottomEdge.r = 1.0F;
    bottomEdge.g = 0.0F;
    bottomEdge.b = 0.0F;
    bottomEdge.a = alpha;
    bottomEdge.depth = 0.9F;
    uiPass.DrawQuad(bottomEdge);

    // Left edge
    CatEngine::Renderer::UIPass::QuadDesc leftEdge;
    leftEdge.x = 0.0F;
    leftEdge.y = 0.0F;
    leftEdge.width = 50.0F;
    leftEdge.height = static_cast<float>(m_screenHeight);
    leftEdge.r = 1.0F;
    leftEdge.g = 0.0F;
    leftEdge.b = 0.0F;
    leftEdge.a = alpha;
    leftEdge.depth = 0.9F;
    uiPass.DrawQuad(leftEdge);

    // Right edge
    CatEngine::Renderer::UIPass::QuadDesc rightEdge;
    rightEdge.x = static_cast<float>(m_screenWidth) - 50.0F;
    rightEdge.y = 0.0F;
    rightEdge.width = 50.0F;
    rightEdge.height = static_cast<float>(m_screenHeight);
    rightEdge.r = 1.0F;
    rightEdge.g = 0.0F;
    rightEdge.b = 0.0F;
    rightEdge.a = alpha;
    rightEdge.depth = 0.9F;
    uiPass.DrawQuad(rightEdge);
}

void HUD::renderFPS(CatEngine::Renderer::UIPass& uiPass) {
    std::string fpsText = "FPS: " + std::to_string(static_cast<int>(m_fps));
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = fpsText.c_str();
    textDesc.x = static_cast<float>(m_screenWidth) - 100.0F;
    textDesc.y = static_cast<float>(m_screenHeight) - 30.0F;
    textDesc.fontSize = 16.0F;
    textDesc.r = 0.0F;
    textDesc.g = 1.0F;
    textDesc.b = 0.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);
}

// ============================================================================
// Private Update Methods
// ============================================================================

void HUD::updateDamageIndicators(float deltaTime) {
    // Update and remove expired indicators
    m_damageIndicators.erase(
        std::remove_if(m_damageIndicators.begin(), m_damageIndicators.end(),
            [deltaTime](DamageIndicator& indicator) {
                indicator.lifetime += deltaTime;
                return indicator.lifetime >= indicator.maxLifetime;
            }),
        m_damageIndicators.end()
    );
}

void HUD::updateDamageNumbers(float deltaTime) {
    // Update positions and remove expired numbers
    for (auto& number : m_damageNumbers) {
        number.lifetime += deltaTime;
        number.position[0] += number.velocity[0] * deltaTime;
        number.position[1] += number.velocity[1] * deltaTime;

        // Slow down over time
        number.velocity[1] *= 0.95F;
    }

    m_damageNumbers.erase(
        std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
            [](const DamageNumber& number) {
                return number.lifetime >= number.maxLifetime;
            }),
        m_damageNumbers.end()
    );
}

// ============================================================================
// Notification System
// ============================================================================

void HUD::showNotification(const std::string& message, float duration, int priority) {
    Notification notification;
    notification.message = message;
    notification.type = NotificationType::Info;
    notification.duration = duration;
    notification.elapsed = 0.0F;
    notification.priority = priority;

    // Insert sorted by priority (higher priority first)
    auto insertPos = std::find_if(m_notifications.begin(), m_notifications.end(),
        [priority](const Notification& n) { return n.priority < priority; });
    m_notifications.insert(insertPos, notification);

    // Limit number of notifications
    if (m_notifications.size() > MAX_NOTIFICATIONS) {
        m_notifications.pop_back();
    }

    Engine::Logger::debug("Notification shown: " + message);
}

void HUD::showNotification(const std::string& message, const std::string& type, float duration) {
    NotificationType notificationType = NotificationType::Info;

    if (type == "success") {
        notificationType = NotificationType::Success;
    } else if (type == "warning") {
        notificationType = NotificationType::Warning;
    } else if (type == "error") {
        notificationType = NotificationType::Error;
    }

    Notification notification;
    notification.message = message;
    notification.type = notificationType;
    notification.duration = duration;
    notification.elapsed = 0.0F;
    if (notificationType == NotificationType::Error) {
        notification.priority = 10;
    } else if (notificationType == NotificationType::Warning) {
        notification.priority = 5;
    } else if (notificationType == NotificationType::Success) {
        notification.priority = 3;
    } else {
        notification.priority = 0;
    }

    // Insert sorted by priority (higher priority first)
    auto insertPos = std::find_if(m_notifications.begin(), m_notifications.end(),
        [&notification](const Notification& n) { return n.priority < notification.priority; });
    m_notifications.insert(insertPos, notification);

    // Limit number of notifications
    if (m_notifications.size() > MAX_NOTIFICATIONS) {
        m_notifications.pop_back();
    }

    Engine::Logger::debug("Notification shown (" + type + "): " + message.c_str());
}

void HUD::clearNotifications() {
    m_notifications.clear();
}

void HUD::updateNotifications(float deltaTime) {
    // Update elapsed time and remove expired notifications
    m_notifications.erase(
        std::remove_if(m_notifications.begin(), m_notifications.end(),
            [deltaTime](Notification& notification) {
                notification.elapsed += deltaTime;
                return notification.elapsed >= notification.duration;
            }),
        m_notifications.end()
    );
}

void HUD::renderNotifications(CatEngine::Renderer::UIPass& uiPass) {
    float yOffset = 60.0F;
    float xPosition = static_cast<float>(m_screenWidth) - 320.0F;
    float notificationHeight = 40.0F;
    float padding = 10.0F;

    for (const auto& notification : m_notifications) {
        // Calculate fade alpha (fade in first 0.2s, fade out last 0.5s)
        float fadeIn = std::min(notification.elapsed / 0.2F, 1.0F);
        float fadeOut = std::min((notification.duration - notification.elapsed) / 0.5F, 1.0F);
        float alpha = fadeIn * fadeOut;

        // Get color based on type
        std::array<float, 4> colorArr = getNotificationColor(notification.type);

        // Draw background
        CatEngine::Renderer::UIPass::QuadDesc bgQuad;
        bgQuad.x = xPosition;
        bgQuad.y = yOffset;
        bgQuad.width = 300.0F;
        bgQuad.height = notificationHeight;
        bgQuad.r = 0.1F;
        bgQuad.g = 0.1F;
        bgQuad.b = 0.1F;
        bgQuad.a = 0.8F * alpha;
        bgQuad.depth = 0.7F;
        uiPass.DrawQuad(bgQuad);

        // Draw colored left border
        CatEngine::Renderer::UIPass::QuadDesc borderQuad;
        borderQuad.x = xPosition;
        borderQuad.y = yOffset;
        borderQuad.width = 4.0F;
        borderQuad.height = notificationHeight;
        borderQuad.r = colorArr[0];
        borderQuad.g = colorArr[1];
        borderQuad.b = colorArr[2];
        borderQuad.a = colorArr[3] * alpha;
        borderQuad.depth = 0.75F;
        uiPass.DrawQuad(borderQuad);

        // Draw text
        CatEngine::Renderer::UIPass::TextDesc textDesc;
        textDesc.text = notification.message.c_str();
        textDesc.x = xPosition + 12.0F;
        textDesc.y = yOffset + 10.0F;
        textDesc.fontSize = 16.0F;
        textDesc.r = 1.0F;
        textDesc.g = 1.0F;
        textDesc.b = 1.0F;
        textDesc.a = alpha;
        textDesc.depth = 0.8F;
        uiPass.DrawText(textDesc);

        yOffset += notificationHeight + padding;
    }
}

std::array<float, 4> HUD::getNotificationColor(NotificationType type) {
    switch (type) {
        case NotificationType::Success:
            return {0.2F, 0.8F, 0.2F, 1.0F};
        case NotificationType::Warning:
            return {1.0F, 0.8F, 0.0F, 1.0F};
        case NotificationType::Error:
            return {1.0F, 0.2F, 0.2F, 1.0F};
        case NotificationType::Info:
        default:
            return {0.3F, 0.6F, 1.0F, 1.0F};
    }
}

} // namespace Game

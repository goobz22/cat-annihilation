#include "hud_ui.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace Game {

HUD_UI::HUD_UI(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

HUD_UI::~HUD_UI() {
    shutdown();
}

bool HUD_UI::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("HUD_UI already initialized");
        return true;
    }

    // Initialize compass
    m_compass = std::make_unique<CompassUI>(m_input);
    if (!m_compass->initialize()) {
        Engine::Logger::error("HUD_UI: Failed to initialize CompassUI");
        return false;
    }

    // Initialize minimap
    m_minimap = std::make_unique<MinimapUI>(m_input);
    if (!m_minimap->initialize()) {
        Engine::Logger::error("HUD_UI: Failed to initialize MinimapUI");
        return false;
    }

    // Initialize spell slots
    for (int i = 0; i < 4; i++) {
        m_spellSlots[static_cast<size_t>(i)].slotNumber = i + 1;
        m_spellSlots[static_cast<size_t>(i)].isReady = true;
    }

    m_initialized = true;
    Engine::Logger::info("HUD_UI initialized successfully");
    return true;
}

void HUD_UI::shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_compass != nullptr) {
        m_compass->shutdown();
        m_compass.reset();
    }

    if (m_minimap != nullptr) {
        m_minimap->shutdown();
        m_minimap.reset();
    }

    m_trackedQuests.clear();
    m_buffs.clear();
    m_damageNumbers.clear();
    m_damageIndicators.clear();

    m_initialized = false;
    Engine::Logger::info("HUD_UI shutdown");
}

void HUD_UI::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update compass
    if (m_compass != nullptr && m_compass->isVisible()) {
        m_compass->update(deltaTime, m_playerPosition, m_playerYaw);
    }

    // Update minimap
    if (m_minimap != nullptr && m_minimap->isVisible()) {
        m_minimap->update(deltaTime, m_playerPosition, m_playerYaw);
    }

    // Update damage numbers
    updateDamageNumbers(deltaTime);

    // Update damage indicators
    updateDamageIndicators(deltaTime);

    // Update buffs
    updateBuffs(deltaTime);

    // Update low health warning
    if (m_healthBar.isLowHealth()) {
        m_lowHealthWarning = true;
        m_lowHealthPulse += deltaTime * 4.0F;
    } else {
        m_lowHealthWarning = false;
        m_lowHealthPulse = 0.0F;
    }
}

void HUD_UI::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized || !m_hudVisible || m_cinematicMode) {
        return;
    }

    // Cache screen dimensions
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Render low health warning (background layer)
    if (m_lowHealthWarning && !m_minimalMode) {
        renderLowHealthWarning(uiPass);
    }

    // Render damage indicators
    if (!m_minimalMode) {
        renderDamageIndicators(uiPass);
    }

    // Render health bar (always visible)
    renderHealthBar(uiPass);

    if (!m_minimalMode) {
        // Render mana and stamina bars
        renderManaBar(uiPass);
        renderStaminaBar(uiPass);

        // Render level and currency
        renderLevelAndCurrency(uiPass);

        // Render experience bar
        renderExperienceBar(uiPass);

        // Render quest tracker
        if (!m_trackedQuests.empty()) {
            renderQuestTracker(uiPass);
        }

        // Render spell slots
        renderSpellSlots(uiPass);

        // Render buff bar
        if (!m_buffs.empty()) {
            renderBuffBar(uiPass);
        }

        // Render compass
        if (m_compass != nullptr) {
            m_compass->render(uiPass, screenWidth, screenHeight);
        }

        // Render minimap
        if (m_minimap != nullptr) {
            m_minimap->render(uiPass, screenWidth, screenHeight);
        }
    }

    // Render crosshair (always centered)
    if (m_showCrosshair) {
        renderCrosshair(uiPass);
    }

    // Render damage numbers
    renderDamageNumbers(uiPass);

    // Render FPS counter
    if (m_showFPS) {
        renderFPS(uiPass);
    }
}

// ============================================================================
// Visibility Modes
// ============================================================================

void HUD_UI::setMinimalMode(bool minimal) {
    m_minimalMode = minimal;
    if (minimal) {
        if (m_compass != nullptr) {
            m_compass->setVisible(false);
        }
        if (m_minimap != nullptr) {
            m_minimap->setVisible(false);
        }
    }
}

void HUD_UI::setCinematicMode(bool cinematic) {
    m_cinematicMode = cinematic;
    if (cinematic) {
        if (m_compass != nullptr) {
            m_compass->setVisible(false);
        }
        if (m_minimap != nullptr) {
            m_minimap->setVisible(false);
        }
    }
}

// ============================================================================
// Player Stats
// ============================================================================

void HUD_UI::setHealth(float current, float max) {
    m_healthBar.setHealth(current, max);
}

void HUD_UI::setMana(float current, float max) {
    m_manaBar.setMana(current, max);
}

void HUD_UI::setStamina(float current, float max) {
    m_staminaBar.setStamina(current, max);
}

void HUD_UI::setExperience(int current, int required) {
    m_currentXP = current;
    m_requiredXP = required;
}

void HUD_UI::setPlayerPosition(const Engine::vec3& pos, float yaw) {
    m_playerPosition = pos;
    m_playerYaw = yaw;
}

// ============================================================================
// Quest Tracker
// ============================================================================

void HUD_UI::addTrackedQuest(const QuestTrackerEntry& quest) {
    for (const auto& q : m_trackedQuests) {
        if (q.questId == quest.questId) {
            return;
        }
    }

    if (static_cast<int>(m_trackedQuests.size()) < m_maxTrackedQuests) {
        m_trackedQuests.push_back(quest);
        Engine::Logger::debug("HUD_UI: Added tracked quest '" + quest.questTitle + "'");
    }
}

void HUD_UI::removeTrackedQuest(const std::string& questId) {
    m_trackedQuests.erase(
        std::remove_if(m_trackedQuests.begin(), m_trackedQuests.end(),
            [&questId](const QuestTrackerEntry& q) { return q.questId == questId; }),
        m_trackedQuests.end()
    );
}

void HUD_UI::updateQuestProgress(const std::string& questId, int current, int total) {
    for (auto& quest : m_trackedQuests) {
        if (quest.questId == questId) {
            quest.objectiveProgress = current;
            quest.objectiveTotal = total;
            break;
        }
    }
}

void HUD_UI::clearTrackedQuests() {
    m_trackedQuests.clear();
}

// ============================================================================
// Spell Slots
// ============================================================================

void HUD_UI::setSpellSlot(int slotNumber, const std::string& spellId, const std::string& iconPath) {
    if (slotNumber >= 1 && slotNumber <= 4) {
        auto& slot = m_spellSlots[static_cast<size_t>(slotNumber - 1)];
        slot.spellId = spellId;
        slot.iconPath = iconPath;
        slot.isReady = true;
    }
}

void HUD_UI::setSpellCooldown(int slotNumber, float remaining, float total) {
    if (slotNumber >= 1 && slotNumber <= 4) {
        auto& slot = m_spellSlots[static_cast<size_t>(slotNumber - 1)];
        slot.cooldownRemaining = remaining;
        slot.cooldownTotal = total;
        slot.isReady = (remaining <= 0.0F);
    }
}

void HUD_UI::clearSpellSlot(int slotNumber) {
    if (slotNumber >= 1 && slotNumber <= 4) {
        auto& slot = m_spellSlots[static_cast<size_t>(slotNumber - 1)];
        slot.spellId.clear();
        slot.iconPath.clear();
        slot.cooldownRemaining = 0.0F;
        slot.cooldownTotal = 0.0F;
        slot.isReady = true;
    }
}

// ============================================================================
// Buffs/Debuffs
// ============================================================================

void HUD_UI::addBuff(const BuffIndicator& buff) {
    for (auto& b : m_buffs) {
        if (b.id == buff.id) {
            b.stackCount++;
            b.remainingTime = buff.duration;
            return;
        }
    }
    m_buffs.push_back(buff);
}

void HUD_UI::removeBuff(const std::string& buffId) {
    m_buffs.erase(
        std::remove_if(m_buffs.begin(), m_buffs.end(),
            [&buffId](const BuffIndicator& b) { return b.id == buffId; }),
        m_buffs.end()
    );
}

void HUD_UI::updateBuffDuration(const std::string& buffId, float remainingTime) {
    for (auto& buff : m_buffs) {
        if (buff.id == buffId) {
            buff.remainingTime = remainingTime;
            break;
        }
    }
}

void HUD_UI::clearBuffs() {
    m_buffs.clear();
}

// ============================================================================
// Visual Effects
// ============================================================================

void HUD_UI::showDamageNumber(float damage, const Engine::vec2& screenPos, bool isCritical) {
    DamageNumber dmg;
    dmg.amount = damage;
    dmg.position = screenPos;
    dmg.velocity = {0.0F, -50.0F};
    dmg.lifetime = dmg.maxLifetime;
    dmg.isCritical = isCritical;
    dmg.isHeal = false;
    m_damageNumbers.push_back(dmg);
}

void HUD_UI::showHealNumber(float amount, const Engine::vec2& screenPos) {
    DamageNumber heal;
    heal.amount = amount;
    heal.position = screenPos;
    heal.velocity = {0.0F, -50.0F};
    heal.lifetime = heal.maxLifetime;
    heal.isCritical = false;
    heal.isHeal = true;
    m_damageNumbers.push_back(heal);
}

void HUD_UI::showDamageIndicator(const Engine::vec2& direction, float intensity) {
    DamageIndicator indicator;
    indicator.direction = direction;
    indicator.intensity = intensity;
    indicator.lifetime = indicator.maxLifetime;
    m_damageIndicators.push_back(indicator);
}

// ============================================================================
// Display Options
// ============================================================================

void HUD_UI::setShowMinimap(bool show) {
    if (m_minimap != nullptr) {
        m_minimap->setVisible(show);
    }
}

void HUD_UI::setShowCompass(bool show) {
    if (m_compass != nullptr) {
        m_compass->setVisible(show);
    }
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void HUD_UI::renderHealthBar(CatEngine::Renderer::UIPass& uiPass) {
    float healthPercent = m_healthBar.getHealthPercent();

    // Health bar background
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 20.0F;
    bgQuad.y = 20.0F;
    bgQuad.width = 200.0F;
    bgQuad.height = 25.0F;
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
    fillQuad.height = 25.0F;
    fillQuad.r = 0.8F;
    fillQuad.g = 0.1F;
    fillQuad.b = 0.1F;
    fillQuad.a = 1.0F;
    fillQuad.depth = 0.1F;
    uiPass.DrawQuad(fillQuad);

    // Health text
    std::string healthText = std::to_string(static_cast<int>(m_healthBar.currentHealth)) + " / " +
                             std::to_string(static_cast<int>(m_healthBar.maxHealth));
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = healthText.c_str();
    textDesc.x = 25.0F;
    textDesc.y = 22.0F;
    textDesc.fontSize = 14.0F;
    textDesc.r = 1.0F;
    textDesc.g = 1.0F;
    textDesc.b = 1.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.2F;
    uiPass.DrawText(textDesc);
}

void HUD_UI::renderManaBar(CatEngine::Renderer::UIPass& uiPass) {
    float manaPercent = m_manaBar.getManaPercent();

    // Mana bar background
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 20.0F;
    bgQuad.y = 50.0F;
    bgQuad.width = 200.0F;
    bgQuad.height = 20.0F;
    bgQuad.r = 0.2F;
    bgQuad.g = 0.2F;
    bgQuad.b = 0.2F;
    bgQuad.a = 0.8F;
    bgQuad.depth = 0.0F;
    uiPass.DrawQuad(bgQuad);

    // Mana bar fill
    CatEngine::Renderer::UIPass::QuadDesc fillQuad;
    fillQuad.x = 20.0F;
    fillQuad.y = 50.0F;
    fillQuad.width = 200.0F * manaPercent;
    fillQuad.height = 20.0F;
    fillQuad.r = 0.1F;
    fillQuad.g = 0.3F;
    fillQuad.b = 0.9F;
    fillQuad.a = 1.0F;
    fillQuad.depth = 0.1F;
    uiPass.DrawQuad(fillQuad);
}

void HUD_UI::renderStaminaBar(CatEngine::Renderer::UIPass& uiPass) {
    float staminaPercent = m_staminaBar.getStaminaPercent();

    // Stamina bar background
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 20.0F;
    bgQuad.y = 75.0F;
    bgQuad.width = 200.0F;
    bgQuad.height = 15.0F;
    bgQuad.r = 0.2F;
    bgQuad.g = 0.2F;
    bgQuad.b = 0.2F;
    bgQuad.a = 0.8F;
    bgQuad.depth = 0.0F;
    uiPass.DrawQuad(bgQuad);

    // Stamina bar fill
    CatEngine::Renderer::UIPass::QuadDesc fillQuad;
    fillQuad.x = 20.0F;
    fillQuad.y = 75.0F;
    fillQuad.width = 200.0F * staminaPercent;
    fillQuad.height = 15.0F;
    fillQuad.r = 0.8F;
    fillQuad.g = 0.8F;
    fillQuad.b = 0.1F;
    fillQuad.a = 1.0F;
    fillQuad.depth = 0.1F;
    uiPass.DrawQuad(fillQuad);
}

void HUD_UI::renderQuestTracker(CatEngine::Renderer::UIPass& uiPass) {
    float xPos = static_cast<float>(m_screenWidth) - 280.0F;
    float yPos = 100.0F;
    float entryHeight = 60.0F;

    for (const auto& quest : m_trackedQuests) {
        // Quest background
        CatEngine::Renderer::UIPass::QuadDesc bgQuad;
        bgQuad.x = xPos;
        bgQuad.y = yPos;
        bgQuad.width = 260.0F;
        bgQuad.height = entryHeight - 5.0F;
        bgQuad.r = 0.1F;
        bgQuad.g = 0.1F;
        bgQuad.b = 0.1F;
        bgQuad.a = 0.7F;
        bgQuad.depth = 0.0F;
        uiPass.DrawQuad(bgQuad);

        // Quest title
        CatEngine::Renderer::UIPass::TextDesc titleDesc;
        titleDesc.text = quest.questTitle.c_str();
        titleDesc.x = xPos + 10.0F;
        titleDesc.y = yPos + 5.0F;
        titleDesc.fontSize = 14.0F;
        titleDesc.r = 1.0F;
        titleDesc.g = 0.9F;
        titleDesc.b = 0.3F;
        titleDesc.a = 1.0F;
        titleDesc.depth = 0.1F;
        uiPass.DrawText(titleDesc);

        // Quest objective
        CatEngine::Renderer::UIPass::TextDesc objDesc;
        objDesc.text = quest.currentObjective.c_str();
        objDesc.x = xPos + 10.0F;
        objDesc.y = yPos + 22.0F;
        objDesc.fontSize = 12.0F;
        objDesc.r = 0.8F;
        objDesc.g = 0.8F;
        objDesc.b = 0.8F;
        objDesc.a = 1.0F;
        objDesc.depth = 0.1F;
        uiPass.DrawText(objDesc);

        // Progress bar background
        CatEngine::Renderer::UIPass::QuadDesc progBg;
        progBg.x = xPos + 10.0F;
        progBg.y = yPos + 40.0F;
        progBg.width = 240.0F;
        progBg.height = 8.0F;
        progBg.r = 0.3F;
        progBg.g = 0.3F;
        progBg.b = 0.3F;
        progBg.a = 1.0F;
        progBg.depth = 0.1F;
        uiPass.DrawQuad(progBg);

        // Progress bar fill
        float progress = (quest.objectiveTotal > 0) ?
            static_cast<float>(quest.objectiveProgress) / static_cast<float>(quest.objectiveTotal) : 0.0F;
        CatEngine::Renderer::UIPass::QuadDesc progFill;
        progFill.x = xPos + 10.0F;
        progFill.y = yPos + 40.0F;
        progFill.width = 240.0F * progress;
        progFill.height = 8.0F;
        progFill.r = 0.2F;
        progFill.g = 0.8F;
        progFill.b = 0.2F;
        progFill.a = 1.0F;
        progFill.depth = 0.2F;
        uiPass.DrawQuad(progFill);

        yPos += entryHeight;
    }
}

void HUD_UI::renderSpellSlots(CatEngine::Renderer::UIPass& uiPass) {
    float slotSize = 50.0F;
    float spacing = 10.0F;
    float totalWidth = 4.0F * slotSize + 3.0F * spacing;
    float startX = (static_cast<float>(m_screenWidth) - totalWidth) / 2.0F;
    float yPos = static_cast<float>(m_screenHeight) - slotSize - 30.0F;

    for (size_t i = 0; i < 4; ++i) {
        const auto& slot = m_spellSlots[i];
        float xPos = startX + static_cast<float>(i) * (slotSize + spacing);

        // Slot background
        CatEngine::Renderer::UIPass::QuadDesc bgQuad;
        bgQuad.x = xPos;
        bgQuad.y = yPos;
        bgQuad.width = slotSize;
        bgQuad.height = slotSize;
        bgQuad.r = 0.2F;
        bgQuad.g = 0.2F;
        bgQuad.b = 0.2F;
        bgQuad.a = 0.8F;
        bgQuad.depth = 0.0F;
        uiPass.DrawQuad(bgQuad);

        // Cooldown overlay
        if (!slot.isReady && slot.cooldownTotal > 0.0F) {
            float cooldownPercent = slot.getCooldownPercent();
            CatEngine::Renderer::UIPass::QuadDesc cdQuad;
            cdQuad.x = xPos;
            cdQuad.y = yPos;
            cdQuad.width = slotSize;
            cdQuad.height = slotSize * cooldownPercent;
            cdQuad.r = 0.0F;
            cdQuad.g = 0.0F;
            cdQuad.b = 0.0F;
            cdQuad.a = 0.6F;
            cdQuad.depth = 0.2F;
            uiPass.DrawQuad(cdQuad);
        }

        // Slot number
        std::string slotNum = std::to_string(slot.slotNumber);
        CatEngine::Renderer::UIPass::TextDesc numDesc;
        numDesc.text = slotNum.c_str();
        numDesc.x = xPos + 3.0F;
        numDesc.y = yPos + 3.0F;
        numDesc.fontSize = 12.0F;
        numDesc.r = 1.0F;
        numDesc.g = 1.0F;
        numDesc.b = 1.0F;
        numDesc.a = 0.7F;
        numDesc.depth = 0.3F;
        uiPass.DrawText(numDesc);
    }
}

void HUD_UI::renderBuffBar(CatEngine::Renderer::UIPass& uiPass) {
    float iconSize = 30.0F;
    float spacing = 5.0F;
    float startX = (static_cast<float>(m_screenWidth) / 2.0F) - (static_cast<float>(m_buffs.size()) * (iconSize + spacing) / 2.0F);
    float yPos = 10.0F;

    for (size_t i = 0; i < m_buffs.size(); ++i) {
        const auto& buff = m_buffs[i];
        float xPos = startX + static_cast<float>(i) * (iconSize + spacing);

        // Buff icon background
        CatEngine::Renderer::UIPass::QuadDesc bgQuad;
        bgQuad.x = xPos;
        bgQuad.y = yPos;
        bgQuad.width = iconSize;
        bgQuad.height = iconSize;
        bgQuad.r = buff.isBuff ? 0.1F : 0.3F;
        bgQuad.g = buff.isBuff ? 0.3F : 0.1F;
        bgQuad.b = 0.1F;
        bgQuad.a = 0.8F;
        bgQuad.depth = 0.0F;
        uiPass.DrawQuad(bgQuad);

        // Time remaining indicator
        float timePercent = buff.getTimePercent();
        CatEngine::Renderer::UIPass::QuadDesc timeQuad;
        timeQuad.x = xPos;
        timeQuad.y = yPos + iconSize - 4.0F;
        timeQuad.width = iconSize * timePercent;
        timeQuad.height = 4.0F;
        timeQuad.r = 0.2F;
        timeQuad.g = 0.8F;
        timeQuad.b = 0.2F;
        timeQuad.a = 1.0F;
        timeQuad.depth = 0.1F;
        uiPass.DrawQuad(timeQuad);

        // Stack count
        if (buff.stackCount > 1) {
            std::string stackStr = std::to_string(buff.stackCount);
            CatEngine::Renderer::UIPass::TextDesc stackDesc;
            stackDesc.text = stackStr.c_str();
            stackDesc.x = xPos + iconSize - 10.0F;
            stackDesc.y = yPos + iconSize - 14.0F;
            stackDesc.fontSize = 12.0F;
            stackDesc.r = 1.0F;
            stackDesc.g = 1.0F;
            stackDesc.b = 1.0F;
            stackDesc.a = 1.0F;
            stackDesc.depth = 0.2F;
            uiPass.DrawText(stackDesc);
        }
    }
}

void HUD_UI::renderExperienceBar(CatEngine::Renderer::UIPass& uiPass) {
    float barHeight = 8.0F;
    float yPos = static_cast<float>(m_screenHeight) - barHeight;

    // XP bar background
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = 0.0F;
    bgQuad.y = yPos;
    bgQuad.width = static_cast<float>(m_screenWidth);
    bgQuad.height = barHeight;
    bgQuad.r = 0.1F;
    bgQuad.g = 0.1F;
    bgQuad.b = 0.1F;
    bgQuad.a = 0.8F;
    bgQuad.depth = 0.0F;
    uiPass.DrawQuad(bgQuad);

    // XP bar fill
    float xpPercent = (m_requiredXP > 0) ? static_cast<float>(m_currentXP) / static_cast<float>(m_requiredXP) : 0.0F;
    CatEngine::Renderer::UIPass::QuadDesc fillQuad;
    fillQuad.x = 0.0F;
    fillQuad.y = yPos;
    fillQuad.width = static_cast<float>(m_screenWidth) * xpPercent;
    fillQuad.height = barHeight;
    fillQuad.r = 0.5F;
    fillQuad.g = 0.2F;
    fillQuad.b = 0.8F;
    fillQuad.a = 1.0F;
    fillQuad.depth = 0.1F;
    uiPass.DrawQuad(fillQuad);
}

void HUD_UI::renderLevelAndCurrency(CatEngine::Renderer::UIPass& uiPass) {
    // Level display
    std::string levelText = "Lv. " + std::to_string(m_playerLevel);
    CatEngine::Renderer::UIPass::TextDesc levelDesc;
    levelDesc.text = levelText.c_str();
    levelDesc.x = 20.0F;
    levelDesc.y = 95.0F;
    levelDesc.fontSize = 16.0F;
    levelDesc.r = 1.0F;
    levelDesc.g = 1.0F;
    levelDesc.b = 1.0F;
    levelDesc.a = 1.0F;
    levelDesc.depth = 0.0F;
    uiPass.DrawText(levelDesc);

    // Currency display
    std::string currencyText = std::to_string(m_playerCurrency) + " G";
    CatEngine::Renderer::UIPass::TextDesc currDesc;
    currDesc.text = currencyText.c_str();
    currDesc.x = 20.0F;
    currDesc.y = 115.0F;
    currDesc.fontSize = 14.0F;
    currDesc.r = 1.0F;
    currDesc.g = 0.85F;
    currDesc.b = 0.3F;
    currDesc.a = 1.0F;
    currDesc.depth = 0.0F;
    uiPass.DrawText(currDesc);
}

void HUD_UI::renderCrosshair(CatEngine::Renderer::UIPass& uiPass) {
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

void HUD_UI::renderFPS(CatEngine::Renderer::UIPass& uiPass) {
    std::string fpsText = "FPS: " + std::to_string(static_cast<int>(m_fps));
    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = fpsText.c_str();
    textDesc.x = static_cast<float>(m_screenWidth) - 100.0F;
    textDesc.y = 10.0F;
    textDesc.fontSize = 14.0F;
    textDesc.r = 0.0F;
    textDesc.g = 1.0F;
    textDesc.b = 0.0F;
    textDesc.a = 1.0F;
    textDesc.depth = 0.0F;
    uiPass.DrawText(textDesc);
}

void HUD_UI::renderDamageNumbers(CatEngine::Renderer::UIPass& uiPass) {
    for (const auto& dmg : m_damageNumbers) {
        float alpha = dmg.lifetime / dmg.maxLifetime;

        float r = 1.0F;
        float g = 1.0F;
        float b = 1.0F;

        if (dmg.isHeal) {
            r = 0.2F; g = 1.0F; b = 0.2F;
        } else if (dmg.isCritical) {
            r = 1.0F; g = 0.5F; b = 0.0F;
        }

        std::string text = dmg.isCritical ?
            std::to_string(static_cast<int>(dmg.amount)) + "!" :
            std::to_string(static_cast<int>(dmg.amount));

        float fontSize = dmg.isCritical ? 26.0F : 18.0F;

        CatEngine::Renderer::UIPass::TextDesc textDesc;
        textDesc.text = text.c_str();
        textDesc.x = dmg.position.x;
        textDesc.y = dmg.position.y;
        textDesc.fontSize = fontSize;
        textDesc.r = r;
        textDesc.g = g;
        textDesc.b = b;
        textDesc.a = alpha;
        textDesc.depth = 0.8F;
        uiPass.DrawText(textDesc);
    }
}

void HUD_UI::renderDamageIndicators(CatEngine::Renderer::UIPass& uiPass) {
    float screenCenterX = static_cast<float>(m_screenWidth) / 2.0F;
    float screenCenterY = static_cast<float>(m_screenHeight) / 2.0F;
    float edgeDistance = std::min(static_cast<float>(m_screenWidth),
                                   static_cast<float>(m_screenHeight)) * 0.4F;

    for (const auto& indicator : m_damageIndicators) {
        float alpha = indicator.lifetime / indicator.maxLifetime;
        alpha *= indicator.intensity;

        float angle = std::atan2(indicator.direction.y, indicator.direction.x);
        float edgeX = screenCenterX + std::cos(angle) * edgeDistance;
        float edgeY = screenCenterY + std::sin(angle) * edgeDistance;

        CatEngine::Renderer::UIPass::QuadDesc indicatorQuad;
        indicatorQuad.x = edgeX - 20.0F;
        indicatorQuad.y = edgeY - 20.0F;
        indicatorQuad.width = 40.0F;
        indicatorQuad.height = 40.0F;
        indicatorQuad.r = 1.0F;
        indicatorQuad.g = 0.0F;
        indicatorQuad.b = 0.0F;
        indicatorQuad.a = alpha;
        indicatorQuad.depth = 0.5F;
        uiPass.DrawQuad(indicatorQuad);
    }
}

void HUD_UI::renderLowHealthWarning(CatEngine::Renderer::UIPass& uiPass) {
    float pulseIntensity = (std::sin(m_lowHealthPulse) + 1.0F) * 0.5F;
    float alpha = 0.3F * pulseIntensity;
    float edgeWidth = 60.0F;

    // Top edge
    CatEngine::Renderer::UIPass::QuadDesc topEdge;
    topEdge.x = 0.0F;
    topEdge.y = 0.0F;
    topEdge.width = static_cast<float>(m_screenWidth);
    topEdge.height = edgeWidth;
    topEdge.r = 1.0F;
    topEdge.g = 0.0F;
    topEdge.b = 0.0F;
    topEdge.a = alpha;
    topEdge.depth = 0.9F;
    uiPass.DrawQuad(topEdge);

    // Bottom edge
    CatEngine::Renderer::UIPass::QuadDesc bottomEdge;
    bottomEdge.x = 0.0F;
    bottomEdge.y = static_cast<float>(m_screenHeight) - edgeWidth;
    bottomEdge.width = static_cast<float>(m_screenWidth);
    bottomEdge.height = edgeWidth;
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
    leftEdge.width = edgeWidth;
    leftEdge.height = static_cast<float>(m_screenHeight);
    leftEdge.r = 1.0F;
    leftEdge.g = 0.0F;
    leftEdge.b = 0.0F;
    leftEdge.a = alpha;
    leftEdge.depth = 0.9F;
    uiPass.DrawQuad(leftEdge);

    // Right edge
    CatEngine::Renderer::UIPass::QuadDesc rightEdge;
    rightEdge.x = static_cast<float>(m_screenWidth) - edgeWidth;
    rightEdge.y = 0.0F;
    rightEdge.width = edgeWidth;
    rightEdge.height = static_cast<float>(m_screenHeight);
    rightEdge.r = 1.0F;
    rightEdge.g = 0.0F;
    rightEdge.b = 0.0F;
    rightEdge.a = alpha;
    rightEdge.depth = 0.9F;
    uiPass.DrawQuad(rightEdge);
}

// ============================================================================
// Private Update Methods
// ============================================================================

void HUD_UI::updateDamageNumbers(float deltaTime) {
    for (auto& dmg : m_damageNumbers) {
        dmg.lifetime -= deltaTime;
        dmg.position.x += dmg.velocity.x * deltaTime;
        dmg.position.y += dmg.velocity.y * deltaTime;
        dmg.velocity.y += 20.0F * deltaTime;
    }

    m_damageNumbers.erase(
        std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
            [](const DamageNumber& d) { return d.lifetime <= 0.0F; }),
        m_damageNumbers.end()
    );
}

void HUD_UI::updateDamageIndicators(float deltaTime) {
    for (auto& indicator : m_damageIndicators) {
        indicator.lifetime -= deltaTime;
    }

    m_damageIndicators.erase(
        std::remove_if(m_damageIndicators.begin(), m_damageIndicators.end(),
            [](const DamageIndicator& d) { return d.lifetime <= 0.0F; }),
        m_damageIndicators.end()
    );
}

void HUD_UI::updateBuffs(float deltaTime) {
    for (auto& buff : m_buffs) {
        buff.remainingTime -= deltaTime;
    }

    m_buffs.erase(
        std::remove_if(m_buffs.begin(), m_buffs.end(),
            [](const BuffIndicator& b) { return b.remainingTime <= 0.0F; }),
        m_buffs.end()
    );
}

} // namespace Game

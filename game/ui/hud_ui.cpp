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
        m_spellSlots[i].slotNumber = i + 1;
        m_spellSlots[i].isReady = true;
    }

    m_initialized = true;
    Engine::Logger::info("HUD_UI initialized successfully");
    return true;
}

void HUD_UI::shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_compass) {
        m_compass->shutdown();
        m_compass.reset();
    }

    if (m_minimap) {
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
    if (m_compass && m_compass->isVisible()) {
        m_compass->update(deltaTime, m_playerPosition, m_playerYaw);
    }

    // Update minimap
    if (m_minimap && m_minimap->isVisible()) {
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
        m_lowHealthPulse += deltaTime * 4.0f;
    } else {
        m_lowHealthWarning = false;
        m_lowHealthPulse = 0.0f;
    }
}

void HUD_UI::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized || !m_hudVisible || m_cinematicMode) {
        return;
    }

    // Render low health warning (background layer)
    if (m_lowHealthWarning && !m_minimalMode) {
        renderLowHealthWarning(renderer);
    }

    // Render damage indicators
    if (!m_minimalMode) {
        renderDamageIndicators(renderer);
    }

    // Render health bar (always visible)
    renderHealthBar(renderer);

    if (!m_minimalMode) {
        // Render mana and stamina bars
        renderManaBar(renderer);
        renderStaminaBar(renderer);

        // Render level and currency
        renderLevelAndCurrency(renderer);

        // Render experience bar
        renderExperienceBar(renderer);

        // Render quest tracker
        if (!m_trackedQuests.empty()) {
            renderQuestTracker(renderer);
        }

        // Render spell slots
        renderSpellSlots(renderer);

        // Render buff bar
        if (!m_buffs.empty()) {
            renderBuffBar(renderer);
        }

        // Render compass
        if (m_compass) {
            m_compass->render(renderer);
        }

        // Render minimap
        if (m_minimap) {
            m_minimap->render(renderer);
        }
    }

    // Render crosshair (always centered)
    if (m_showCrosshair) {
        renderCrosshair(renderer);
    }

    // Render damage numbers
    renderDamageNumbers(renderer);

    // Render FPS counter
    if (m_showFPS) {
        renderFPS(renderer);
    }
}

// ============================================================================
// Visibility Modes
// ============================================================================

void HUD_UI::setMinimalMode(bool minimal) {
    m_minimalMode = minimal;
    if (minimal) {
        // Hide compass and minimap in minimal mode
        if (m_compass) m_compass->setVisible(false);
        if (m_minimap) m_minimap->setVisible(false);
    }
}

void HUD_UI::setCinematicMode(bool cinematic) {
    m_cinematicMode = cinematic;
    if (cinematic) {
        // Hide everything in cinematic mode
        if (m_compass) m_compass->setVisible(false);
        if (m_minimap) m_minimap->setVisible(false);
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
    // Check if quest is already tracked
    for (const auto& q : m_trackedQuests) {
        if (q.questId == quest.questId) {
            return;  // Already tracked
        }
    }

    // Add quest (limit to max tracked)
    if (static_cast<int>(m_trackedQuests.size()) < m_maxTrackedQuests) {
        m_trackedQuests.push_back(quest);
        Engine::Logger::debug("HUD_UI: Added tracked quest '{}'", quest.questTitle);
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
        auto& slot = m_spellSlots[slotNumber - 1];
        slot.spellId = spellId;
        slot.iconPath = iconPath;
        slot.isReady = true;
    }
}

void HUD_UI::setSpellCooldown(int slotNumber, float remaining, float total) {
    if (slotNumber >= 1 && slotNumber <= 4) {
        auto& slot = m_spellSlots[slotNumber - 1];
        slot.cooldownRemaining = remaining;
        slot.cooldownTotal = total;
        slot.isReady = (remaining <= 0.0f);
    }
}

void HUD_UI::clearSpellSlot(int slotNumber) {
    if (slotNumber >= 1 && slotNumber <= 4) {
        auto& slot = m_spellSlots[slotNumber - 1];
        slot.spellId.clear();
        slot.iconPath.clear();
        slot.cooldownRemaining = 0.0f;
        slot.cooldownTotal = 0.0f;
        slot.isReady = true;
    }
}

// ============================================================================
// Buffs/Debuffs
// ============================================================================

void HUD_UI::addBuff(const BuffIndicator& buff) {
    // Check if buff already exists (update stack count)
    for (auto& b : m_buffs) {
        if (b.id == buff.id) {
            b.stackCount++;
            b.remainingTime = buff.duration;  // Refresh duration
            return;
        }
    }

    // Add new buff
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
    dmg.velocity = {0.0f, -50.0f};  // Float upward
    dmg.lifetime = dmg.maxLifetime;
    dmg.isCritical = isCritical;
    dmg.isHeal = false;
    m_damageNumbers.push_back(dmg);
}

void HUD_UI::showHealNumber(float amount, const Engine::vec2& screenPos) {
    DamageNumber heal;
    heal.amount = amount;
    heal.position = screenPos;
    heal.velocity = {0.0f, -50.0f};
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
    if (m_minimap) {
        m_minimap->setVisible(show);
    }
}

void HUD_UI::setShowCompass(bool show) {
    if (m_compass) {
        m_compass->setVisible(show);
    }
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void HUD_UI::renderHealthBar(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement health bar rendering
    // Position: Top left
    // Show current/max health
    // Red color with darker background
    // Show damage flash effect if recently damaged
}

void HUD_UI::renderManaBar(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement mana bar rendering
    // Position: Below health bar
    // Blue color
}

void HUD_UI::renderStaminaBar(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement stamina bar rendering
    // Position: Below mana bar
    // Green/yellow color
}

void HUD_UI::renderQuestTracker(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement quest tracker rendering
    // Position: Right side of screen
    // Show quest title and current objective
    // Show progress bar for objectives
}

void HUD_UI::renderSpellSlots(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement spell slots rendering
    // Position: Bottom center
    // Show 4 slots with icons
    // Show cooldown overlays
    // Show slot numbers (1-4)
}

void HUD_UI::renderBuffBar(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement buff bar rendering
    // Position: Top center
    // Show buff/debuff icons with timers
    // Show stack counts
}

void HUD_UI::renderExperienceBar(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement XP bar rendering
    // Position: Bottom of screen
    // Thin progress bar showing XP progress to next level
}

void HUD_UI::renderLevelAndCurrency(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement level and currency display
    // Position: Top left, near health bar
    // Show level number and currency amount
}

void HUD_UI::renderCrosshair(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement crosshair rendering
    // Position: Center of screen
    // Simple cross or dot
}

void HUD_UI::renderFPS(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement FPS counter rendering
    // Position: Top right
    // Show current FPS
}

void HUD_UI::renderDamageNumbers(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement damage number rendering
    // Render floating damage/heal numbers
    // Different colors for damage, critical, and healing
}

void HUD_UI::renderDamageIndicators(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement damage indicator rendering
    // Show directional damage indicators at screen edges
}

void HUD_UI::renderLowHealthWarning(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement low health warning
    // Pulsing red vignette at screen edges
    // Use m_lowHealthPulse for animation
}

// ============================================================================
// Private Update Methods
// ============================================================================

void HUD_UI::updateDamageNumbers(float deltaTime) {
    for (auto& dmg : m_damageNumbers) {
        dmg.lifetime -= deltaTime;
        dmg.position.x += dmg.velocity.x * deltaTime;
        dmg.position.y += dmg.velocity.y * deltaTime;
        // Add slight deceleration
        dmg.velocity.y += 20.0f * deltaTime;
    }

    // Remove expired damage numbers
    m_damageNumbers.erase(
        std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
            [](const DamageNumber& d) { return d.lifetime <= 0.0f; }),
        m_damageNumbers.end()
    );
}

void HUD_UI::updateDamageIndicators(float deltaTime) {
    for (auto& indicator : m_damageIndicators) {
        indicator.lifetime -= deltaTime;
    }

    // Remove expired indicators
    m_damageIndicators.erase(
        std::remove_if(m_damageIndicators.begin(), m_damageIndicators.end(),
            [](const DamageIndicator& d) { return d.lifetime <= 0.0f; }),
        m_damageIndicators.end()
    );
}

void HUD_UI::updateBuffs(float deltaTime) {
    for (auto& buff : m_buffs) {
        buff.remainingTime -= deltaTime;
    }

    // Remove expired buffs
    m_buffs.erase(
        std::remove_if(m_buffs.begin(), m_buffs.end(),
            [](const BuffIndicator& b) { return b.remainingTime <= 0.0f; }),
        m_buffs.end()
    );
}

} // namespace Game

#ifndef GAME_UI_HUD_UI_HPP
#define GAME_UI_HUD_UI_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include "../../engine/math/Vector.hpp"
#include "compass_ui.hpp"
#include "minimap_ui.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Game {

// Forward declaration
class GameAudio;

/**
 * @brief Health bar component
 */
struct HealthBar {
    float currentHealth = 100.0f;
    float maxHealth = 100.0f;
    float lastDamageTime = 0.0f;
    float damageFlashDuration = 0.2f;
    bool showDamageFlash = false;

    void setHealth(float current, float max) {
        currentHealth = current;
        maxHealth = max;
    }

    float getHealthPercent() const {
        return (maxHealth > 0.0f) ? (currentHealth / maxHealth) : 0.0f;
    }

    bool isLowHealth() const {
        return getHealthPercent() < 0.25f;
    }
};

/**
 * @brief Mana bar component
 */
struct ManaBar {
    float currentMana = 100.0f;
    float maxMana = 100.0f;

    void setMana(float current, float max) {
        currentMana = current;
        maxMana = max;
    }

    float getManaPercent() const {
        return (maxMana > 0.0f) ? (currentMana / maxMana) : 0.0f;
    }
};

/**
 * @brief Stamina bar component
 */
struct StaminaBar {
    float currentStamina = 100.0f;
    float maxStamina = 100.0f;
    bool isRegenerating = false;

    void setStamina(float current, float max) {
        currentStamina = current;
        maxStamina = max;
    }

    float getStaminaPercent() const {
        return (maxStamina > 0.0f) ? (currentStamina / maxStamina) : 0.0f;
    }
};

/**
 * @brief Quest tracker entry (displayed on HUD)
 */
struct QuestTrackerEntry {
    std::string questId;
    std::string questTitle;
    std::string currentObjective;
    int objectiveProgress;
    int objectiveTotal;
};

/**
 * @brief Spell slot display
 */
struct SpellSlot {
    int slotNumber;  // 1-4
    std::string spellId;
    std::string iconPath;
    float cooldownRemaining = 0.0f;
    float cooldownTotal = 0.0f;
    bool isReady = true;

    float getCooldownPercent() const {
        return (cooldownTotal > 0.0f) ? (cooldownRemaining / cooldownTotal) : 0.0f;
    }
};

/**
 * @brief Buff/debuff indicator
 */
struct BuffIndicator {
    std::string id;
    std::string name;
    std::string iconPath;
    float duration;
    float remainingTime;
    bool isBuff;  // true = buff, false = debuff
    int stackCount = 1;

    float getTimePercent() const {
        return (duration > 0.0f) ? (remainingTime / duration) : 0.0f;
    }
};

/**
 * @brief HUD UI - Enhanced main HUD container
 *
 * Features:
 * - Health, mana, stamina bars
 * - Compass UI integration
 * - Minimap UI integration
 * - Quest tracker (active quest objectives)
 * - Spell slots with cooldowns
 * - Buff/debuff bar
 * - Experience bar
 * - Level display
 * - Currency display
 * - Crosshair
 * - FPS counter
 * - Damage/heal numbers
 * - Low health warning
 * - Minimal/cinematic modes
 */
class HUD_UI {
public:
    explicit HUD_UI(Engine::Input& input, GameAudio& audio);
    ~HUD_UI();

    /**
     * @brief Initialize HUD
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown HUD
     */
    void shutdown();

    /**
     * @brief Update HUD (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render HUD
     * @param renderer Renderer to use for drawing
     */
    void render(CatEngine::Renderer::Renderer& renderer);

    // ========================================================================
    // Components Access
    // ========================================================================

    HealthBar& getHealthBar() { return m_healthBar; }
    ManaBar& getManaBar() { return m_manaBar; }
    StaminaBar& getStaminaBar() { return m_staminaBar; }
    CompassUI& getCompass() { return *m_compass; }
    MinimapUI& getMinimap() { return *m_minimap; }

    // ========================================================================
    // Visibility Modes
    // ========================================================================

    /**
     * @brief Set HUD visibility
     * @param visible true to show HUD
     */
    void setHUDVisible(bool visible) { m_hudVisible = visible; }

    /**
     * @brief Check if HUD is visible
     */
    bool isHUDVisible() const { return m_hudVisible; }

    /**
     * @brief Set minimal mode (only essential info)
     * @param minimal true to enable minimal mode
     */
    void setMinimalMode(bool minimal);

    /**
     * @brief Check if in minimal mode
     */
    bool isMinimalMode() const { return m_minimalMode; }

    /**
     * @brief Set cinematic mode (hide all UI)
     * @param cinematic true to enable cinematic mode
     */
    void setCinematicMode(bool cinematic);

    /**
     * @brief Check if in cinematic mode
     */
    bool isCinematicMode() const { return m_cinematicMode; }

    // ========================================================================
    // Player Stats
    // ========================================================================

    /**
     * @brief Set player health
     */
    void setHealth(float current, float max);

    /**
     * @brief Set player mana
     */
    void setMana(float current, float max);

    /**
     * @brief Set player stamina
     */
    void setStamina(float current, float max);

    /**
     * @brief Set player level
     */
    void setLevel(int level) { m_playerLevel = level; }

    /**
     * @brief Set player experience
     * @param current Current XP
     * @param required XP required for next level
     */
    void setExperience(int current, int required);

    /**
     * @brief Set player currency
     */
    void setCurrency(int amount) { m_playerCurrency = amount; }

    /**
     * @brief Update player position (for compass/minimap)
     */
    void setPlayerPosition(const Engine::vec3& pos, float yaw);

    // ========================================================================
    // Quest Tracker
    // ========================================================================

    /**
     * @brief Add quest to tracker
     */
    void addTrackedQuest(const QuestTrackerEntry& quest);

    /**
     * @brief Remove quest from tracker
     */
    void removeTrackedQuest(const std::string& questId);

    /**
     * @brief Update quest progress
     */
    void updateQuestProgress(const std::string& questId, int current, int total);

    /**
     * @brief Clear all tracked quests
     */
    void clearTrackedQuests();

    // ========================================================================
    // Spell Slots
    // ========================================================================

    /**
     * @brief Set spell in slot
     * @param slotNumber Slot number (1-4)
     * @param spellId Spell ID
     * @param iconPath Icon path
     */
    void setSpellSlot(int slotNumber, const std::string& spellId, const std::string& iconPath);

    /**
     * @brief Update spell cooldown
     * @param slotNumber Slot number (1-4)
     * @param remaining Remaining cooldown time
     * @param total Total cooldown time
     */
    void setSpellCooldown(int slotNumber, float remaining, float total);

    /**
     * @brief Clear spell slot
     */
    void clearSpellSlot(int slotNumber);

    // ========================================================================
    // Buffs/Debuffs
    // ========================================================================

    /**
     * @brief Add buff indicator
     */
    void addBuff(const BuffIndicator& buff);

    /**
     * @brief Remove buff indicator
     */
    void removeBuff(const std::string& buffId);

    /**
     * @brief Update buff duration
     */
    void updateBuffDuration(const std::string& buffId, float remainingTime);

    /**
     * @brief Clear all buffs
     */
    void clearBuffs();

    // ========================================================================
    // Visual Effects
    // ========================================================================

    /**
     * @brief Show damage number
     */
    void showDamageNumber(float damage, const Engine::vec2& screenPos, bool isCritical = false);

    /**
     * @brief Show heal number
     */
    void showHealNumber(float amount, const Engine::vec2& screenPos);

    /**
     * @brief Show damage indicator (directional)
     */
    void showDamageIndicator(const Engine::vec2& direction, float intensity = 1.0f);

    // ========================================================================
    // Display Options
    // ========================================================================

    /**
     * @brief Show or hide crosshair
     */
    void setShowCrosshair(bool show) { m_showCrosshair = show; }

    /**
     * @brief Show or hide FPS counter
     */
    void setShowFPS(bool show) { m_showFPS = show; }

    /**
     * @brief Update FPS value
     */
    void setFPS(float fps) { m_fps = fps; }

    /**
     * @brief Show or hide minimap
     */
    void setShowMinimap(bool show);

    /**
     * @brief Show or hide compass
     */
    void setShowCompass(bool show);

private:
    /**
     * @brief Render health bar
     */
    void renderHealthBar(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render mana bar
     */
    void renderManaBar(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render stamina bar
     */
    void renderStaminaBar(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render quest tracker
     */
    void renderQuestTracker(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render spell slots
     */
    void renderSpellSlots(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render buff bar
     */
    void renderBuffBar(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render experience bar
     */
    void renderExperienceBar(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render level and currency
     */
    void renderLevelAndCurrency(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render crosshair
     */
    void renderCrosshair(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render FPS counter
     */
    void renderFPS(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render damage/heal numbers
     */
    void renderDamageNumbers(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render damage indicators
     */
    void renderDamageIndicators(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render low health warning
     */
    void renderLowHealthWarning(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Update damage numbers animation
     */
    void updateDamageNumbers(float deltaTime);

    /**
     * @brief Update damage indicators
     */
    void updateDamageIndicators(float deltaTime);

    /**
     * @brief Update buffs/debuffs
     */
    void updateBuffs(float deltaTime);

    Engine::Input& m_input;
    GameAudio& m_audio;

    // Components
    HealthBar m_healthBar;
    ManaBar m_manaBar;
    StaminaBar m_staminaBar;
    std::unique_ptr<CompassUI> m_compass;
    std::unique_ptr<MinimapUI> m_minimap;

    // Quest tracker
    std::vector<QuestTrackerEntry> m_trackedQuests;
    int m_maxTrackedQuests = 3;

    // Spell slots
    std::array<SpellSlot, 4> m_spellSlots;

    // Buffs/debuffs
    std::vector<BuffIndicator> m_buffs;

    // Player stats
    int m_playerLevel = 1;
    int m_currentXP = 0;
    int m_requiredXP = 100;
    int m_playerCurrency = 0;

    // Player position
    Engine::vec3 m_playerPosition = Engine::vec3::zero();
    float m_playerYaw = 0.0f;

    // Display options
    bool m_hudVisible = true;
    bool m_minimalMode = false;
    bool m_cinematicMode = false;
    bool m_showCrosshair = true;
    bool m_showFPS = false;
    float m_fps = 0.0f;

    // Damage numbers (same as existing HUD)
    struct DamageNumber {
        float amount;
        Engine::vec2 position;
        Engine::vec2 velocity;
        float lifetime;
        float maxLifetime = 1.5f;
        bool isCritical;
        bool isHeal;
    };
    std::vector<DamageNumber> m_damageNumbers;

    // Damage indicators (same as existing HUD)
    struct DamageIndicator {
        Engine::vec2 direction;
        float intensity;
        float lifetime;
        float maxLifetime = 0.5f;
    };
    std::vector<DamageIndicator> m_damageIndicators;

    // Low health warning
    bool m_lowHealthWarning = false;
    float m_lowHealthPulse = 0.0f;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_HUD_UI_HPP

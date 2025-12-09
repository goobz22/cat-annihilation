#ifndef GAME_UI_HUD_HPP
#define GAME_UI_HUD_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include <cstdint>
#include <string>

namespace Game {

class GameAudio;

/**
 * @brief Heads-Up Display - In-game UI overlay
 *
 * Displays health, wave number, enemy count, score, and other vital info.
 * Shows visual feedback like damage indicators and low health warnings.
 */
class HUD {
public:
    explicit HUD(Engine::Input& input, GameAudio& audio);
    ~HUD();

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
     * @param uiPass UIPass to use for 2D drawing
     * @param screenWidth Current screen width
     * @param screenHeight Current screen height
     */
    void render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight);

    // ========================================================================
    // Data Setters
    // ========================================================================

    /**
     * @brief Set player health
     * @param current Current health
     * @param max Maximum health
     */
    void setHealth(float current, float max);

    /**
     * @brief Set wave number
     * @param wave Current wave number
     */
    void setWave(uint32_t wave);

    /**
     * @brief Set enemy count
     * @param remaining Remaining enemies in wave
     * @param total Total enemies in wave
     */
    void setEnemyCount(uint32_t remaining, uint32_t total);

    /**
     * @brief Set player score
     * @param score Current score
     */
    void setScore(uint32_t score);

    /**
     * @brief Set combo counter
     * @param combo Current combo count
     */
    void setCombo(uint32_t combo);

    // ========================================================================
    // Visual Effects
    // ========================================================================

    /**
     * @brief Show damage indicator (screen flash)
     * @param direction Direction of damage source (normalized)
     * @param intensity Intensity of effect (0.0 to 1.0)
     */
    void showDamageIndicator(const std::array<float, 2>& direction, float intensity = 1.0f);

    /**
     * @brief Show damage number at position
     * @param damage Damage amount
     * @param screenPosition Screen position to display number
     * @param isCritical Is this a critical hit
     */
    void showDamageNumber(float damage,
                          const std::array<float, 2>& screenPosition,
                          bool isCritical = false);

    /**
     * @brief Show heal number at position
     * @param amount Heal amount
     * @param screenPosition Screen position to display number
     */
    void showHealNumber(float amount, const std::array<float, 2>& screenPosition);

    /**
     * @brief Enable/disable low health warning
     * @param enable true to show warning
     */
    void setLowHealthWarning(bool enable);

    /**
     * @brief Show crosshair
     * @param show true to show crosshair
     */
    void setShowCrosshair(bool show) { m_showCrosshair = show; }

    /**
     * @brief Enable/disable FPS counter
     * @param show true to show FPS
     */
    void setShowFPS(bool show) { m_showFPS = show; }

    /**
     * @brief Update FPS counter
     * @param fps Current frames per second
     */
    void setFPS(float fps);

    // ========================================================================
    // Notification System
    // ========================================================================

    /**
     * @brief Show a notification message on screen
     * @param message The message to display
     * @param duration How long to show (seconds), default 3.0
     * @param priority Higher priority notifications display on top
     */
    void showNotification(const std::string& message, float duration = 3.0f, int priority = 0);

    /**
     * @brief Show a notification with a specific type (affects color/style)
     * @param message The message to display
     * @param type Notification type: "info", "success", "warning", "error"
     * @param duration How long to show (seconds)
     */
    void showNotification(const std::string& message, const std::string& type, float duration = 3.0f);

    /**
     * @brief Clear all notifications
     */
    void clearNotifications();

private:
    /**
     * @brief Render health bar
     */
    void renderHealthBar(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render wave counter
     */
    void renderWaveCounter(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render enemy counter
     */
    void renderEnemyCounter(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render score display
     */
    void renderScore(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render crosshair
     */
    void renderCrosshair(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render damage indicators
     */
    void renderDamageIndicators(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render damage/heal numbers
     */
    void renderDamageNumbers(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render low health warning (screen edge glow)
     */
    void renderLowHealthWarning(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render FPS counter
     */
    void renderFPS(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Update damage indicators
     */
    void updateDamageIndicators(float deltaTime);

    /**
     * @brief Update damage numbers
     */
    void updateDamageNumbers(float deltaTime);

    Engine::Input& m_input;
    GameAudio& m_audio;

    // Player stats
    float m_currentHealth = 100.0f;
    float m_maxHealth = 100.0f;
    uint32_t m_currentWave = 1;
    uint32_t m_remainingEnemies = 0;
    uint32_t m_totalEnemies = 0;
    uint32_t m_score = 0;
    uint32_t m_combo = 0;

    // Visual options
    bool m_showCrosshair = true;
    bool m_showFPS = false;
    bool m_lowHealthWarning = false;
    float m_fps = 0.0f;

    // Damage indicators
    struct DamageIndicator {
        std::array<float, 2> direction;
        float intensity;
        float lifetime;
        float maxLifetime = 0.5f;
    };
    std::vector<DamageIndicator> m_damageIndicators;

    // Damage numbers
    struct DamageNumber {
        float amount;
        std::array<float, 2> position;
        std::array<float, 2> velocity;
        float lifetime;
        float maxLifetime = 1.5f;
        bool isCritical;
        bool isHeal;
    };
    std::vector<DamageNumber> m_damageNumbers;

    // Low health warning animation
    float m_lowHealthPulse = 0.0F;

    // Combo display
    float m_comboDisplayTime = 0.0F;
    float m_comboFadeTime = 2.0F;

    // Notification system
    enum class NotificationType {
        Info,
        Success,
        Warning,
        Error
    };

    struct Notification {
        std::string message;
        NotificationType type = NotificationType::Info;
        float duration = 3.0F;
        float elapsed = 0.0F;
        int priority = 0;
    };
    std::vector<Notification> m_notifications;
    static constexpr size_t MAX_NOTIFICATIONS = 5;

    /**
     * @brief Update notifications
     */
    void updateNotifications(float deltaTime);

    /**
     * @brief Render notifications
     */
    void renderNotifications(CatEngine::Renderer::UIPass& uiPass);

    // Screen dimensions (cached during render)
    uint32_t m_screenWidth = 1920;
    uint32_t m_screenHeight = 1080;

    /**
     * @brief Get color for notification type
     */
    static std::array<float, 4> getNotificationColor(NotificationType type);

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_HUD_HPP

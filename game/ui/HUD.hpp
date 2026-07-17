#ifndef GAME_UI_HUD_HPP
#define GAME_UI_HUD_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include "../../engine/math/Transform.hpp"  // Engine::Transform + Engine::vec3 for enemy-bar projection
#include <cstdint>
#include <string>
#include <vector>

namespace Engine { class ImGuiLayer; }

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

    /**
     * @brief Set the currently-selected hotbar weapon/spell for the active-weapon
     *        indicator drawn beside the health bar.
     *
     * Mirrors the web InventoryHotbar's active-slot label (InventoryHotbar.tsx:66-70),
     * which prints the active item's name plus its 1-based slot key.
     *
     * @param itemName Display name of the active item ("Spell"/"Sword"/"Bow"/"Shield").
     *                 An empty string hides the indicator (nothing equipped/known yet).
     * @param slotNumber 1-based hotbar slot (web shows keys 1-9, InventoryHotbar.tsx:60-62)
     */
    void setActiveWeapon(const std::string& itemName, uint32_t slotNumber);

    /**
     * @brief Set the cat's current level for the level/XP readout.
     *
     * Mirrors web CatStats '🐱 Lv.{level}' (CatStats.tsx:125-127).
     * @param level Current cat level (>= 1)
     */
    void setCatLevel(uint32_t level);

    /**
     * @brief Set XP progress toward the next level (0.0 - 1.0) for the XP bar fill.
     *
     * Mirrors web CatStats' xpIntoLevel/xpNeededForLevel bar (CatStats.tsx:34-36,133-137);
     * native feeds LevelingSystem::getXPProgress() which is already normalised 0..1.
     * @param progress Fraction of the current level completed, clamped to [0,1] on render.
     */
    void setXpProgress(float progress);

    /**
     * @brief Set the ability-status caption under the XP bar.
     *
     * Mirrors web CatStats' ability strip: unlocked ability names plus the
     * "next unlock" hint (CatStats.tsx:64-75 picks the next of
     * regeneration/agility/nineLives/predatorInstinct/alphaStrike by level).
     * The game layer composes the full string from LevelingSystem state so
     * the HUD stays a dumb renderer, same as setWave/setScore.
     * @param line Composed text; empty hides the row.
     */
    void setAbilityLine(const std::string& line);

    /**
     * @brief Set the ACTIVE weapon's skill progression for the top-right
     *        weapon-skill card (web WeaponSkills.tsx / ui.css
     *        `.weapon-skills-container`).
     *
     * The card shows only the currently-selected weapon's skill: title
     * "<Weapon> Level N", a progress bar, "cur / need XP", and "X XP to
     * level N+1". The HUD derives the title text + theme colour from the
     * active-weapon name already fed via setActiveWeapon() (a spell maps to
     * "<Element> Magic" in that element's colour, e.g. Water Magic blue
     * #3b82f6; a shield has no skill and hides the card), so this setter only
     * carries the raw numbers the HUD cannot know — read straight off
     * LevelingSystem for the active weapon/element (WeaponSkill::level/xp/
     * xpToNextLevel).
     *
     * INTEGRATION: call once per frame from the game layer (it owns
     * LevelingSystem + the active hotbar slot), right beside the existing
     * setActiveWeapon() call. Pass level <= 0 to hide the card (e.g. when the
     * active item is the shield, which the web excludes).
     *
     * @param level         Active weapon/element skill level (>= 1), or <= 0 to hide.
     * @param currentXp     Absolute skill XP (web shows `skill.xp`).
     * @param xpToNextLevel Absolute XP total for the next level (web `skill.xpToNextLevel`).
     */
    void setActiveWeaponSkill(int level, int currentXp, int xpToNextLevel);

    // ========================================================================
    // Enemy overhead health bars (web LocalEnemySystem.tsx:483-494)
    // ========================================================================

    /**
     * @brief Provide the live camera for projecting enemy world positions to
     *        screen for the floating health bars.
     *
     * Kept self-contained in the HUD (the task contract): the HUD rebuilds the
     * view-projection from the camera transform + projection params exactly as
     * the scene camera does (Camera::UpdateViewMatrix = rotation^T * translate
     * (-pos); mat4::perspective for the projection), so the bars land on the
     * dogs the same frame the scene renders them.
     *
     * INTEGRATION: feed PlayerControlSystem::getCameraTransform() and the
     * scene camera's fov/aspect/near/far each frame before addEnemyBar().
     *
     * @param cameraTransform World-space camera transform (position + rotation).
     * @param fovYRadians      Vertical field of view (native scene camera; web fov=75deg).
     * @param aspect           Viewport aspect ratio (width / height).
     * @param nearPlane        Camera near plane.
     * @param farPlane         Camera far plane.
     */
    // Feed the scene's exact camera as lookAt inputs: `cameraPosition` and
    // `cameraTarget` must be the SAME eye/target the scene render passes to
    // mat4::lookAt (including any camera shake on the eye), so projected
    // bars land on the rendered dogs.
    void setEnemyBarCamera(const Engine::vec3& cameraPosition,
                           const Engine::vec3& cameraTarget,
                           float fovYRadians, float aspect,
                           float nearPlane, float farPlane);

    /**
     * @brief Clear the per-frame enemy-bar list. Call at the start of each
     *        frame's enemy sweep before re-adding the living enemies.
     */
    void clearEnemyBars();

    /**
     * @brief Queue one living enemy's floating health bar for this frame.
     *
     * Mirrors the web bar, drawn for EVERY living dog (not only when damaged):
     * a #333 track with a health-tiered fill, billboarded 1.5 world-units above
     * the enemy origin.
     *
     * @param worldPosition Enemy world position (ground origin; the +1.5 lift is applied here).
     * @param healthRatio   current / max health, clamped to [0,1] on render.
     */
    void addEnemyBar(const Engine::vec3& worldPosition, float healthRatio);

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

    /**
     * @brief Attach the ImGui layer (used for fonts and widget rendering). Optional.
     */
    void setImGuiLayer(Engine::ImGuiLayer* imguiLayer) { m_imguiLayer = imguiLayer; }

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

    // Active hotbar weapon/spell indicator (mirrors web InventoryHotbar active-slot
    // label). Empty name = nothing to show yet, so the indicator stays hidden.
    std::string m_activeWeaponName;
    uint32_t m_activeWeaponSlot = 1;

    // Cat progression readout (mirrors web CatStats level + XP bar). m_xpProgress is
    // a 0..1 fraction of the current level, fed from LevelingSystem::getXPProgress().
    uint32_t m_catLevel = 1;
    float m_xpProgress = 0.0F;

    // Ability strip under the XP bar (web CatStats ability icons + next-unlock
    // hint). Composed by the game layer; empty = row hidden.
    std::string m_abilityLine;

    // Active-weapon skill card (web WeaponSkills.tsx). Level <= 0 means "not
    // fed / no skill" and hides the card (also hidden when the active item is
    // the shield, which the web has no skill for). XP values are the absolute
    // totals the web card prints (skill.xp / skill.xpToNextLevel).
    int m_weaponSkillLevel = 0;
    int m_weaponSkillCurrentXp = 0;
    int m_weaponSkillXpToNext = 0;

    // Enemy overhead health bars (web LocalEnemySystem.tsx:483-494). Rebuilt
    // each frame by the game layer (clearEnemyBars + addEnemyBar per living
    // dog); projected to screen with the fed camera. Empty = nothing drawn.
    struct EnemyBar {
        Engine::vec3 worldPosition;
        float healthRatio;
    };
    std::vector<EnemyBar> m_enemyBars;
    // Enemy-bar camera stored as the SCENE's lookAt inputs (eye + target),
    // not a free camera Transform: the bars must project with the EXACT same
    // view the scene renders the dogs with, or they float off the heads
    // (2026-07-17 audit — the old getCameraTransform path used the camera's
    // yaw/pitch, ~2.6 deg off the scene's lookAt(camPos, playerTorso), and
    // missed camera shake). The view is rebuilt as mat4::lookAt(pos, target).
    Engine::vec3 m_enemyBarCamPos{0.0f, 0.0f, 0.0f};
    Engine::vec3 m_enemyBarCamTarget{0.0f, 0.0f, -1.0f};
    float m_enemyBarFovY = 1.309f;     // 75 deg (web PerspectiveCamera fov=75) until fed
    float m_enemyBarAspect = 16.0f / 9.0f;
    float m_enemyBarNear = 0.1f;
    float m_enemyBarFar = 1000.0f;
    bool m_enemyBarCameraValid = false;

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

    // Optional ImGui layer (not owned). When set, render() emits ImGui widgets.
    Engine::ImGuiLayer* m_imguiLayer = nullptr;
};

} // namespace Game

#endif // GAME_UI_HUD_HPP

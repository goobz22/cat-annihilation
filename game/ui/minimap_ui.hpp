#ifndef GAME_UI_MINIMAP_UI_HPP
#define GAME_UI_MINIMAP_UI_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include "../../engine/math/Vector.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace Game {

/**
 * @brief Minimap icon structure
 */
struct MinimapIcon {
    std::string id;
    Engine::vec3 worldPosition;
    std::string iconPath;
    Engine::vec4 color = {1.0F, 1.0F, 1.0F, 1.0F};
    float scale = 1.0F;
    bool isVisible = true;
    bool rotateWithIcon = false;  // For directional icons
    float rotation = 0.0F;        // Rotation in degrees
};

/**
 * @brief Fog of war cell
 */
struct FogCell {
    int x;
    int y;
    bool revealed = false;
    float revealTime = 0.0F;  // Time when revealed (for fade-in effect)
};

/**
 * @brief Minimap UI - Small map display in corner of screen
 *
 * Features:
 * - Circular or square minimap
 * - Player position and rotation indicator
 * - Enemy, NPC, and quest markers
 * - Teammate markers (for multiplayer)
 * - Fog of war system
 * - Zoom levels
 * - Can rotate with player or stay north-oriented
 * - Ping system for marking locations
 */
class MinimapUI {
public:
    explicit MinimapUI(Engine::Input& input);
    ~MinimapUI();

    /**
     * @brief Initialize minimap UI
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown minimap UI
     */
    void shutdown();

    /**
     * @brief Update minimap UI (call once per frame)
     * @param deltaTime Time since last frame in seconds
     * @param playerPos Current player world position
     * @param playerYaw Player rotation in degrees
     */
    void update(float deltaTime, const Engine::vec3& playerPos, float playerYaw);

    /**
     * @brief Render minimap UI
     * @param uiPass UIPass to use for 2D drawing
     * @param screenWidth Current screen width
     * @param screenHeight Current screen height
     */
    void render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight);

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set minimap radius (for circular minimap)
     * @param radius Radius in pixels
     */
    void setSize(float radius);

    /**
     * @brief Set minimap zoom level
     * @param zoom Zoom factor (1.0 = normal, 2.0 = zoomed in 2x, 0.5 = zoomed out 2x)
     */
    void setZoom(float zoom);

    /**
     * @brief Get current zoom level
     */
    float getZoom() const { return m_zoom; }

    /**
     * @brief Zoom in
     */
    void zoomIn();

    /**
     * @brief Zoom out
     */
    void zoomOut();

    /**
     * @brief Set minimap screen position
     * @param screenPos Position in normalized screen coordinates (0-1)
     */
    void setPosition(const Engine::vec2& screenPos);

    /**
     * @brief Set whether minimap rotates with player
     * @param rotate true = rotate with player, false = north always up
     */
    void setRotateWithPlayer(bool rotate) { m_rotateWithPlayer = rotate; }

    /**
     * @brief Check if minimap rotates with player
     */
    bool getRotateWithPlayer() const { return m_rotateWithPlayer; }

    /**
     * @brief Set minimap shape
     * @param circular true = circular, false = square
     */
    void setCircular(bool circular) { m_isCircular = circular; }

    /**
     * @brief Set minimap visibility
     */
    void setVisible(bool visible) { m_visible = visible; }

    /**
     * @brief Check if minimap is visible
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Set minimap opacity
     * @param opacity Opacity (0.0 = transparent, 1.0 = opaque)
     */
    void setOpacity(float opacity);

    // ========================================================================
    // Icons
    // ========================================================================

    /**
     * @brief Add an icon to the minimap
     * @param id Icon ID (must be unique)
     * @param worldPos World position
     * @param iconPath Path to icon texture
     */
    void addIcon(const std::string& id, const Engine::vec3& worldPos,
                 const std::string& iconPath);

    /**
     * @brief Remove an icon from the minimap
     * @param id Icon ID
     */
    void removeIcon(const std::string& id);

    /**
     * @brief Update icon position
     * @param id Icon ID
     * @param newPos New world position
     */
    void updateIconPosition(const std::string& id, const Engine::vec3& newPos);

    /**
     * @brief Update icon rotation
     * @param id Icon ID
     * @param rotation Rotation in degrees
     */
    void updateIconRotation(const std::string& id, float rotation);

    /**
     * @brief Set icon visibility
     * @param id Icon ID
     * @param visible Visibility flag
     */
    void setIconVisible(const std::string& id, bool visible);

    /**
     * @brief Clear all icons
     */
    void clearAllIcons();

    // ========================================================================
    // Built-in Markers
    // ========================================================================

    /**
     * @brief Show or hide enemy markers
     * @param show true to show enemies on minimap
     */
    void showEnemies(bool show) { m_showEnemies = show; }

    /**
     * @brief Show or hide NPC markers
     * @param show true to show NPCs on minimap
     */
    void showNPCs(bool show) { m_showNPCs = show; }

    /**
     * @brief Show or hide quest markers
     * @param show true to show quest objectives on minimap
     */
    void showQuestMarkers(bool show) { m_showQuestMarkers = show; }

    /**
     * @brief Show or hide teammate markers
     * @param show true to show teammates on minimap
     */
    void showTeammates(bool show) { m_showTeammates = show; }

    /**
     * @brief Add enemy marker (auto-managed)
     * @param enemyId Enemy entity ID
     * @param worldPos Enemy position
     */
    void addEnemyMarker(const std::string& enemyId, const Engine::vec3& worldPos);

    /**
     * @brief Add NPC marker (auto-managed)
     * @param npcId NPC entity ID
     * @param worldPos NPC position
     */
    void addNPCMarker(const std::string& npcId, const Engine::vec3& worldPos);

    /**
     * @brief Add quest marker (auto-managed)
     * @param questId Quest ID
     * @param worldPos Objective position
     */
    void addQuestMarker(const std::string& questId, const Engine::vec3& worldPos);

    /**
     * @brief Remove enemy marker
     */
    void removeEnemyMarker(const std::string& enemyId);

    /**
     * @brief Remove NPC marker
     */
    void removeNPCMarker(const std::string& npcId);

    /**
     * @brief Remove quest marker
     */
    void removeQuestMarker(const std::string& questId);

    // ========================================================================
    // Fog of War
    // ========================================================================

    /**
     * @brief Reveal area around a position
     * @param center Center position
     * @param radius Reveal radius
     */
    void revealArea(const Engine::vec3& center, float radius);

    /**
     * @brief Check if an area is revealed
     * @param position Position to check
     * @return true if area is revealed
     */
    bool isAreaRevealed(const Engine::vec3& position) const;

    /**
     * @brief Enable or disable fog of war
     * @param enabled true to enable fog of war
     */
    void setFogOfWarEnabled(bool enabled) { m_fogOfWarEnabled = enabled; }

    /**
     * @brief Clear fog of war (reveal entire map)
     */
    void clearFogOfWar();

    /**
     * @brief Reset fog of war (hide entire map)
     */
    void resetFogOfWar();

    // ========================================================================
    // Ping System
    // ========================================================================

    /**
     * @brief Create a ping at world position
     * @param worldPos Ping position
     * @param color Ping color
     * @param duration Ping duration in seconds
     */
    void createPing(const Engine::vec3& worldPos, const Engine::vec4& color, float duration = 3.0F);

    /**
     * @brief Clear all pings
     */
    void clearPings();

private:
    /**
     * @brief Ping structure
     */
    struct Ping {
        Engine::vec3 worldPosition;
        Engine::vec4 color;
        float lifetime;
        float maxLifetime;
        float pulsePhase = 0.0F;
    };

    /**
     * @brief Render minimap background
     */
    void renderBackground(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render fog of war
     */
    void renderFogOfWar(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render minimap icons
     */
    void renderIcons(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render player indicator
     */
    void renderPlayer(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render pings
     */
    void renderPings(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render minimap border
     */
    void renderBorder(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Convert world position to minimap position
     * @param worldPos World position
     * @return Screen position on minimap (relative to minimap center)
     */
    Engine::vec2 worldToMinimapPos(const Engine::vec3& worldPos) const;

    /**
     * @brief Check if position is within minimap bounds
     */
    bool isWithinMinimapBounds(const Engine::vec2& minimapPos) const;

    /**
     * @brief Get fog cell coordinates for world position
     */
    void worldToFogCell(const Engine::vec3& worldPos, int& outX, int& outY) const;

    /**
     * @brief Get fog cell
     */
    FogCell* getFogCell(int x, int y);
    const FogCell* getFogCell(int x, int y) const;

    /**
     * @brief Update pings
     */
    void updatePings(float deltaTime);

    /**
     * @brief Get icon prefix for category
     */
    static std::string getIconPrefix(const std::string& category);

    /**
     * @brief Check if icon category is visible
     */
    bool isIconCategoryVisible(const std::string& iconId) const;

    Engine::Input& m_input;

    // Icons
    std::unordered_map<std::string, MinimapIcon> m_icons;

    // Built-in marker toggles
    bool m_showEnemies = true;
    bool m_showNPCs = true;
    bool m_showQuestMarkers = true;
    bool m_showTeammates = true;

    // Configuration
    bool m_visible = true;
    bool m_rotateWithPlayer = true;
    bool m_isCircular = true;
    float m_radius = 100.0F;
    float m_zoom = 1.0F;
    float m_minZoom = 0.5F;
    float m_maxZoom = 4.0F;
    float m_opacity = 0.8F;

    // Position (normalized screen coordinates)
    Engine::vec2 m_screenPos = {0.9F, 0.1F};  // Top right

    // Player state
    Engine::vec3 m_playerPosition = Engine::vec3::zero();
    float m_playerYaw = 0.0F;

    // Fog of war
    bool m_fogOfWarEnabled = true;
    float m_fogCellSize = 10.0F;  // Size of each fog cell in world units
    std::unordered_map<uint64_t, FogCell> m_fogCells;

    // Pings
    std::vector<Ping> m_pings;

    // Zoom animation
    float m_targetZoom = 1.0F;
    float m_zoomSpeed = 5.0F;

    // Screen dimensions (cached during render)
    uint32_t m_screenWidth = 1920;
    uint32_t m_screenHeight = 1080;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_MINIMAP_UI_HPP

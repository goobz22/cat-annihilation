#ifndef GAME_UI_COMPASS_UI_HPP
#define GAME_UI_COMPASS_UI_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include "../../engine/math/Vector.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace Game {

/**
 * @brief Compass marker structure
 *
 * Represents a marker displayed on the compass (quest objective, waypoint, etc.)
 */
struct CompassMarker {
    std::string id;
    std::string label;
    Engine::vec3 worldPosition;
    std::string iconPath;
    Engine::vec4 color = {1.0F, 1.0F, 1.0F, 1.0F};
    bool isVisible = true;
    float importance = 1.0F;  // For priority when overlapping (1.0 = normal, 2.0 = high)
    float minDistance = 0.0F;  // Distance at which marker disappears (0 = never)
    float maxDistance = 0.0F;  // Distance beyond which marker fades out (0 = infinite)

    // Animation
    float pulseSpeed = 0.0F;  // 0 = no pulse
    float pulseAmount = 0.0F; // Pulse intensity
};

/**
 * @brief Cardinal direction info
 */
struct CardinalDirection {
    std::string label;  // "N", "E", "S", "W", "NE", "NW", "SE", "SW"
    float angle;        // Angle in degrees (0 = North)
    bool isPrimary;     // true for N, E, S, W
};

/**
 * @brief Compass UI - Displays directional information and markers
 *
 * Features:
 * - Cardinal directions (N, E, S, W) display
 * - Quest objective markers
 * - Custom waypoint markers
 * - Distance display to markers
 * - Marker priority system for overlapping
 * - Fade effects based on distance
 * - Pulse animations for important markers
 */
class CompassUI {
public:
    explicit CompassUI(Engine::Input& input);
    ~CompassUI();

    /**
     * @brief Initialize compass UI
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown compass UI
     */
    void shutdown();

    /**
     * @brief Update compass UI (call once per frame)
     * @param deltaTime Time since last frame in seconds
     * @param playerPos Current player world position
     * @param playerYaw Player rotation in degrees (0 = North, 90 = East)
     */
    void update(float deltaTime, const Engine::vec3& playerPos, float playerYaw);

    /**
     * @brief Render compass UI
     * @param uiPass UIPass to use for 2D drawing
     * @param screenWidth Current screen width
     * @param screenHeight Current screen height
     */
    void render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight);

    // ========================================================================
    // Marker Management
    // ========================================================================

    /**
     * @brief Add a marker to the compass
     * @param marker Marker to add
     */
    void addMarker(const CompassMarker& marker);

    /**
     * @brief Remove a marker from the compass
     * @param markerId Marker ID to remove
     */
    void removeMarker(const std::string& markerId);

    /**
     * @brief Update marker world position
     * @param markerId Marker ID
     * @param newPos New world position
     */
    void updateMarkerPosition(const std::string& markerId, const Engine::vec3& newPos);

    /**
     * @brief Set marker visibility
     * @param markerId Marker ID
     * @param visible Visibility flag
     */
    void setMarkerVisible(const std::string& markerId, bool visible);

    /**
     * @brief Clear all markers
     */
    void clearAllMarkers();

    // ========================================================================
    // Built-in Markers
    // ========================================================================

    /**
     * @brief Set quest objective marker
     * @param position World position of objective
     * @param label Optional label for the objective
     */
    void setQuestObjectiveMarker(const Engine::vec3& position, const std::string& label = "Quest");

    /**
     * @brief Set custom waypoint marker
     * @param position World position of waypoint
     * @param label Optional label for the waypoint
     */
    void setWaypointMarker(const Engine::vec3& position, const std::string& label = "Waypoint");

    /**
     * @brief Clear quest objective marker
     */
    void clearQuestMarker();

    /**
     * @brief Clear waypoint marker
     */
    void clearWaypoint();

    // ========================================================================
    // Display Options
    // ========================================================================

    /**
     * @brief Show or hide cardinal directions
     * @param show true to show N, E, S, W
     */
    void setShowCardinals(bool show) { m_showCardinals = show; }

    /**
     * @brief Check if cardinal directions are shown
     */
    bool getShowCardinals() const { return m_showCardinals; }

    /**
     * @brief Show or hide distance to markers
     * @param show true to show distance labels
     */
    void setShowDistances(bool show) { m_showDistances = show; }

    /**
     * @brief Check if distances are shown
     */
    bool getShowDistances() const { return m_showDistances; }

    /**
     * @brief Set compass size
     * @param width Width in pixels
     * @param height Height in pixels
     */
    void setSize(float width, float height);

    /**
     * @brief Set compass position on screen
     * @param x X position (0-1, normalized screen coordinates)
     * @param y Y position (0-1, normalized screen coordinates)
     */
    void setPosition(float x, float y);

    /**
     * @brief Set compass visibility
     * @param visible true to show compass
     */
    void setVisible(bool visible) { m_visible = visible; }

    /**
     * @brief Check if compass is visible
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Set compass opacity
     * @param opacity Opacity value (0.0 = transparent, 1.0 = opaque)
     */
    void setOpacity(float opacity);

    // ========================================================================
    // Query Methods
    // ========================================================================

    /**
     * @brief Get marker by ID
     * @param markerId Marker ID
     * @return Pointer to marker, or nullptr if not found
     */
    const CompassMarker* getMarker(const std::string& markerId) const;

    /**
     * @brief Get all markers
     * @return Vector of all markers
     */
    std::vector<const CompassMarker*> getAllMarkers() const;

    /**
     * @brief Get number of visible markers
     */
    size_t getVisibleMarkerCount() const;

private:
    /**
     * @brief Render compass background
     */
    void renderBackground(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render cardinal directions
     */
    void renderCardinals(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Render markers
     */
    void renderMarkers(CatEngine::Renderer::UIPass& uiPass);

    /**
     * @brief Calculate angle from player to world position
     * @param playerPos Player position
     * @param worldPos Target world position
     * @param playerYaw Player rotation in degrees
     * @return Angle in degrees relative to compass (0 = North on compass)
     */
    float calculateMarkerAngle(const Engine::vec3& playerPos,
                              const Engine::vec3& worldPos,
                              float playerYaw) const;

    /**
     * @brief Calculate distance from player to world position
     */
    float calculateDistance(const Engine::vec3& playerPos, const Engine::vec3& worldPos) const;

    /**
     * @brief Calculate marker opacity based on distance
     */
    float calculateMarkerOpacity(const CompassMarker& marker, float distance) const;

    /**
     * @brief Format distance string
     * @param distance Distance in world units
     * @return Formatted string (e.g., "125m", "1.2km")
     */
    std::string formatDistance(float distance) const;

    /**
     * @brief Update marker animations
     */
    void updateMarkerAnimations(float deltaTime);

    /**
     * @brief Get cardinal directions
     */
    std::vector<CardinalDirection> getCardinalDirections() const;

    /**
     * @brief Calculate X position on compass bar for an angle
     * @param angle Angle relative to player (0 = directly ahead)
     * @return X position on compass, or -1 if outside visible range
     */
    float angleToCompassX(float angle) const;

    Engine::Input& m_input;

    // Markers
    std::unordered_map<std::string, CompassMarker> m_markers;

    // Built-in marker IDs
    static constexpr const char* QUEST_MARKER_ID = "__quest_marker__";
    static constexpr const char* WAYPOINT_MARKER_ID = "__waypoint_marker__";

    // Display settings
    bool m_showCardinals = true;
    bool m_showDistances = true;
    bool m_visible = true;
    float m_opacity = 1.0F;

    // Position and size (normalized screen coordinates)
    float m_screenX = 0.5F;  // Center top
    float m_screenY = 0.05F;
    float m_width = 400.0F;
    float m_height = 60.0F;

    // Player state
    Engine::vec3 m_playerPosition = Engine::vec3::zero();
    float m_playerYaw = 0.0F;

    // Animation state
    float m_animationTime = 0.0F;

    // Cardinal directions cache
    std::vector<CardinalDirection> m_cardinals;

    // Screen dimensions (cached during render)
    uint32_t m_screenWidth = 1920;
    uint32_t m_screenHeight = 1080;

    // Compass visibility range (degrees from center)
    float m_visibleRange = 90.0F;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_COMPASS_UI_HPP

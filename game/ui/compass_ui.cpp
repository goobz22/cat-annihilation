#include "compass_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Game {

CompassUI::CompassUI(Engine::Input& input)
    : m_input(input) {
}

CompassUI::~CompassUI() {
    shutdown();
}

bool CompassUI::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("CompassUI already initialized");
        return true;
    }

    // Initialize cardinal directions
    m_cardinals = {
        {"N", 0.0f, true},
        {"NE", 45.0f, false},
        {"E", 90.0f, true},
        {"SE", 135.0f, false},
        {"S", 180.0f, true},
        {"SW", 225.0f, false},
        {"W", 270.0f, true},
        {"NW", 315.0f, false}
    };

    m_initialized = true;
    Engine::Logger::info("CompassUI initialized successfully");
    return true;
}

void CompassUI::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_markers.clear();
    m_cardinals.clear();

    m_initialized = false;
    Engine::Logger::info("CompassUI shutdown");
}

void CompassUI::update(float deltaTime, const Engine::vec3& playerPos, float playerYaw) {
    if (!m_initialized || !m_visible) {
        return;
    }

    m_playerPosition = playerPos;
    m_playerYaw = playerYaw;
    m_animationTime += deltaTime;

    // Update marker animations
    updateMarkerAnimations(deltaTime);
}

void CompassUI::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized || !m_visible) {
        return;
    }

    // Render background
    renderBackground(renderer);

    // Render cardinal directions
    if (m_showCardinals) {
        renderCardinals(renderer);
    }

    // Render markers
    renderMarkers(renderer);
}

// ============================================================================
// Marker Management
// ============================================================================

void CompassUI::addMarker(const CompassMarker& marker) {
    m_markers[marker.id] = marker;
    Engine::Logger::debug("CompassUI: Added marker '{}'", marker.id);
}

void CompassUI::removeMarker(const std::string& markerId) {
    auto it = m_markers.find(markerId);
    if (it != m_markers.end()) {
        m_markers.erase(it);
        Engine::Logger::debug("CompassUI: Removed marker '{}'", markerId);
    }
}

void CompassUI::updateMarkerPosition(const std::string& markerId, const Engine::vec3& newPos) {
    auto it = m_markers.find(markerId);
    if (it != m_markers.end()) {
        it->second.worldPosition = newPos;
    }
}

void CompassUI::setMarkerVisible(const std::string& markerId, bool visible) {
    auto it = m_markers.find(markerId);
    if (it != m_markers.end()) {
        it->second.isVisible = visible;
    }
}

void CompassUI::clearAllMarkers() {
    m_markers.clear();
    Engine::Logger::debug("CompassUI: Cleared all markers");
}

// ============================================================================
// Built-in Markers
// ============================================================================

void CompassUI::setQuestObjectiveMarker(const Engine::vec3& position, const std::string& label) {
    CompassMarker marker;
    marker.id = QUEST_MARKER_ID;
    marker.label = label;
    marker.worldPosition = position;
    marker.iconPath = "assets/textures/ui/quest_icons/main.png";
    marker.color = {1.0f, 0.9f, 0.3f, 1.0f};  // Gold
    marker.importance = 2.0f;  // High priority
    marker.pulseSpeed = 2.0f;
    marker.pulseAmount = 0.3f;

    addMarker(marker);
}

void CompassUI::setWaypointMarker(const Engine::vec3& position, const std::string& label) {
    CompassMarker marker;
    marker.id = WAYPOINT_MARKER_ID;
    marker.label = label;
    marker.worldPosition = position;
    marker.iconPath = "assets/textures/ui/compass_arrow.png";
    marker.color = {0.3f, 0.8f, 1.0f, 1.0f};  // Light blue
    marker.importance = 1.5f;
    marker.pulseSpeed = 1.0f;
    marker.pulseAmount = 0.2f;

    addMarker(marker);
}

void CompassUI::clearQuestMarker() {
    removeMarker(QUEST_MARKER_ID);
}

void CompassUI::clearWaypoint() {
    removeMarker(WAYPOINT_MARKER_ID);
}

// ============================================================================
// Display Options
// ============================================================================

void CompassUI::setSize(float width, float height) {
    m_width = width;
    m_height = height;
}

void CompassUI::setPosition(float x, float y) {
    m_screenX = std::clamp(x, 0.0f, 1.0f);
    m_screenY = std::clamp(y, 0.0f, 1.0f);
}

void CompassUI::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.0f, 1.0f);
}

// ============================================================================
// Query Methods
// ============================================================================

const CompassMarker* CompassUI::getMarker(const std::string& markerId) const {
    auto it = m_markers.find(markerId);
    if (it != m_markers.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const CompassMarker*> CompassUI::getAllMarkers() const {
    std::vector<const CompassMarker*> markers;
    markers.reserve(m_markers.size());
    for (const auto& pair : m_markers) {
        markers.push_back(&pair.second);
    }
    return markers;
}

size_t CompassUI::getVisibleMarkerCount() const {
    size_t count = 0;
    for (const auto& pair : m_markers) {
        if (pair.second.isVisible) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void CompassUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement using renderer's 2D drawing API
    // Draw semi-transparent background bar
    // Position: (m_screenX, m_screenY)
    // Size: (m_width, m_height)
    // Color: Black with m_opacity * 0.7 alpha
}

void CompassUI::renderCardinals(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement using renderer's 2D drawing API
    // For each cardinal direction:
    // 1. Calculate position on compass based on player yaw
    // 2. If within visible range of compass:
    //    - Draw cardinal letter (larger for N, E, S, W)
    //    - Position relative to center of compass
    //    - Fade out at edges
}

void CompassUI::renderMarkers(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement using renderer's 2D drawing API
    // For each marker:
    // 1. Calculate angle and distance
    // 2. Calculate position on compass
    // 3. Calculate opacity based on distance and marker settings
    // 4. If within visible range:
    //    - Draw marker icon
    //    - Draw label if space permits
    //    - Draw distance if m_showDistances is true
    //    - Apply pulse animation if configured

    // Example implementation structure:
    /*
    std::vector<std::pair<float, const CompassMarker*>> sortedMarkers;

    for (const auto& pair : m_markers) {
        const CompassMarker& marker = pair.second;
        if (!marker.isVisible) continue;

        float distance = calculateDistance(m_playerPosition, marker.worldPosition);
        sortedMarkers.push_back({marker.importance, &marker});
    }

    // Sort by importance (highest first)
    std::sort(sortedMarkers.begin(), sortedMarkers.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& pair : sortedMarkers) {
        const CompassMarker* marker = pair.second;

        float angle = calculateMarkerAngle(m_playerPosition, marker->worldPosition, m_playerYaw);
        float distance = calculateDistance(m_playerPosition, marker->worldPosition);
        float opacity = calculateMarkerOpacity(*marker, distance) * m_opacity;

        // Render marker at calculated position with opacity
        // ...
    }
    */
}

// ============================================================================
// Private Helper Methods
// ============================================================================

float CompassUI::calculateMarkerAngle(const Engine::vec3& playerPos,
                                     const Engine::vec3& worldPos,
                                     float playerYaw) const {
    // Calculate direction vector from player to marker
    float dx = worldPos.x - playerPos.x;
    float dz = worldPos.z - playerPos.z;

    // Calculate angle in world space (0 = North, 90 = East)
    float worldAngle = std::atan2(dx, dz) * (180.0f / 3.14159265f);

    // Convert to compass angle (relative to player facing direction)
    float compassAngle = worldAngle - playerYaw;

    // Normalize to 0-360 range
    while (compassAngle < 0.0f) compassAngle += 360.0f;
    while (compassAngle >= 360.0f) compassAngle -= 360.0f;

    return compassAngle;
}

float CompassUI::calculateDistance(const Engine::vec3& playerPos, const Engine::vec3& worldPos) const {
    float dx = worldPos.x - playerPos.x;
    float dy = worldPos.y - playerPos.y;
    float dz = worldPos.z - playerPos.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float CompassUI::calculateMarkerOpacity(const CompassMarker& marker, float distance) const {
    float opacity = 1.0f;

    // Fade out beyond max distance
    if (marker.maxDistance > 0.0f && distance > marker.maxDistance) {
        float fadeRange = marker.maxDistance * 0.2f;  // 20% fade zone
        float fadeAmount = (distance - marker.maxDistance) / fadeRange;
        opacity = 1.0f - std::clamp(fadeAmount, 0.0f, 1.0f);
    }

    // Hide below min distance
    if (marker.minDistance > 0.0f && distance < marker.minDistance) {
        opacity = 0.0f;
    }

    return opacity;
}

std::string CompassUI::formatDistance(float distance) const {
    std::ostringstream ss;

    if (distance < 1000.0f) {
        // Show in meters
        ss << static_cast<int>(distance) << "m";
    } else {
        // Show in kilometers
        ss << std::fixed << std::setprecision(1) << (distance / 1000.0f) << "km";
    }

    return ss.str();
}

void CompassUI::updateMarkerAnimations(float deltaTime) {
    // Update pulse animations for markers
    for (auto& pair : m_markers) {
        CompassMarker& marker = pair.second;

        if (marker.pulseSpeed > 0.0f) {
            // Pulse is handled in render using m_animationTime
            // No per-marker state needed
        }
    }
}

std::vector<CardinalDirection> CompassUI::getCardinalDirections() const {
    return m_cardinals;
}

} // namespace Game

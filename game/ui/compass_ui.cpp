#include "compass_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Game {

namespace {
    constexpr float PI = 3.14159265358979323846F;
}

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
        {"N", 0.0F, true},
        {"NE", 45.0F, false},
        {"E", 90.0F, true},
        {"SE", 135.0F, false},
        {"S", 180.0F, true},
        {"SW", 225.0F, false},
        {"W", 270.0F, true},
        {"NW", 315.0F, false}
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

void CompassUI::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized || !m_visible) {
        return;
    }

    // Cache screen dimensions
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Render background
    renderBackground(uiPass);

    // Render cardinal directions
    if (m_showCardinals) {
        renderCardinals(uiPass);
    }

    // Render markers
    renderMarkers(uiPass);
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
    marker.color = {1.0F, 0.9F, 0.3F, 1.0F};  // Gold
    marker.importance = 2.0F;  // High priority
    marker.pulseSpeed = 2.0F;
    marker.pulseAmount = 0.3F;

    addMarker(marker);
}

void CompassUI::setWaypointMarker(const Engine::vec3& position, const std::string& label) {
    CompassMarker marker;
    marker.id = WAYPOINT_MARKER_ID;
    marker.label = label;
    marker.worldPosition = position;
    marker.iconPath = "assets/textures/ui/compass_arrow.png";
    marker.color = {0.3F, 0.8F, 1.0F, 1.0F};  // Light blue
    marker.importance = 1.5F;
    marker.pulseSpeed = 1.0F;
    marker.pulseAmount = 0.2F;

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
    m_screenX = std::clamp(x, 0.0F, 1.0F);
    m_screenY = std::clamp(y, 0.0F, 1.0F);
}

void CompassUI::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.0F, 1.0F);
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

void CompassUI::renderBackground(CatEngine::Renderer::UIPass& uiPass) {
    // Calculate compass position (centered at top of screen)
    float compassX = (static_cast<float>(m_screenWidth) * m_screenX) - (m_width / 2.0F);
    float compassY = static_cast<float>(m_screenHeight) * m_screenY;

    // Background bar
    CatEngine::Renderer::UIPass::QuadDesc bgQuad;
    bgQuad.x = compassX;
    bgQuad.y = compassY;
    bgQuad.width = m_width;
    bgQuad.height = m_height;
    bgQuad.r = 0.0F;
    bgQuad.g = 0.0F;
    bgQuad.b = 0.0F;
    bgQuad.a = 0.7F * m_opacity;
    bgQuad.depth = 0.0F;
    bgQuad.texture = nullptr;
    uiPass.DrawQuad(bgQuad);

    // Gradient edges for fade effect (left)
    CatEngine::Renderer::UIPass::QuadDesc leftFade;
    leftFade.x = compassX;
    leftFade.y = compassY;
    leftFade.width = 40.0F;
    leftFade.height = m_height;
    leftFade.r = 0.0F;
    leftFade.g = 0.0F;
    leftFade.b = 0.0F;
    leftFade.a = 0.3F * m_opacity;
    leftFade.depth = 0.05F;
    leftFade.texture = nullptr;
    uiPass.DrawQuad(leftFade);

    // Gradient edges for fade effect (right)
    CatEngine::Renderer::UIPass::QuadDesc rightFade;
    rightFade.x = (compassX + m_width) - 40.0F;
    rightFade.y = compassY;
    rightFade.width = 40.0F;
    rightFade.height = m_height;
    rightFade.r = 0.0F;
    rightFade.g = 0.0F;
    rightFade.b = 0.0F;
    rightFade.a = 0.3F * m_opacity;
    rightFade.depth = 0.05F;
    rightFade.texture = nullptr;
    uiPass.DrawQuad(rightFade);

    // Center tick mark
    float centerX = compassX + (m_width / 2.0F);
    CatEngine::Renderer::UIPass::QuadDesc centerTick;
    centerTick.x = centerX - 1.0F;
    centerTick.y = compassY;
    centerTick.width = 2.0F;
    centerTick.height = 15.0F;
    centerTick.r = 1.0F;
    centerTick.g = 1.0F;
    centerTick.b = 1.0F;
    centerTick.a = 0.9F * m_opacity;
    centerTick.depth = 0.1F;
    centerTick.texture = nullptr;
    uiPass.DrawQuad(centerTick);

    // Bottom center tick
    CatEngine::Renderer::UIPass::QuadDesc bottomTick;
    bottomTick.x = centerX - 1.0F;
    bottomTick.y = (compassY + m_height) - 15.0F;
    bottomTick.width = 2.0F;
    bottomTick.height = 15.0F;
    bottomTick.r = 1.0F;
    bottomTick.g = 1.0F;
    bottomTick.b = 1.0F;
    bottomTick.a = 0.9F * m_opacity;
    bottomTick.depth = 0.1F;
    bottomTick.texture = nullptr;
    uiPass.DrawQuad(bottomTick);
}

void CompassUI::renderCardinals(CatEngine::Renderer::UIPass& uiPass) {
    float compassX = (static_cast<float>(m_screenWidth) * m_screenX) - (m_width / 2.0F);
    float compassY = static_cast<float>(m_screenHeight) * m_screenY;
    float centerX = compassX + (m_width / 2.0F);
    float centerY = compassY + (m_height / 2.0F);

    for (const auto& cardinal : m_cardinals) {
        // Calculate angle relative to player's facing direction
        float relativeAngle = cardinal.angle - m_playerYaw;

        // Normalize angle to -180 to 180 range
        while (relativeAngle > 180.0F) relativeAngle -= 360.0F;
        while (relativeAngle < -180.0F) relativeAngle += 360.0F;

        // Check if within visible range
        if (std::abs(relativeAngle) > m_visibleRange) {
            continue;
        }

        // Calculate X position on compass
        float xPos = angleToCompassX(relativeAngle);
        if (xPos < 0.0F) {
            continue;
        }

        float screenXPos = compassX + xPos;

        // Calculate opacity based on distance from center (fade at edges)
        float distFromCenter = std::abs(relativeAngle) / m_visibleRange;
        float alpha = 1.0F - (distFromCenter * distFromCenter);
        alpha = std::max(0.0F, alpha) * m_opacity;

        // Determine color and size based on primary/secondary
        float fontSize = cardinal.isPrimary ? 20.0F : 14.0F;
        float r = 1.0F;
        float g = 1.0F;
        float b = 1.0F;

        // North is red/orange for emphasis
        if (cardinal.label == "N") {
            r = 1.0F;
            g = 0.3F;
            b = 0.2F;
        }

        // Draw cardinal letter
        CatEngine::Renderer::UIPass::TextDesc textDesc;
        textDesc.text = cardinal.label.c_str();
        textDesc.x = screenXPos - (fontSize * 0.3F * static_cast<float>(cardinal.label.length()));
        textDesc.y = centerY - (fontSize / 2.0F);
        textDesc.fontSize = fontSize;
        textDesc.r = r;
        textDesc.g = g;
        textDesc.b = b;
        textDesc.a = alpha;
        textDesc.depth = 0.2F;
        textDesc.fontAtlas = nullptr;
        uiPass.DrawText(textDesc);

        // Draw tick mark for primary cardinals
        if (cardinal.isPrimary) {
            CatEngine::Renderer::UIPass::QuadDesc tickQuad;
            tickQuad.x = screenXPos - 1.0F;
            tickQuad.y = compassY + 5.0F;
            tickQuad.width = 2.0F;
            tickQuad.height = 8.0F;
            tickQuad.r = r;
            tickQuad.g = g;
            tickQuad.b = b;
            tickQuad.a = alpha * 0.7F;
            tickQuad.depth = 0.15F;
            tickQuad.texture = nullptr;
            uiPass.DrawQuad(tickQuad);
        }
    }
}

void CompassUI::renderMarkers(CatEngine::Renderer::UIPass& uiPass) {
    float compassX = (static_cast<float>(m_screenWidth) * m_screenX) - (m_width / 2.0F);
    float compassY = static_cast<float>(m_screenHeight) * m_screenY;
    float centerY = compassY + (m_height / 2.0F);

    // Collect and sort markers by importance
    std::vector<std::pair<float, const CompassMarker*>> sortedMarkers;

    for (const auto& pair : m_markers) {
        const CompassMarker& marker = pair.second;
        if (!marker.isVisible) {
            continue;
        }

        float distance = calculateDistance(m_playerPosition, marker.worldPosition);
        sortedMarkers.push_back({marker.importance, &marker});
    }

    // Sort by importance (highest first)
    std::sort(sortedMarkers.begin(), sortedMarkers.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& sortedPair : sortedMarkers) {
        const CompassMarker* marker = sortedPair.second;

        // Calculate angle to marker
        float angle = calculateMarkerAngle(m_playerPosition, marker->worldPosition, m_playerYaw);

        // Normalize angle to -180 to 180 range
        while (angle > 180.0F) angle -= 360.0F;
        while (angle < -180.0F) angle += 360.0F;

        // Check if within visible range
        if (std::abs(angle) > m_visibleRange) {
            continue;
        }

        // Calculate X position on compass
        float xPos = angleToCompassX(angle);
        if (xPos < 0.0F) {
            continue;
        }

        float screenXPos = compassX + xPos;

        // Calculate opacity
        float distance = calculateDistance(m_playerPosition, marker->worldPosition);
        float distanceOpacity = calculateMarkerOpacity(*marker, distance);

        // Edge fade
        float distFromCenter = std::abs(angle) / m_visibleRange;
        float edgeFade = 1.0F - (distFromCenter * distFromCenter);
        edgeFade = std::max(0.0F, edgeFade);

        float alpha = distanceOpacity * edgeFade * m_opacity;

        // Apply pulse animation
        if (marker->pulseSpeed > 0.0F) {
            float pulse = std::sin(m_animationTime * marker->pulseSpeed * PI * 2.0F);
            pulse = (pulse * 0.5F) + 0.5F;  // Normalize to 0-1
            alpha *= 1.0F + (pulse * marker->pulseAmount);
        }

        alpha = std::clamp(alpha, 0.0F, 1.0F);

        // Draw marker icon (simple diamond shape for now)
        float iconSize = 16.0F * (marker->importance > 1.5F ? 1.2F : 1.0F);

        // Draw diamond shape using two triangles (approximated with quads)
        CatEngine::Renderer::UIPass::QuadDesc iconQuad;
        iconQuad.x = screenXPos - (iconSize / 2.0F);
        iconQuad.y = centerY - (iconSize / 2.0F);
        iconQuad.width = iconSize;
        iconQuad.height = iconSize;
        iconQuad.r = marker->color.x;
        iconQuad.g = marker->color.y;
        iconQuad.b = marker->color.z;
        iconQuad.a = alpha * marker->color.w;
        iconQuad.depth = 0.3F;
        iconQuad.texture = nullptr;  // Would use actual icon texture
        uiPass.DrawQuad(iconQuad);

        // Draw distance text if enabled and close enough
        if (m_showDistances && distance < 10000.0F) {
            std::string distStr = formatDistance(distance);

            CatEngine::Renderer::UIPass::TextDesc distText;
            distText.text = distStr.c_str();
            distText.x = screenXPos - 15.0F;
            distText.y = (compassY + m_height) - 18.0F;
            distText.fontSize = 10.0F;
            distText.r = marker->color.x;
            distText.g = marker->color.y;
            distText.b = marker->color.z;
            distText.a = alpha * 0.8F;
            distText.depth = 0.35F;
            distText.fontAtlas = nullptr;
            uiPass.DrawText(distText);
        }

        // Draw label if marker has one
        if (!marker->label.empty()) {
            CatEngine::Renderer::UIPass::TextDesc labelText;
            labelText.text = marker->label.c_str();
            labelText.x = screenXPos - (static_cast<float>(marker->label.length()) * 3.0F);
            labelText.y = compassY + 3.0F;
            labelText.fontSize = 10.0F;
            labelText.r = marker->color.x;
            labelText.g = marker->color.y;
            labelText.b = marker->color.z;
            labelText.a = alpha * 0.9F;
            labelText.depth = 0.35F;
            labelText.fontAtlas = nullptr;
            uiPass.DrawText(labelText);
        }
    }
}

// ============================================================================
// Private Helper Methods
// ============================================================================

float CompassUI::angleToCompassX(float angle) const {
    // Angle is in range [-visibleRange, visibleRange]
    // Map to [0, m_width]
    if (std::abs(angle) > m_visibleRange) {
        return -1.0F;  // Outside visible range
    }

    float normalizedAngle = (angle / m_visibleRange) + 1.0F;  // 0 to 2
    normalizedAngle *= 0.5F;  // 0 to 1
    return normalizedAngle * m_width;
}

float CompassUI::calculateMarkerAngle(const Engine::vec3& playerPos,
                                     const Engine::vec3& worldPos,
                                     float playerYaw) const {
    // Calculate direction vector from player to marker
    float dx = worldPos.x - playerPos.x;
    float dz = worldPos.z - playerPos.z;

    // Calculate angle in world space (0 = North, 90 = East)
    float worldAngle = std::atan2(dx, dz) * (180.0F / PI);

    // Convert to compass angle (relative to player facing direction)
    float compassAngle = worldAngle - playerYaw;

    // Normalize to -180 to 180 range
    while (compassAngle > 180.0F) compassAngle -= 360.0F;
    while (compassAngle < -180.0F) compassAngle += 360.0F;

    return compassAngle;
}

float CompassUI::calculateDistance(const Engine::vec3& playerPos, const Engine::vec3& worldPos) const {
    float dx = worldPos.x - playerPos.x;
    float dy = worldPos.y - playerPos.y;
    float dz = worldPos.z - playerPos.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float CompassUI::calculateMarkerOpacity(const CompassMarker& marker, float distance) const {
    float opacity = 1.0F;

    // Fade out beyond max distance
    if (marker.maxDistance > 0.0F && distance > marker.maxDistance) {
        float fadeRange = marker.maxDistance * 0.2F;  // 20% fade zone
        float fadeAmount = (distance - marker.maxDistance) / fadeRange;
        opacity = 1.0F - std::clamp(fadeAmount, 0.0F, 1.0F);
    }

    // Hide below min distance
    if (marker.minDistance > 0.0F && distance < marker.minDistance) {
        opacity = 0.0F;
    }

    return opacity;
}

std::string CompassUI::formatDistance(float distance) const {
    std::ostringstream ss;

    if (distance < 1000.0F) {
        // Show in meters
        ss << static_cast<int>(distance) << "m";
    } else {
        // Show in kilometers
        ss << std::fixed << std::setprecision(1) << (distance / 1000.0F) << "km";
    }

    return ss.str();
}

void CompassUI::updateMarkerAnimations(float /*deltaTime*/) {
    // Pulse animations are calculated directly in render using m_animationTime
    // No per-marker state updates needed
}

std::vector<CardinalDirection> CompassUI::getCardinalDirections() const {
    return m_cardinals;
}

} // namespace Game

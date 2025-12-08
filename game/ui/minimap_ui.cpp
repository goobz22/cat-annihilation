#include "minimap_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace Game {

// Helper to create hash for fog cell coordinates
static uint64_t fogCellHash(int x, int y) {
    return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y);
}

MinimapUI::MinimapUI(Engine::Input& input)
    : m_input(input) {
}

MinimapUI::~MinimapUI() {
    shutdown();
}

bool MinimapUI::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("MinimapUI already initialized");
        return true;
    }

    m_targetZoom = m_zoom;

    m_initialized = true;
    Engine::Logger::info("MinimapUI initialized successfully");
    return true;
}

void MinimapUI::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_icons.clear();
    m_fogCells.clear();
    m_pings.clear();

    m_initialized = false;
    Engine::Logger::info("MinimapUI shutdown");
}

void MinimapUI::update(float deltaTime, const Engine::vec3& playerPos, float playerYaw) {
    if (!m_initialized || !m_visible) {
        return;
    }

    m_playerPosition = playerPos;
    m_playerYaw = playerYaw;

    // Smooth zoom animation
    if (std::abs(m_zoom - m_targetZoom) > 0.001f) {
        m_zoom += (m_targetZoom - m_zoom) * m_zoomSpeed * deltaTime;
    }

    // Update pings
    updatePings(deltaTime);

    // Auto-reveal area around player
    if (m_fogOfWarEnabled) {
        revealArea(playerPos, 20.0f);  // Reveal 20 units around player
    }
}

void MinimapUI::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized || !m_visible) {
        return;
    }

    // Render background
    renderBackground(renderer);

    // Render fog of war
    if (m_fogOfWarEnabled) {
        renderFogOfWar(renderer);
    }

    // Render icons
    renderIcons(renderer);

    // Render pings
    renderPings(renderer);

    // Render player indicator
    renderPlayer(renderer);

    // Render border
    renderBorder(renderer);
}

// ============================================================================
// Configuration
// ============================================================================

void MinimapUI::setSize(float radius) {
    m_radius = std::max(50.0f, radius);
    Engine::Logger::debug("MinimapUI: Set size to {}", m_radius);
}

void MinimapUI::setZoom(float zoom) {
    m_targetZoom = std::clamp(zoom, m_minZoom, m_maxZoom);
    Engine::Logger::debug("MinimapUI: Set zoom to {}", m_targetZoom);
}

void MinimapUI::zoomIn() {
    m_targetZoom = std::clamp(m_targetZoom * 1.5f, m_minZoom, m_maxZoom);
}

void MinimapUI::zoomOut() {
    m_targetZoom = std::clamp(m_targetZoom / 1.5f, m_minZoom, m_maxZoom);
}

void MinimapUI::setPosition(const Engine::vec2& screenPos) {
    m_screenPos.x = std::clamp(screenPos.x, 0.0f, 1.0f);
    m_screenPos.y = std::clamp(screenPos.y, 0.0f, 1.0f);
}

void MinimapUI::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.0f, 1.0f);
}

// ============================================================================
// Icons
// ============================================================================

void MinimapUI::addIcon(const std::string& id, const Engine::vec3& worldPos,
                       const std::string& iconPath) {
    MinimapIcon icon;
    icon.id = id;
    icon.worldPosition = worldPos;
    icon.iconPath = iconPath;
    m_icons[id] = icon;
}

void MinimapUI::removeIcon(const std::string& id) {
    m_icons.erase(id);
}

void MinimapUI::updateIconPosition(const std::string& id, const Engine::vec3& newPos) {
    auto it = m_icons.find(id);
    if (it != m_icons.end()) {
        it->second.worldPosition = newPos;
    }
}

void MinimapUI::updateIconRotation(const std::string& id, float rotation) {
    auto it = m_icons.find(id);
    if (it != m_icons.end()) {
        it->second.rotation = rotation;
    }
}

void MinimapUI::setIconVisible(const std::string& id, bool visible) {
    auto it = m_icons.find(id);
    if (it != m_icons.end()) {
        it->second.isVisible = visible;
    }
}

void MinimapUI::clearAllIcons() {
    m_icons.clear();
    Engine::Logger::debug("MinimapUI: Cleared all icons");
}

// ============================================================================
// Built-in Markers
// ============================================================================

void MinimapUI::addEnemyMarker(const std::string& enemyId, const Engine::vec3& worldPos) {
    addIcon("enemy_" + enemyId, worldPos, "assets/textures/ui/minimap_icons/enemy.png");
    auto it = m_icons.find("enemy_" + enemyId);
    if (it != m_icons.end()) {
        it->second.color = {1.0f, 0.2f, 0.2f, 1.0f};  // Red
    }
}

void MinimapUI::addNPCMarker(const std::string& npcId, const Engine::vec3& worldPos) {
    addIcon("npc_" + npcId, worldPos, "assets/textures/ui/minimap_icons/npc.png");
    auto it = m_icons.find("npc_" + npcId);
    if (it != m_icons.end()) {
        it->second.color = {0.3f, 1.0f, 0.3f, 1.0f};  // Green
    }
}

void MinimapUI::addQuestMarker(const std::string& questId, const Engine::vec3& worldPos) {
    addIcon("quest_" + questId, worldPos, "assets/textures/ui/minimap_icons/quest.png");
    auto it = m_icons.find("quest_" + questId);
    if (it != m_icons.end()) {
        it->second.color = {1.0f, 0.9f, 0.3f, 1.0f};  // Gold
    }
}

void MinimapUI::removeEnemyMarker(const std::string& enemyId) {
    removeIcon("enemy_" + enemyId);
}

void MinimapUI::removeNPCMarker(const std::string& npcId) {
    removeIcon("npc_" + npcId);
}

void MinimapUI::removeQuestMarker(const std::string& questId) {
    removeIcon("quest_" + questId);
}

// ============================================================================
// Fog of War
// ============================================================================

void MinimapUI::revealArea(const Engine::vec3& center, float radius) {
    if (!m_fogOfWarEnabled) {
        return;
    }

    // Calculate cell range
    int minX = static_cast<int>((center.x - radius) / m_fogCellSize) - 1;
    int maxX = static_cast<int>((center.x + radius) / m_fogCellSize) + 1;
    int minY = static_cast<int>((center.z - radius) / m_fogCellSize) - 1;
    int maxY = static_cast<int>((center.z + radius) / m_fogCellSize) + 1;

    // Reveal cells in range
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            // Calculate cell center
            float cellCenterX = (x + 0.5f) * m_fogCellSize;
            float cellCenterZ = (y + 0.5f) * m_fogCellSize;

            // Check distance
            float dx = cellCenterX - center.x;
            float dz = cellCenterZ - center.z;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist <= radius) {
                uint64_t hash = fogCellHash(x, y);
                auto it = m_fogCells.find(hash);

                if (it == m_fogCells.end()) {
                    // Create new revealed cell
                    FogCell cell;
                    cell.x = x;
                    cell.y = y;
                    cell.revealed = true;
                    cell.revealTime = 0.0f;  // TODO: Use actual game time
                    m_fogCells[hash] = cell;
                } else if (!it->second.revealed) {
                    // Reveal existing cell
                    it->second.revealed = true;
                    it->second.revealTime = 0.0f;
                }
            }
        }
    }
}

bool MinimapUI::isAreaRevealed(const Engine::vec3& position) const {
    if (!m_fogOfWarEnabled) {
        return true;
    }

    int x, y;
    const_cast<MinimapUI*>(this)->worldToFogCell(position, x, y);

    const FogCell* cell = getFogCell(x, y);
    return cell && cell->revealed;
}

void MinimapUI::clearFogOfWar() {
    // Mark all cells as revealed
    for (auto& pair : m_fogCells) {
        pair.second.revealed = true;
    }
    Engine::Logger::debug("MinimapUI: Cleared fog of war");
}

void MinimapUI::resetFogOfWar() {
    m_fogCells.clear();
    Engine::Logger::debug("MinimapUI: Reset fog of war");
}

// ============================================================================
// Ping System
// ============================================================================

void MinimapUI::createPing(const Engine::vec3& worldPos, const Engine::vec4& color,
                          float duration) {
    Ping ping;
    ping.worldPosition = worldPos;
    ping.color = color;
    ping.lifetime = duration;
    ping.maxLifetime = duration;
    m_pings.push_back(ping);
    Engine::Logger::debug("MinimapUI: Created ping at ({}, {}, {})",
                        worldPos.x, worldPos.y, worldPos.z);
}

void MinimapUI::clearPings() {
    m_pings.clear();
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void MinimapUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement using renderer's 2D drawing API
    // Draw circular or square background
    // Apply m_opacity
    // Position at m_screenPos
}

void MinimapUI::renderFogOfWar(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement fog of war rendering
    // Draw darkened areas for unrevealed cells
    // Draw visible terrain for revealed cells
}

void MinimapUI::renderIcons(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement icon rendering
    // For each icon:
    //   1. Convert world position to minimap position
    //   2. Check if within minimap bounds
    //   3. Check category visibility flags (enemies, NPCs, etc.)
    //   4. Rotate icon if needed
    //   5. Draw icon with color and scale
}

void MinimapUI::renderPlayer(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement player indicator rendering
    // Draw player icon at center of minimap
    // Rotate based on player yaw (if m_rotateWithPlayer is false)
    // Draw directional arrow or triangle
}

void MinimapUI::renderPings(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement ping rendering
    // For each ping:
    //   1. Convert world position to minimap position
    //   2. Draw pulsing circle/indicator
    //   3. Apply color and fade based on lifetime
}

void MinimapUI::renderBorder(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement border rendering
    // Draw circular or square border around minimap
    // Apply m_opacity
}

// ============================================================================
// Private Helper Methods
// ============================================================================

Engine::vec2 MinimapUI::worldToMinimapPos(const Engine::vec3& worldPos) const {
    // Calculate relative position to player
    float dx = worldPos.x - m_playerPosition.x;
    float dz = worldPos.z - m_playerPosition.z;

    // Apply zoom
    dx *= m_zoom;
    dz *= m_zoom;

    // Rotate if minimap rotates with player
    if (m_rotateWithPlayer) {
        float yawRad = m_playerYaw * (3.14159265f / 180.0f);
        float cosYaw = std::cos(yawRad);
        float sinYaw = std::sin(yawRad);

        float rotatedX = dx * cosYaw - dz * sinYaw;
        float rotatedZ = dx * sinYaw + dz * cosYaw;

        dx = rotatedX;
        dz = rotatedZ;
    }

    // Convert to minimap coordinates (dz is inverted because -Z is forward in many engines)
    Engine::vec2 minimapPos;
    minimapPos.x = dx;
    minimapPos.y = -dz;  // Invert Z for top-down view

    return minimapPos;
}

bool MinimapUI::isWithinMinimapBounds(const Engine::vec2& minimapPos) const {
    if (m_isCircular) {
        // Check if within circle
        float distSq = minimapPos.x * minimapPos.x + minimapPos.y * minimapPos.y;
        return distSq <= (m_radius * m_radius);
    } else {
        // Check if within square
        return std::abs(minimapPos.x) <= m_radius && std::abs(minimapPos.y) <= m_radius;
    }
}

void MinimapUI::worldToFogCell(const Engine::vec3& worldPos, int& outX, int& outY) const {
    outX = static_cast<int>(std::floor(worldPos.x / m_fogCellSize));
    outY = static_cast<int>(std::floor(worldPos.z / m_fogCellSize));
}

FogCell* MinimapUI::getFogCell(int x, int y) {
    uint64_t hash = fogCellHash(x, y);
    auto it = m_fogCells.find(hash);
    return (it != m_fogCells.end()) ? &it->second : nullptr;
}

const FogCell* MinimapUI::getFogCell(int x, int y) const {
    uint64_t hash = fogCellHash(x, y);
    auto it = m_fogCells.find(hash);
    return (it != m_fogCells.end()) ? &it->second : nullptr;
}

void MinimapUI::updatePings(float deltaTime) {
    // Update ping lifetimes and remove expired pings
    for (auto& ping : m_pings) {
        ping.lifetime -= deltaTime;
        ping.pulsePhase += deltaTime * 3.0f;  // Pulse speed
    }

    // Remove expired pings
    m_pings.erase(
        std::remove_if(m_pings.begin(), m_pings.end(),
            [](const Ping& p) { return p.lifetime <= 0.0f; }),
        m_pings.end()
    );
}

std::string MinimapUI::getIconPrefix(const std::string& category) const {
    // Helper to generate icon ID prefix
    return category + "_";
}

} // namespace Game

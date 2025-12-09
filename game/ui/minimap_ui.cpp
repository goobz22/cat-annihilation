#include "minimap_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace Game {

namespace {
    constexpr float PI = 3.14159265358979323846F;

    // Helper to create hash for fog cell coordinates
    uint64_t fogCellHash(int x, int y) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(y));
    }
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
    if (std::abs(m_zoom - m_targetZoom) > 0.001F) {
        m_zoom += (m_targetZoom - m_zoom) * m_zoomSpeed * deltaTime;
    }

    // Update pings
    updatePings(deltaTime);

    // Auto-reveal area around player
    if (m_fogOfWarEnabled) {
        revealArea(playerPos, 20.0F);  // Reveal 20 units around player
    }
}

void MinimapUI::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized || !m_visible) {
        return;
    }

    // Cache screen dimensions
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Render background
    renderBackground(uiPass);

    // Render fog of war
    if (m_fogOfWarEnabled) {
        renderFogOfWar(uiPass);
    }

    // Render icons
    renderIcons(uiPass);

    // Render pings
    renderPings(uiPass);

    // Render player indicator
    renderPlayer(uiPass);

    // Render border
    renderBorder(uiPass);
}

// ============================================================================
// Configuration
// ============================================================================

void MinimapUI::setSize(float radius) {
    m_radius = std::max(50.0F, radius);
    Engine::Logger::debug("MinimapUI: Set size to {}", m_radius);
}

void MinimapUI::setZoom(float zoom) {
    m_targetZoom = std::clamp(zoom, m_minZoom, m_maxZoom);
    Engine::Logger::debug("MinimapUI: Set zoom to {}", m_targetZoom);
}

void MinimapUI::zoomIn() {
    m_targetZoom = std::clamp(m_targetZoom * 1.5F, m_minZoom, m_maxZoom);
}

void MinimapUI::zoomOut() {
    m_targetZoom = std::clamp(m_targetZoom / 1.5F, m_minZoom, m_maxZoom);
}

void MinimapUI::setPosition(const Engine::vec2& screenPos) {
    m_screenPos.x = std::clamp(screenPos.x, 0.0F, 1.0F);
    m_screenPos.y = std::clamp(screenPos.y, 0.0F, 1.0F);
}

void MinimapUI::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.0F, 1.0F);
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
    std::string iconId = "enemy_" + enemyId;
    addIcon(iconId, worldPos, "assets/textures/ui/minimap_icons/enemy.png");
    auto it = m_icons.find(iconId);
    if (it != m_icons.end()) {
        it->second.color = {1.0F, 0.2F, 0.2F, 1.0F};  // Red
    }
}

void MinimapUI::addNPCMarker(const std::string& npcId, const Engine::vec3& worldPos) {
    std::string iconId = "npc_" + npcId;
    addIcon(iconId, worldPos, "assets/textures/ui/minimap_icons/npc.png");
    auto it = m_icons.find(iconId);
    if (it != m_icons.end()) {
        it->second.color = {0.3F, 1.0F, 0.3F, 1.0F};  // Green
    }
}

void MinimapUI::addQuestMarker(const std::string& questId, const Engine::vec3& worldPos) {
    std::string iconId = "quest_" + questId;
    addIcon(iconId, worldPos, "assets/textures/ui/minimap_icons/quest.png");
    auto it = m_icons.find(iconId);
    if (it != m_icons.end()) {
        it->second.color = {1.0F, 0.9F, 0.3F, 1.0F};  // Gold
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
            float cellCenterX = (static_cast<float>(x) + 0.5F) * m_fogCellSize;
            float cellCenterZ = (static_cast<float>(y) + 0.5F) * m_fogCellSize;

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
                    cell.revealTime = 0.0F;
                    m_fogCells[hash] = cell;
                } else if (!it->second.revealed) {
                    // Reveal existing cell
                    it->second.revealed = true;
                    it->second.revealTime = 0.0F;
                }
            }
        }
    }
}

bool MinimapUI::isAreaRevealed(const Engine::vec3& position) const {
    if (!m_fogOfWarEnabled) {
        return true;
    }

    int x = 0;
    int y = 0;
    worldToFogCell(position, x, y);

    const FogCell* cell = getFogCell(x, y);
    return cell != nullptr && cell->revealed;
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

void MinimapUI::renderBackground(CatEngine::Renderer::UIPass& uiPass) {
    // Calculate minimap center position on screen
    float centerX = static_cast<float>(m_screenWidth) * m_screenPos.x;
    float centerY = static_cast<float>(m_screenHeight) * m_screenPos.y;

    // Draw background (circular or square)
    if (m_isCircular) {
        // For circular minimap, draw a series of concentric circles
        // Approximate with a square for now (proper circle would need shader support)
        CatEngine::Renderer::UIPass::QuadDesc bgQuad;
        bgQuad.x = centerX - m_radius;
        bgQuad.y = centerY - m_radius;
        bgQuad.width = m_radius * 2.0F;
        bgQuad.height = m_radius * 2.0F;
        bgQuad.r = 0.1F;
        bgQuad.g = 0.12F;
        bgQuad.b = 0.15F;
        bgQuad.a = m_opacity * 0.9F;
        bgQuad.depth = 0.0F;
        bgQuad.texture = nullptr;
        uiPass.DrawQuad(bgQuad);
    } else {
        // Square minimap
        CatEngine::Renderer::UIPass::QuadDesc bgQuad;
        bgQuad.x = centerX - m_radius;
        bgQuad.y = centerY - m_radius;
        bgQuad.width = m_radius * 2.0F;
        bgQuad.height = m_radius * 2.0F;
        bgQuad.r = 0.1F;
        bgQuad.g = 0.12F;
        bgQuad.b = 0.15F;
        bgQuad.a = m_opacity * 0.9F;
        bgQuad.depth = 0.0F;
        bgQuad.texture = nullptr;
        uiPass.DrawQuad(bgQuad);
    }

    // Draw grid lines for visual reference
    float gridSpacing = m_radius / 4.0F;
    for (int i = -3; i <= 3; i++) {
        // Vertical lines
        CatEngine::Renderer::UIPass::QuadDesc vLine;
        vLine.x = centerX + (static_cast<float>(i) * gridSpacing) - 0.5F;
        vLine.y = centerY - m_radius;
        vLine.width = 1.0F;
        vLine.height = m_radius * 2.0F;
        vLine.r = 0.3F;
        vLine.g = 0.35F;
        vLine.b = 0.4F;
        vLine.a = m_opacity * 0.3F;
        vLine.depth = 0.05F;
        vLine.texture = nullptr;
        uiPass.DrawQuad(vLine);

        // Horizontal lines
        CatEngine::Renderer::UIPass::QuadDesc hLine;
        hLine.x = centerX - m_radius;
        hLine.y = centerY + (static_cast<float>(i) * gridSpacing) - 0.5F;
        hLine.width = m_radius * 2.0F;
        hLine.height = 1.0F;
        hLine.r = 0.3F;
        hLine.g = 0.35F;
        hLine.b = 0.4F;
        hLine.a = m_opacity * 0.3F;
        hLine.depth = 0.05F;
        hLine.texture = nullptr;
        uiPass.DrawQuad(hLine);
    }
}

void MinimapUI::renderFogOfWar(CatEngine::Renderer::UIPass& uiPass) {
    // Calculate minimap center position on screen
    float centerX = static_cast<float>(m_screenWidth) * m_screenPos.x;
    float centerY = static_cast<float>(m_screenHeight) * m_screenPos.y;

    // Calculate the world range visible on minimap
    float worldRange = m_radius / m_zoom;

    // Calculate fog cell range to render
    int minCellX = static_cast<int>((m_playerPosition.x - worldRange) / m_fogCellSize) - 1;
    int maxCellX = static_cast<int>((m_playerPosition.x + worldRange) / m_fogCellSize) + 1;
    int minCellY = static_cast<int>((m_playerPosition.z - worldRange) / m_fogCellSize) - 1;
    int maxCellY = static_cast<int>((m_playerPosition.z + worldRange) / m_fogCellSize) + 1;

    // Render fog cells (unrevealed areas)
    for (int cellX = minCellX; cellX <= maxCellX; cellX++) {
        for (int cellY = minCellY; cellY <= maxCellY; cellY++) {
            uint64_t hash = fogCellHash(cellX, cellY);
            auto it = m_fogCells.find(hash);

            bool isRevealed = (it != m_fogCells.end()) && it->second.revealed;

            if (!isRevealed) {
                // Calculate cell world position
                float cellWorldX = (static_cast<float>(cellX) + 0.5F) * m_fogCellSize;
                float cellWorldZ = (static_cast<float>(cellY) + 0.5F) * m_fogCellSize;

                // Convert to minimap position
                Engine::vec3 cellPos = {cellWorldX, 0.0F, cellWorldZ};
                Engine::vec2 minimapPos = worldToMinimapPos(cellPos);

                if (isWithinMinimapBounds(minimapPos)) {
                    float fogSize = m_fogCellSize * m_zoom;

                    CatEngine::Renderer::UIPass::QuadDesc fogQuad;
                    fogQuad.x = centerX + minimapPos.x - (fogSize / 2.0F);
                    fogQuad.y = centerY + minimapPos.y - (fogSize / 2.0F);
                    fogQuad.width = fogSize;
                    fogQuad.height = fogSize;
                    fogQuad.r = 0.0F;
                    fogQuad.g = 0.0F;
                    fogQuad.b = 0.0F;
                    fogQuad.a = m_opacity * 0.8F;
                    fogQuad.depth = 0.1F;
                    fogQuad.texture = nullptr;
                    uiPass.DrawQuad(fogQuad);
                }
            }
        }
    }
}

void MinimapUI::renderIcons(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) * m_screenPos.x;
    float centerY = static_cast<float>(m_screenHeight) * m_screenPos.y;

    for (const auto& pair : m_icons) {
        const MinimapIcon& icon = pair.second;

        if (!icon.isVisible) {
            continue;
        }

        // Check category visibility
        if (!isIconCategoryVisible(icon.id)) {
            continue;
        }

        // Convert world position to minimap position
        Engine::vec2 minimapPos = worldToMinimapPos(icon.worldPosition);

        // Check if within bounds
        if (!isWithinMinimapBounds(minimapPos)) {
            // Draw edge indicator instead
            float angle = std::atan2(minimapPos.y, minimapPos.x);
            float edgeX = std::cos(angle) * (m_radius - 5.0F);
            float edgeY = std::sin(angle) * (m_radius - 5.0F);

            // Small triangle at edge
            CatEngine::Renderer::UIPass::QuadDesc edgeIndicator;
            edgeIndicator.x = centerX + edgeX - 4.0F;
            edgeIndicator.y = centerY + edgeY - 4.0F;
            edgeIndicator.width = 8.0F;
            edgeIndicator.height = 8.0F;
            edgeIndicator.r = icon.color.x;
            edgeIndicator.g = icon.color.y;
            edgeIndicator.b = icon.color.z;
            edgeIndicator.a = icon.color.w * m_opacity * 0.7F;
            edgeIndicator.depth = 0.25F;
            edgeIndicator.texture = nullptr;
            uiPass.DrawQuad(edgeIndicator);
            continue;
        }

        // Draw icon
        float iconSize = 10.0F * icon.scale;

        CatEngine::Renderer::UIPass::QuadDesc iconQuad;
        iconQuad.x = centerX + minimapPos.x - (iconSize / 2.0F);
        iconQuad.y = centerY + minimapPos.y - (iconSize / 2.0F);
        iconQuad.width = iconSize;
        iconQuad.height = iconSize;
        iconQuad.r = icon.color.x;
        iconQuad.g = icon.color.y;
        iconQuad.b = icon.color.z;
        iconQuad.a = icon.color.w * m_opacity;
        iconQuad.depth = 0.2F;
        iconQuad.texture = nullptr;  // Would use actual icon texture
        uiPass.DrawQuad(iconQuad);
    }
}

void MinimapUI::renderPlayer(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) * m_screenPos.x;
    float centerY = static_cast<float>(m_screenHeight) * m_screenPos.y;

    // Player is always at center of minimap
    float playerSize = 12.0F;

    // Draw player icon (simple arrow/triangle approximation)
    // Main body
    CatEngine::Renderer::UIPass::QuadDesc playerQuad;
    playerQuad.x = centerX - (playerSize / 2.0F);
    playerQuad.y = centerY - (playerSize / 2.0F);
    playerQuad.width = playerSize;
    playerQuad.height = playerSize;
    playerQuad.r = 0.2F;
    playerQuad.g = 0.6F;
    playerQuad.b = 1.0F;
    playerQuad.a = m_opacity;
    playerQuad.depth = 0.3F;
    playerQuad.texture = nullptr;
    uiPass.DrawQuad(playerQuad);

    // Direction indicator (small line pointing forward)
    float dirLength = 10.0F;
    float yawRad = m_playerYaw * (PI / 180.0F);

    // Calculate direction vector
    float dirX = std::sin(yawRad);
    float dirY = -std::cos(yawRad);  // Negative because Y increases downward on screen

    if (!m_rotateWithPlayer) {
        // If minimap doesn't rotate, show player facing direction
        CatEngine::Renderer::UIPass::QuadDesc dirIndicator;
        dirIndicator.x = centerX + (dirX * dirLength * 0.5F) - 2.0F;
        dirIndicator.y = centerY + (dirY * dirLength * 0.5F) - 2.0F;
        dirIndicator.width = 4.0F;
        dirIndicator.height = 4.0F;
        dirIndicator.r = 1.0F;
        dirIndicator.g = 1.0F;
        dirIndicator.b = 1.0F;
        dirIndicator.a = m_opacity;
        dirIndicator.depth = 0.35F;
        dirIndicator.texture = nullptr;
        uiPass.DrawQuad(dirIndicator);
    }

    // Draw player outline
    float outlineSize = playerSize + 4.0F;
    CatEngine::Renderer::UIPass::QuadDesc outlineQuad;
    outlineQuad.x = centerX - (outlineSize / 2.0F);
    outlineQuad.y = centerY - (outlineSize / 2.0F);
    outlineQuad.width = outlineSize;
    outlineQuad.height = outlineSize;
    outlineQuad.r = 1.0F;
    outlineQuad.g = 1.0F;
    outlineQuad.b = 1.0F;
    outlineQuad.a = m_opacity * 0.3F;
    outlineQuad.depth = 0.28F;
    outlineQuad.texture = nullptr;
    uiPass.DrawQuad(outlineQuad);
}

void MinimapUI::renderPings(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) * m_screenPos.x;
    float centerY = static_cast<float>(m_screenHeight) * m_screenPos.y;

    for (const auto& ping : m_pings) {
        // Convert world position to minimap position
        Engine::vec2 minimapPos = worldToMinimapPos(ping.worldPosition);

        // Calculate pulse effect
        float lifeRatio = ping.lifetime / ping.maxLifetime;
        float pulse = std::sin(ping.pulsePhase * PI * 2.0F);
        pulse = (pulse * 0.5F) + 0.5F;  // Normalize to 0-1

        float baseSize = 15.0F;
        float pingSize = baseSize + (pulse * 10.0F);
        float alpha = lifeRatio * m_opacity;

        // Check bounds and clamp to edge if needed
        float displayX = minimapPos.x;
        float displayY = minimapPos.y;

        if (!isWithinMinimapBounds(minimapPos)) {
            float angle = std::atan2(minimapPos.y, minimapPos.x);
            displayX = std::cos(angle) * (m_radius - 10.0F);
            displayY = std::sin(angle) * (m_radius - 10.0F);
        }

        // Draw ping (pulsing circle approximation)
        CatEngine::Renderer::UIPass::QuadDesc pingQuad;
        pingQuad.x = centerX + displayX - (pingSize / 2.0F);
        pingQuad.y = centerY + displayY - (pingSize / 2.0F);
        pingQuad.width = pingSize;
        pingQuad.height = pingSize;
        pingQuad.r = ping.color.x;
        pingQuad.g = ping.color.y;
        pingQuad.b = ping.color.z;
        pingQuad.a = alpha * ping.color.w * 0.7F;
        pingQuad.depth = 0.4F;
        pingQuad.texture = nullptr;
        uiPass.DrawQuad(pingQuad);

        // Inner ping
        float innerSize = pingSize * 0.5F;
        CatEngine::Renderer::UIPass::QuadDesc innerPing;
        innerPing.x = centerX + displayX - (innerSize / 2.0F);
        innerPing.y = centerY + displayY - (innerSize / 2.0F);
        innerPing.width = innerSize;
        innerPing.height = innerSize;
        innerPing.r = ping.color.x;
        innerPing.g = ping.color.y;
        innerPing.b = ping.color.z;
        innerPing.a = alpha * ping.color.w;
        innerPing.depth = 0.45F;
        innerPing.texture = nullptr;
        uiPass.DrawQuad(innerPing);
    }
}

void MinimapUI::renderBorder(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) * m_screenPos.x;
    float centerY = static_cast<float>(m_screenHeight) * m_screenPos.y;

    float borderWidth = 3.0F;

    // Top border
    CatEngine::Renderer::UIPass::QuadDesc topBorder;
    topBorder.x = centerX - m_radius - borderWidth;
    topBorder.y = centerY - m_radius - borderWidth;
    topBorder.width = (m_radius * 2.0F) + (borderWidth * 2.0F);
    topBorder.height = borderWidth;
    topBorder.r = 0.4F;
    topBorder.g = 0.45F;
    topBorder.b = 0.5F;
    topBorder.a = m_opacity;
    topBorder.depth = 0.5F;
    topBorder.texture = nullptr;
    uiPass.DrawQuad(topBorder);

    // Bottom border
    CatEngine::Renderer::UIPass::QuadDesc bottomBorder;
    bottomBorder.x = centerX - m_radius - borderWidth;
    bottomBorder.y = centerY + m_radius;
    bottomBorder.width = (m_radius * 2.0F) + (borderWidth * 2.0F);
    bottomBorder.height = borderWidth;
    bottomBorder.r = 0.4F;
    bottomBorder.g = 0.45F;
    bottomBorder.b = 0.5F;
    bottomBorder.a = m_opacity;
    bottomBorder.depth = 0.5F;
    bottomBorder.texture = nullptr;
    uiPass.DrawQuad(bottomBorder);

    // Left border
    CatEngine::Renderer::UIPass::QuadDesc leftBorder;
    leftBorder.x = centerX - m_radius - borderWidth;
    leftBorder.y = centerY - m_radius;
    leftBorder.width = borderWidth;
    leftBorder.height = m_radius * 2.0F;
    leftBorder.r = 0.4F;
    leftBorder.g = 0.45F;
    leftBorder.b = 0.5F;
    leftBorder.a = m_opacity;
    leftBorder.depth = 0.5F;
    leftBorder.texture = nullptr;
    uiPass.DrawQuad(leftBorder);

    // Right border
    CatEngine::Renderer::UIPass::QuadDesc rightBorder;
    rightBorder.x = centerX + m_radius;
    rightBorder.y = centerY - m_radius;
    rightBorder.width = borderWidth;
    rightBorder.height = m_radius * 2.0F;
    rightBorder.r = 0.4F;
    rightBorder.g = 0.45F;
    rightBorder.b = 0.5F;
    rightBorder.a = m_opacity;
    rightBorder.depth = 0.5F;
    rightBorder.texture = nullptr;
    uiPass.DrawQuad(rightBorder);

    // North indicator (only if not rotating with player)
    if (!m_rotateWithPlayer) {
        CatEngine::Renderer::UIPass::TextDesc northText;
        northText.text = "N";
        northText.x = centerX - 4.0F;
        northText.y = centerY - m_radius - 18.0F;
        northText.fontSize = 14.0F;
        northText.r = 1.0F;
        northText.g = 0.3F;
        northText.b = 0.2F;
        northText.a = m_opacity;
        northText.depth = 0.55F;
        northText.fontAtlas = nullptr;
        uiPass.DrawText(northText);
    }
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
        float yawRad = m_playerYaw * (PI / 180.0F);
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
        float distSq = (minimapPos.x * minimapPos.x) + (minimapPos.y * minimapPos.y);
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
        ping.pulsePhase += deltaTime * 3.0F;  // Pulse speed
    }

    // Remove expired pings
    m_pings.erase(
        std::remove_if(m_pings.begin(), m_pings.end(),
            [](const Ping& p) { return p.lifetime <= 0.0F; }),
        m_pings.end()
    );
}

std::string MinimapUI::getIconPrefix(const std::string& category) {
    // Helper to generate icon ID prefix
    return category + "_";
}

bool MinimapUI::isIconCategoryVisible(const std::string& iconId) const {
    // Check if the icon's category is set to visible
    if (iconId.rfind("enemy_", 0) == 0) {
        return m_showEnemies;
    }
    if (iconId.rfind("npc_", 0) == 0) {
        return m_showNPCs;
    }
    if (iconId.rfind("quest_", 0) == 0) {
        return m_showQuestMarkers;
    }
    if (iconId.rfind("teammate_", 0) == 0) {
        return m_showTeammates;
    }
    // Default: show unknown categories
    return true;
}

} // namespace Game

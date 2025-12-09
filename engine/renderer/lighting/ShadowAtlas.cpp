#include "ShadowAtlas.hpp"
#include "../../math/Math.hpp"
#include "../../math/AABB.hpp"
#include <algorithm>
#include <cmath>

namespace Engine::Renderer {

ShadowAtlas::ShadowAtlas(uint32_t atlasWidth, uint32_t atlasHeight)
    : m_atlasWidth(atlasWidth)
    , m_atlasHeight(atlasHeight)
{
    // Reserve space for allocations
    m_allocations.reserve(64);
    m_cascadedMaps.reserve(8);
}

ShadowAtlas::~ShadowAtlas() {
    shutdown();
}

bool ShadowAtlas::initialize() {
    if (m_initialized) {
        return true;
    }

    // Create atlas texture
    // In a real implementation, this would create the depth texture using RHI
    // Pseudo-code:
    //
    // TextureDesc desc;
    // desc.type = TextureType::Texture2D;
    // desc.format = TextureFormat::D32_SFLOAT; // or D24_UNORM_S8_UINT
    // desc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;
    // desc.width = m_atlasWidth;
    // desc.height = m_atlasHeight;
    // desc.mipLevels = 1;
    // desc.sampleCount = 1;
    // desc.debugName = "Shadow Atlas";
    // m_atlasTexture = RHI::CreateTexture(desc);

    m_initialized = true;
    return true;
}

void ShadowAtlas::shutdown() {
    clear();
    m_atlasTexture = nullptr;
    m_initialized = false;
}

// ============================================================================
// Shadow Map Allocation
// ============================================================================

ShadowAtlas::ShadowMapHandle ShadowAtlas::allocateCascadedShadowMap(ShadowResolution resolution) {
    uint32_t size = static_cast<uint32_t>(resolution);

    // Allocate 4 cascades (2x2 grid for efficient packing)
    // Each cascade is a square region
    ShadowMapHandle handle;
    handle.index = static_cast<uint32_t>(m_cascadedMaps.size());
    handle.generation = m_nextGeneration++;

    CascadedShadowMap cascadedMap;
    cascadedMap.handle = handle;

    // Allocate 4 regions for cascades
    for (uint32_t i = 0; i < 4; ++i) {
        auto regionOpt = findFreeSpace(size, size);
        if (!regionOpt.has_value()) {
            // Failed to allocate, free previously allocated cascades
            for (uint32_t j = 0; j < i; ++j) {
                // Mark as free
                for (auto& alloc : m_allocations) {
                    if (alloc.active && alloc.region.x == cascadedMap.cascades[j].x &&
                        alloc.region.y == cascadedMap.cascades[j].y) {
                        alloc.active = false;
                        m_usedPixels -= alloc.region.width * alloc.region.height;
                    }
                }
            }
            return ShadowMapHandle{}; // Invalid handle
        }

        cascadedMap.cascades[i] = regionOpt.value();
        cascadedMap.cascades[i].generation = handle.generation;
        cascadedMap.cascades[i].active = true;
        cascadedMap.cascades[i].updateUVTransform(m_atlasWidth, m_atlasHeight);

        // Create allocation entry
        AllocationEntry entry;
        entry.region = cascadedMap.cascades[i];
        entry.generation = handle.generation;
        entry.isCascaded = true;
        entry.cascadeIndex = i;
        entry.parentIndex = handle.index;
        entry.active = true;
        m_allocations.push_back(entry);

        m_usedPixels += size * size;
    }

    m_cascadedMaps.push_back(cascadedMap);
    return handle;
}

ShadowAtlas::ShadowMapHandle ShadowAtlas::allocateShadowMap(ShadowResolution resolution) {
    uint32_t size = static_cast<uint32_t>(resolution);
    return allocateRegion(size, size);
}

ShadowAtlas::ShadowMapHandle ShadowAtlas::allocateCubemapShadowMap(ShadowResolution resolution) {
    uint32_t size = static_cast<uint32_t>(resolution);

    // Allocate 6 faces (can be packed as 3x2 or 2x3 grid)
    ShadowMapHandle handle;
    handle.index = static_cast<uint32_t>(m_allocations.size());
    handle.generation = m_nextGeneration++;

    // Try to allocate 6 regions
    std::vector<ShadowRegion> faces;
    faces.reserve(6);

    for (uint32_t i = 0; i < 6; ++i) {
        auto regionOpt = findFreeSpace(size, size);
        if (!regionOpt.has_value()) {
            // Failed to allocate, free previously allocated faces
            for (const auto& face : faces) {
                for (auto& alloc : m_allocations) {
                    if (alloc.active && alloc.region.x == face.x && alloc.region.y == face.y) {
                        alloc.active = false;
                        m_usedPixels -= alloc.region.width * alloc.region.height;
                    }
                }
            }
            return ShadowMapHandle{}; // Invalid handle
        }

        ShadowRegion face = regionOpt.value();
        face.generation = handle.generation;
        face.active = true;
        face.updateUVTransform(m_atlasWidth, m_atlasHeight);
        faces.push_back(face);

        // Create allocation entry
        AllocationEntry entry;
        entry.region = face;
        entry.generation = handle.generation;
        entry.isCubemap = true;
        entry.cascadeIndex = i; // Repurpose for face index
        entry.parentIndex = handle.index;
        entry.active = true;
        m_allocations.push_back(entry);

        m_usedPixels += size * size;
    }

    // Store first face as the main allocation
    if (!m_allocations.empty() && handle.index < m_allocations.size()) {
        m_allocations[handle.index].region = faces[0];
    }

    return handle;
}

void ShadowAtlas::freeShadowMap(ShadowMapHandle handle) {
    if (!handle.isValid()) {
        return;
    }

    // Check if it's a cascaded shadow map
    if (handle.index < m_cascadedMaps.size() &&
        m_cascadedMaps[handle.index].handle == handle) {
        // Free all cascade allocations
        for (auto& alloc : m_allocations) {
            if (alloc.active && alloc.isCascaded && alloc.parentIndex == handle.index) {
                m_usedPixels -= alloc.region.width * alloc.region.height;
                alloc.active = false;
            }
        }
        m_cascadedMaps[handle.index].handle.invalidate();
        return;
    }

    // Check if it's a regular allocation
    if (handle.index < m_allocations.size() &&
        m_allocations[handle.index].generation == handle.generation &&
        m_allocations[handle.index].active) {

        // If it's a cubemap, free all faces
        if (m_allocations[handle.index].isCubemap) {
            for (auto& alloc : m_allocations) {
                if (alloc.active && alloc.isCubemap && alloc.parentIndex == handle.index) {
                    m_usedPixels -= alloc.region.width * alloc.region.height;
                    alloc.active = false;
                }
            }
        } else {
            m_usedPixels -= m_allocations[handle.index].region.width *
                           m_allocations[handle.index].region.height;
            m_allocations[handle.index].active = false;
        }
    }
}

void ShadowAtlas::clear() {
    m_allocations.clear();
    m_cascadedMaps.clear();
    m_shelves.clear();
    m_usedPixels = 0;
}

// ============================================================================
// Shadow Map Queries
// ============================================================================

const ShadowAtlas::ShadowRegion* ShadowAtlas::getShadowRegion(ShadowMapHandle handle) const {
    if (!handle.isValid() || handle.index >= m_allocations.size()) {
        return nullptr;
    }

    const auto& entry = m_allocations[handle.index];
    if (entry.active && entry.generation == handle.generation) {
        return &entry.region;
    }

    return nullptr;
}

ShadowAtlas::ShadowRegion* ShadowAtlas::getShadowRegion(ShadowMapHandle handle) {
    return const_cast<ShadowRegion*>(
        const_cast<const ShadowAtlas*>(this)->getShadowRegion(handle)
    );
}

const ShadowAtlas::CascadedShadowMap* ShadowAtlas::getCascadedShadowMap(ShadowMapHandle handle) const {
    if (!handle.isValid() || handle.index >= m_cascadedMaps.size()) {
        return nullptr;
    }

    const auto& cascaded = m_cascadedMaps[handle.index];
    if (cascaded.handle == handle) {
        return &cascaded;
    }

    return nullptr;
}

ShadowAtlas::CascadedShadowMap* ShadowAtlas::getCascadedShadowMap(ShadowMapHandle handle) {
    return const_cast<CascadedShadowMap*>(
        const_cast<const ShadowAtlas*>(this)->getCascadedShadowMap(handle)
    );
}

vec4 ShadowAtlas::getUVTransform(ShadowMapHandle handle) const {
    const ShadowRegion* region = getShadowRegion(handle);
    if (region) {
        return region->uvTransform;
    }
    return vec4(0.0f, 0.0f, 1.0f, 1.0f);
}

float ShadowAtlas::getUsedSpace() const {
    uint32_t totalPixels = m_atlasWidth * m_atlasHeight;
    return totalPixels > 0 ? static_cast<float>(m_usedPixels) / static_cast<float>(totalPixels) : 0.0f;
}

bool ShadowAtlas::hasSpace(ShadowResolution resolution) {
    uint32_t size = static_cast<uint32_t>(resolution);
    return findFreeSpace(size, size).has_value();
}

// ============================================================================
// Shadow Rendering
// ============================================================================

void ShadowAtlas::updateCascadedShadowMatrices(
    ShadowMapHandle handle,
    const DirectionalLight& light,
    const mat4& viewMatrix,
    const mat4& projectionMatrix)
{
    CascadedShadowMap* cascaded = getCascadedShadowMap(handle);
    if (!cascaded) {
        return;
    }

    // Compute cascade splits
    // Extract near/far from projection matrix (simplified)
    float nearPlane = 0.1f;  // Would extract from projection matrix
    float farPlane = 1000.0f;

    computeCascadeSplits(nearPlane, farPlane, light.cascadeSplitLambda, cascaded->cascadeSplits);

    // For each cascade, compute light space matrix
    for (uint32_t i = 0; i < 4; ++i) {
        float cascadeNear = (i == 0) ? nearPlane : cascaded->cascadeSplits[i - 1];
        float cascadeFar = cascaded->cascadeSplits[i];

        // Build frustum for this cascade
        // Compute scene bounds visible in this cascade
        AABB sceneBounds; // Would compute from frustum corners

        // Build orthographic shadow matrix
        cascaded->cascadeMatrices[i] = buildOrthographicShadowMatrix(
            light.direction,
            sceneBounds
        );
    }
}

mat4 ShadowAtlas::updateSpotLightShadowMatrix(ShadowRegion& region, const SpotLight& light) {
    // Build perspective projection for spot light shadow
    float fov = light.outerAngle * 2.0f;
    float nearPlane = 0.1f;
    float farPlane = light.radius;

    return buildPerspectiveShadowMatrix(
        light.position,
        light.direction,
        fov,
        nearPlane,
        farPlane
    );
}

std::array<mat4, 6> ShadowAtlas::updatePointLightShadowMatrices(
    ShadowMapHandle handle,
    const PointLight& light)
{
    std::array<mat4, 6> matrices;

    // Cubemap face directions
    const vec3 directions[6] = {
        vec3(1.0f, 0.0f, 0.0f),   // +X
        vec3(-1.0f, 0.0f, 0.0f),  // -X
        vec3(0.0f, 1.0f, 0.0f),   // +Y
        vec3(0.0f, -1.0f, 0.0f),  // -Y
        vec3(0.0f, 0.0f, 1.0f),   // +Z
        vec3(0.0f, 0.0f, -1.0f)   // -Z
    };

    const vec3 ups[6] = {
        vec3(0.0f, -1.0f, 0.0f),  // +X
        vec3(0.0f, -1.0f, 0.0f),  // -X
        vec3(0.0f, 0.0f, 1.0f),   // +Y
        vec3(0.0f, 0.0f, -1.0f),  // -Y
        vec3(0.0f, -1.0f, 0.0f),  // +Z
        vec3(0.0f, -1.0f, 0.0f)   // -Z
    };

    float nearPlane = 0.1f;
    float farPlane = light.radius;

    for (uint32_t i = 0; i < 6; ++i) {
        matrices[i] = buildPerspectiveShadowMatrix(
            light.position,
            directions[i],
            Math::PI / 2.0f, // 90 degrees FOV for cubemap
            nearPlane,
            farPlane
        );
    }

    return matrices;
}

void ShadowAtlas::clearRegion(const ShadowRegion& region) {
    // In a real implementation, this would clear the depth texture region
    // Pseudo-code:
    // commandBuffer->ClearDepthStencil(m_atlasTexture, region.x, region.y, region.width, region.height, 1.0f, 0);
}

void ShadowAtlas::clearUnusedRegions() {
    // Clear all inactive regions
    for (const auto& alloc : m_allocations) {
        if (!alloc.active) {
            clearRegion(alloc.region);
        }
    }
}

// ============================================================================
// Private Methods
// ============================================================================

std::optional<ShadowAtlas::ShadowRegion> ShadowAtlas::findFreeSpace(uint32_t width, uint32_t height) {
    // Simple shelf packing algorithm
    // Try to find a shelf that fits this region

    for (auto& shelf : m_shelves) {
        // Check if this region fits in the shelf
        if (shelf.height >= height && shelf.usedWidth + width <= m_atlasWidth) {
            ShadowRegion region;
            region.x = shelf.usedWidth;
            region.y = shelf.y;
            region.width = width;
            region.height = height;
            region.active = true;

            shelf.usedWidth += width;
            return region;
        }
    }

    // Need to create a new shelf
    uint32_t shelfY = 0;
    if (!m_shelves.empty()) {
        const auto& lastShelf = m_shelves.back();
        shelfY = lastShelf.y + lastShelf.height;
    }

    // Check if we have vertical space
    if (shelfY + height > m_atlasHeight) {
        return std::nullopt; // No space
    }

    // Create new shelf
    Shelf newShelf;
    newShelf.y = shelfY;
    newShelf.height = height;
    newShelf.usedWidth = width;
    m_shelves.push_back(newShelf);

    ShadowRegion region;
    region.x = 0;
    region.y = shelfY;
    region.width = width;
    region.height = height;
    region.active = true;

    return region;
}

ShadowAtlas::ShadowMapHandle ShadowAtlas::allocateRegion(uint32_t width, uint32_t height) {
    auto regionOpt = findFreeSpace(width, height);
    if (!regionOpt.has_value()) {
        return ShadowMapHandle{}; // Invalid handle
    }

    ShadowMapHandle handle;
    handle.index = static_cast<uint32_t>(m_allocations.size());
    handle.generation = m_nextGeneration++;

    AllocationEntry entry;
    entry.region = regionOpt.value();
    entry.region.generation = handle.generation;
    entry.region.updateUVTransform(m_atlasWidth, m_atlasHeight);
    entry.generation = handle.generation;
    entry.active = true;

    m_allocations.push_back(entry);
    m_usedPixels += width * height;

    return handle;
}

void ShadowAtlas::freeRegion(uint32_t index) {
    if (index >= m_allocations.size()) {
        return;
    }

    auto& entry = m_allocations[index];
    if (entry.active) {
        m_usedPixels -= entry.region.width * entry.region.height;
        entry.active = false;
    }
}

void ShadowAtlas::computeCascadeSplits(float nearPlane, float farPlane, float lambda, float splits[4]) {
    // Hybrid logarithmic/linear split scheme
    // lambda = 0: pure linear
    // lambda = 1: pure logarithmic

    for (uint32_t i = 0; i < 4; ++i) {
        float p = static_cast<float>(i + 1) / 4.0f;

        // Linear split
        float linearSplit = nearPlane + (farPlane - nearPlane) * p;

        // Logarithmic split
        float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);

        // Blend
        splits[i] = lambda * logSplit + (1.0f - lambda) * linearSplit;
    }
}

mat4 ShadowAtlas::buildOrthographicShadowMatrix(
    const vec3& lightDirection,
    const AABB& sceneBounds)
{
    // Build view matrix looking down the light direction
    vec3 center = sceneBounds.center();
    vec3 up = std::abs(lightDirection.dot(vec3::up())) > 0.99f ? vec3::right() : vec3::up();
    mat4 view = mat4::lookAt(center - lightDirection * 100.0f, center, up);

    // Transform scene bounds to light space
    AABB lightSpaceBounds = sceneBounds.transformed(view);

    // Build orthographic projection
    vec3 boundsSize = lightSpaceBounds.size();
    mat4 proj = mat4::ortho(
        lightSpaceBounds.min.x, lightSpaceBounds.max.x,
        lightSpaceBounds.min.y, lightSpaceBounds.max.y,
        -lightSpaceBounds.max.z - 100.0f, -lightSpaceBounds.min.z
    );

    return proj * view;
}

mat4 ShadowAtlas::buildPerspectiveShadowMatrix(
    const vec3& position,
    const vec3& direction,
    float fov,
    float nearPlane,
    float farPlane)
{
    // Build view matrix
    vec3 up = std::abs(direction.dot(vec3::up())) > 0.99f ? vec3::right() : vec3::up();
    mat4 view = mat4::lookAt(position, position + direction, up);

    // Build perspective projection
    mat4 proj = mat4::perspective(fov, 1.0f, nearPlane, farPlane);

    return proj * view;
}

} // namespace Engine::Renderer

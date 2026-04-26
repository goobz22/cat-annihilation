#include "ShadowAtlas.hpp"
#include "../../math/Math.hpp"
#include "../../math/AABB.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Engine::Renderer {

ShadowAtlas::ShadowAtlas(uint32_t atlasWidth, uint32_t atlasHeight)
    : m_atlasWidth(atlasWidth)
    , m_atlasHeight(atlasHeight)
    , m_packer(atlasWidth, atlasHeight)
{
    // Reserve space for allocations
    m_allocations.reserve(64);
    m_cascadedMaps.reserve(8);
}

ShadowAtlas::~ShadowAtlas() {
    shutdown();
}

bool ShadowAtlas::initialize(CatEngine::RHI::IRHIDevice* device, uint32_t atlasSize) {
    if (m_initialized) {
        return true;
    }

    if (!device) {
        std::cout << "[ShadowAtlas] initialize() failed: null RHI device\n";
        return false;
    }

    // Override atlas dimensions with the caller-provided square size so that
    // callers that construct the atlas with default dimensions can still size
    // the GPU texture at initialize time.
    m_device = device;
    m_atlasWidth = atlasSize;
    m_atlasHeight = atlasSize;
    // Resize resets the packer's free-rect list to a single atlas-wide
    // free rect, so any allocations made before initialize() (there
    // shouldn't be any in normal use, but be defensive) are wiped here.
    m_packer.resize(atlasSize, atlasSize);

    // Create the depth atlas texture. It needs DepthStencil (rendered into),
    // Sampled (read by lighting shaders), and TransferDst (so it can be cleared
    // outside of a render pass if the backend promotes clears to transfers).
    CatEngine::RHI::TextureDesc desc;
    desc.type = CatEngine::RHI::TextureType::Texture2D;
    desc.format = CatEngine::RHI::TextureFormat::D32_SFLOAT;
    desc.usage = CatEngine::RHI::TextureUsage::DepthStencil
               | CatEngine::RHI::TextureUsage::Sampled
               | CatEngine::RHI::TextureUsage::TransferDst;
    desc.width = m_atlasWidth;
    desc.height = m_atlasHeight;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.debugName = "ShadowAtlas";

    m_atlasTexture = m_device->CreateTexture(desc);
    if (!m_atlasTexture) {
        std::cout << "[ShadowAtlas] Failed to create atlas texture\n";
        m_device = nullptr;
        return false;
    }

    // Matching whole-image view; format Undefined tells the RHI to inherit the
    // texture's format. mipLevelCount/arrayLayerCount of 0 => all remaining.
    m_atlasTextureView = m_device->CreateTextureView(
        m_atlasTexture,
        CatEngine::RHI::TextureFormat::Undefined,
        0, 0, 0, 0
    );
    if (!m_atlasTextureView) {
        std::cout << "[ShadowAtlas] Failed to create atlas texture view\n";
        m_device->DestroyTexture(m_atlasTexture);
        m_atlasTexture = nullptr;
        m_device = nullptr;
        return false;
    }

    std::cout << "[ShadowAtlas] Created atlas " << m_atlasWidth << "x" << m_atlasHeight << "\n";

    m_initialized = true;
    return true;
}

void ShadowAtlas::shutdown() {
    clear();

    if (m_device) {
        if (m_atlasTextureView) {
            m_device->DestroyTextureView(m_atlasTextureView);
        }
        if (m_atlasTexture) {
            m_device->DestroyTexture(m_atlasTexture);
        }
    }

    m_atlasTextureView = nullptr;
    m_atlasTexture = nullptr;
    m_device = nullptr;
    m_initialized = false;
}

// ============================================================================
// Shadow Map Allocation
// ============================================================================

ShadowAtlas::ShadowMapHandle ShadowAtlas::allocateCascadedShadowMap(ShadowResolution resolution) {
    uint32_t size = static_cast<uint32_t>(resolution);

    // Allocate 4 cascades. The packer picks the best-fit free rect for
    // each one independently; with a Guillotine packer they usually
    // end up in a 2x2 grid when the atlas is empty, but on a partially
    // full atlas they may scatter across surviving free slots — which
    // is exactly what cascades + spot shadows sharing one atlas needs.
    ShadowMapHandle handle;
    handle.index = static_cast<uint32_t>(m_cascadedMaps.size());
    handle.generation = m_nextGeneration++;

    CascadedShadowMap cascadedMap;
    cascadedMap.handle = handle;

    // Track placements so we can roll them back cleanly on partial
    // failure. The old code matched by (x, y) against m_allocations,
    // which was O(N²) and fragile; the packer now owns the pixels so a
    // direct free() call against the same PackedRect is the right
    // rollback primitive.
    std::array<PackedRect, 4> placedRects{};
    uint32_t placedCount = 0;

    for (uint32_t i = 0; i < 4; ++i) {
        auto packedOpt = m_packer.insert(size, size);
        if (!packedOpt.has_value()) {
            // Roll back every cascade slot we successfully placed
            // before the failure, both in the packer (pixels) and in
            // m_allocations (active bit). Entries aren't erased from
            // m_allocations because other live handles still index
            // into that vector; we leave them as orphan
            // (active == false) entries the same way freeShadowMap()
            // would.
            for (uint32_t j = 0; j < placedCount; ++j) {
                m_packer.free(placedRects[j]);
            }
            const size_t startIdx = m_allocations.size() - placedCount;
            for (uint32_t j = 0; j < placedCount; ++j) {
                m_allocations[startIdx + j].active = false;
            }
            return ShadowMapHandle{}; // Invalid handle
        }

        const PackedRect rect = packedOpt.value();
        placedRects[placedCount++] = rect;

        cascadedMap.cascades[i].x = rect.x;
        cascadedMap.cascades[i].y = rect.y;
        cascadedMap.cascades[i].width = rect.w;
        cascadedMap.cascades[i].height = rect.h;
        cascadedMap.cascades[i].generation = handle.generation;
        cascadedMap.cascades[i].active = true;
        cascadedMap.cascades[i].updateUVTransform(m_atlasWidth, m_atlasHeight);

        AllocationEntry entry;
        entry.region = cascadedMap.cascades[i];
        entry.generation = handle.generation;
        entry.isCascaded = true;
        entry.cascadeIndex = i;
        entry.parentIndex = handle.index;
        entry.active = true;
        m_allocations.push_back(entry);
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

    // Allocate 6 faces. The Guillotine packer will greedily find the
    // best fit for each face independently; unlike the previous shelf
    // allocator, there is no strong guarantee the 6 faces end up
    // spatially adjacent. Shadow sampling looks each face up via the
    // per-face UV transform, so scattered placement is fine.
    ShadowMapHandle handle;
    handle.index = static_cast<uint32_t>(m_allocations.size());
    handle.generation = m_nextGeneration++;

    std::array<PackedRect, 6> placedRects{};
    std::array<ShadowRegion, 6> faces{};
    uint32_t placedCount = 0;

    for (uint32_t i = 0; i < 6; ++i) {
        auto packedOpt = m_packer.insert(size, size);
        if (!packedOpt.has_value()) {
            // Roll back placed faces — packer frees pixels, m_allocations
            // entries are left as inactive orphans so later handles keep
            // their indices (same rationale as allocateCascadedShadowMap).
            for (uint32_t j = 0; j < placedCount; ++j) {
                m_packer.free(placedRects[j]);
            }
            const size_t startIdx = m_allocations.size() - placedCount;
            for (uint32_t j = 0; j < placedCount; ++j) {
                m_allocations[startIdx + j].active = false;
            }
            return ShadowMapHandle{}; // Invalid handle
        }

        const PackedRect rect = packedOpt.value();
        placedRects[placedCount] = rect;

        ShadowRegion face{};
        face.x = rect.x;
        face.y = rect.y;
        face.width = rect.w;
        face.height = rect.h;
        face.generation = handle.generation;
        face.active = true;
        face.updateUVTransform(m_atlasWidth, m_atlasHeight);
        faces[placedCount] = face;
        placedCount++;

        AllocationEntry entry;
        entry.region = face;
        entry.generation = handle.generation;
        entry.isCubemap = true;
        entry.cascadeIndex = i; // Repurpose for face index
        entry.parentIndex = handle.index;
        entry.active = true;
        m_allocations.push_back(entry);
    }

    // Point the "main" allocation entry at the first face. This mirrors
    // the pre-refactor behaviour so callers that look up the cubemap via
    // getShadowRegion(handle) still see face 0 as the primary rect.
    if (!m_allocations.empty() && handle.index < m_allocations.size()) {
        m_allocations[handle.index].region = faces[0];
    }

    return handle;
}

void ShadowAtlas::freeShadowMap(ShadowMapHandle handle) {
    if (!handle.isValid()) {
        return;
    }

    // Cascaded shadow map path: free every cascade slot owned by this
    // handle in BOTH the allocation table (flip active) and the packer
    // (return pixels so a future allocation can land on the same tile).
    // The old code only flipped the bit; pixels were effectively leaked
    // until the whole atlas was cleared.
    if (handle.index < m_cascadedMaps.size() &&
        m_cascadedMaps[handle.index].handle == handle) {
        for (auto& alloc : m_allocations) {
            if (alloc.active && alloc.isCascaded && alloc.parentIndex == handle.index) {
                m_packer.free(PackedRect{
                    alloc.region.x, alloc.region.y,
                    alloc.region.width, alloc.region.height
                });
                alloc.active = false;
            }
        }
        m_cascadedMaps[handle.index].handle.invalidate();
        return;
    }

    // Regular or cubemap allocation path.
    if (handle.index < m_allocations.size() &&
        m_allocations[handle.index].generation == handle.generation &&
        m_allocations[handle.index].active) {

        if (m_allocations[handle.index].isCubemap) {
            // Cubemap: free all 6 face allocations grouped by parentIndex.
            for (auto& alloc : m_allocations) {
                if (alloc.active && alloc.isCubemap && alloc.parentIndex == handle.index) {
                    m_packer.free(PackedRect{
                        alloc.region.x, alloc.region.y,
                        alloc.region.width, alloc.region.height
                    });
                    alloc.active = false;
                }
            }
        } else {
            // Single tile.
            auto& entry = m_allocations[handle.index];
            m_packer.free(PackedRect{
                entry.region.x, entry.region.y,
                entry.region.width, entry.region.height
            });
            entry.active = false;
        }
    }
}

void ShadowAtlas::clear() {
    m_allocations.clear();
    m_cascadedMaps.clear();
    // Reset the packer so its free-rect list once again covers the full
    // atlas extent. This implicitly zeroes usedPixels() on the packer.
    m_packer.reset();
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
    // Density is tracked authoritatively by the packer — it increments
    // on insert() and decrements on free(), so it stays consistent with
    // the actual free-rect list even if m_allocations diverges.
    return m_packer.density();
}

bool ShadowAtlas::hasSpace(ShadowResolution resolution) const {
    // Const, non-mutating probe. The previous shelf-packer version of
    // this function called findFreeSpace(), which could silently grow a
    // new shelf — turning a harmless "do I have room?" query into a
    // subtle state change that inflated perceived occupancy.
    uint32_t size = static_cast<uint32_t>(resolution);
    return m_packer.canFit(size, size);
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

    // Extract near/far from the engine's standard perspective projection.
    //
    // Convention: CatEngine's Matrix.hpp::perspective() builds an OpenGL-style
    // RH projection — clip-space depth in [-1, +1], with:
    //     A = P[2][2] = -(f + n) / (f - n)
    //     B = P[3][2] = -2fn     / (f - n)
    // Solving for n and f in terms of A and B:
    //     n = B / (A - 1)
    //     f = B / (A + 1)
    // A previous version of this code used Vulkan depth-[0,1] formulas
    // (n = B / A, f = B / (A+1)), which silently produced wrong cascade
    // splits whenever the caller passed the engine-built projection.
    //
    // Fall back to sane scene defaults if the projection looks degenerate
    // (e.g. an ortho matrix or an infinite-far perspective was passed, in
    // which case the (A-1) / (A+1) denominators aren't meaningful).
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    {
        const float A = projectionMatrix[2][2];
        const float B = projectionMatrix[3][2];
        const float denomNear = A - 1.0f;
        const float denomFar  = A + 1.0f;
        const float kEps = 1e-6f;
        if (A < 0.0f && B < 0.0f &&
            std::abs(denomNear) > kEps && std::abs(denomFar) > kEps) {
            const float extractedNear = B / denomNear;
            const float extractedFar  = B / denomFar;
            if (extractedNear > 0.0f && extractedFar > extractedNear) {
                nearPlane = extractedNear;
                farPlane = extractedFar;
            }
        }
    }

    computeCascadeSplits(nearPlane, farPlane, light.cascadeSplitLambda, cascaded->cascadeSplits);

    // Inverse of view*proj transforms NDC-cube corners back into world space.
    // This is the canonical way to get the camera frustum corners for CSM.
    mat4 invViewProj = (projectionMatrix * viewMatrix).inverse();

    const vec3 ndcCorners[8] = {
        vec3(-1.0f, -1.0f, 0.0f), vec3(1.0f, -1.0f, 0.0f),
        vec3(-1.0f,  1.0f, 0.0f), vec3(1.0f,  1.0f, 0.0f),
        vec3(-1.0f, -1.0f, 1.0f), vec3(1.0f, -1.0f, 1.0f),
        vec3(-1.0f,  1.0f, 1.0f), vec3(1.0f,  1.0f, 1.0f)
    };

    vec3 worldCorners[8];
    for (uint32_t i = 0; i < 8; ++i) {
        worldCorners[i] = invViewProj.transformPoint(ndcCorners[i]);
    }

    // For each cascade, build an AABB around the slice of the frustum between
    // cascadeNear and cascadeFar along the camera's view direction, then fit
    // an orthographic light matrix to those bounds.
    float range = farPlane - nearPlane;
    for (uint32_t i = 0; i < 4; ++i) {
        float cascadeNear = (i == 0) ? nearPlane : cascaded->cascadeSplits[i - 1];
        float cascadeFar = cascaded->cascadeSplits[i];

        // Normalised [0,1] positions of this cascade's near/far inside the
        // camera frustum; used to linearly interpolate between the world-space
        // near-face and far-face corners of the full frustum.
        float tNear = (cascadeNear - nearPlane) / range;
        float tFar = (cascadeFar - nearPlane) / range;

        AABB sceneBounds;
        for (uint32_t c = 0; c < 4; ++c) {
            const vec3& nearCorner = worldCorners[c];        // NDC z=0 corners
            const vec3& farCorner = worldCorners[c + 4];     // NDC z=1 corners
            vec3 dir = farCorner - nearCorner;
            sceneBounds.expand(nearCorner + dir * tNear);
            sceneBounds.expand(nearCorner + dir * tFar);
        }

        cascaded->cascadeMatrices[i] = buildOrthographicShadowMatrix(
            light.direction,
            sceneBounds
        );
    }
}

mat4 ShadowAtlas::updateSpotLightShadowMatrix(const SpotLight& light) {
    // Build a world -> light clip-space matrix for the spot light. The
    // returned matrix does NOT encode any atlas tile offset — atlas tile
    // remapping (shadow UV -> sub-rect inside the atlas texture) is applied
    // downstream at sample time using the ShadowRegion's uv offset/scale.
    // Mixing tile-offset math into this matrix would double-apply that
    // transform and break filtering, so the region parameter was removed.
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

void ShadowAtlas::clearRegion(const ShadowRegion& region, CatEngine::RHI::IRHICommandBuffer* cmd) {
    if (!cmd) {
        return;
    }

    CatEngine::RHI::ClearDepthStencilValue clearValue;
    clearValue.depth = 1.0f;
    clearValue.stencil = 0;

    CatEngine::RHI::Rect2D rect;
    rect.x = static_cast<int32_t>(region.x);
    rect.y = static_cast<int32_t>(region.y);
    rect.width = region.width;
    rect.height = region.height;

    cmd->ClearDepthStencilAttachment(clearValue, rect);
}

void ShadowAtlas::clearUnusedRegions(CatEngine::RHI::IRHICommandBuffer* cmd) {
    if (!cmd) {
        return;
    }

    // Clear all inactive regions so stale shadow data cannot leak into the next
    // frame when their atlas slots get reallocated to new lights.
    for (const auto& alloc : m_allocations) {
        if (!alloc.active) {
            clearRegion(alloc.region, cmd);
        }
    }
}

// ============================================================================
// Private Methods
// ============================================================================

std::optional<ShadowAtlas::ShadowRegion> ShadowAtlas::findFreeSpace(uint32_t width, uint32_t height) {
    // Delegate to the Guillotine packer. The packer owns the spatial
    // state; this helper is a thin translation layer from PackedRect
    // to ShadowRegion so the rest of ShadowAtlas keeps its existing
    // vocabulary. active=true is set here because every call site
    // immediately records the region as live.
    auto packedOpt = m_packer.insert(width, height);
    if (!packedOpt.has_value()) {
        return std::nullopt;
    }
    const PackedRect rect = packedOpt.value();
    ShadowRegion region{};
    region.x = rect.x;
    region.y = rect.y;
    region.width = rect.w;
    region.height = rect.h;
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
    return handle;
}

void ShadowAtlas::freeRegion(uint32_t index) {
    if (index >= m_allocations.size()) {
        return;
    }

    auto& entry = m_allocations[index];
    if (entry.active) {
        // Return the tile to the packer so future allocations can
        // reuse it. This is the key behavioural upgrade over the
        // shelf-packer era, where freed tiles were never reclaimed.
        m_packer.free(PackedRect{
            entry.region.x, entry.region.y,
            entry.region.width, entry.region.height
        });
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

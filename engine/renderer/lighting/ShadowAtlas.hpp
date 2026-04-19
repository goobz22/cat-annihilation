#ifndef ENGINE_RENDERER_LIGHTING_SHADOW_ATLAS_HPP
#define ENGINE_RENDERER_LIGHTING_SHADOW_ATLAS_HPP

#include "Light.hpp"
#include "LightManager.hpp"
#include "../../math/Vector.hpp"
#include "../../math/Matrix.hpp"
#include "../../rhi/RHI.hpp"
#include "../../rhi/RHITexture.hpp"
#include "../../rhi/RHICommandBuffer.hpp"
#include <vector>
#include <memory>
#include <optional>

namespace Engine::Renderer {

/**
 * Shadow Atlas Manager
 *
 * Manages a large texture atlas for shadow maps
 * Allocates regions for different lights with varying resolutions
 * Supports:
 * - Cascaded shadow maps (directional lights - 4 cascades)
 * - Single shadow maps (spot lights)
 * - Point light shadows (simplified to single direction or 6 faces)
 *
 * Default atlas size: 4096x4096
 */
class ShadowAtlas {
public:
    /**
     * Shadow map allocation handle
     */
    struct ShadowMapHandle {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;

        bool isValid() const { return index != UINT32_MAX; }
        void invalidate() { index = UINT32_MAX; }

        bool operator==(const ShadowMapHandle& other) const {
            return index == other.index && generation == other.generation;
        }
        bool operator!=(const ShadowMapHandle& other) const {
            return !(*this == other);
        }
    };

    /**
     * Shadow map region in the atlas
     */
    struct ShadowRegion {
        uint32_t x = 0;              // X offset in atlas
        uint32_t y = 0;              // Y offset in atlas
        uint32_t width = 0;          // Width in pixels
        uint32_t height = 0;         // Height in pixels
        uint32_t generation = 0;     // For validation
        bool active = false;         // Is this region allocated?

        // UV transform for sampling (from [0,1] to atlas coordinates)
        vec4 uvTransform;            // xy = offset, zw = scale

        /**
         * Update UV transform based on atlas size
         */
        void updateUVTransform(uint32_t atlasWidth, uint32_t atlasHeight) {
            float u = static_cast<float>(x) / static_cast<float>(atlasWidth);
            float v = static_cast<float>(y) / static_cast<float>(atlasHeight);
            float scaleU = static_cast<float>(width) / static_cast<float>(atlasWidth);
            float scaleV = static_cast<float>(height) / static_cast<float>(atlasHeight);
            uvTransform = vec4(u, v, scaleU, scaleV);
        }
    };

    /**
     * Cascade shadow map allocation (for directional lights)
     * Contains 4 cascade regions
     */
    struct CascadedShadowMap {
        ShadowMapHandle handle;
        std::array<ShadowRegion, 4> cascades;
        mat4 cascadeMatrices[4];     // Light space matrices for each cascade
        float cascadeSplits[4];      // Split distances for cascades
    };

    /**
     * Standard resolutions for shadow maps
     */
    enum class ShadowResolution : uint32_t {
        Low = 512,
        Medium = 1024,
        High = 2048,
        Ultra = 4096
    };

    /**
     * Constructor
     * @param atlasWidth Atlas texture width (default: 4096)
     * @param atlasHeight Atlas texture height (default: 4096)
     */
    ShadowAtlas(uint32_t atlasWidth = 4096, uint32_t atlasHeight = 4096);

    /**
     * Destructor
     */
    ~ShadowAtlas();

    // Disable copy, allow move
    ShadowAtlas(const ShadowAtlas&) = delete;
    ShadowAtlas& operator=(const ShadowAtlas&) = delete;
    ShadowAtlas(ShadowAtlas&&) = default;
    ShadowAtlas& operator=(ShadowAtlas&&) = default;

    /**
     * Initialize the shadow atlas
     * Creates the atlas depth texture and matching texture view via the RHI device.
     * @param device RHI device used to allocate GPU resources
     * @param atlasSize Square atlas dimensions in pixels (default: 4096)
     */
    bool initialize(CatEngine::RHI::IRHIDevice* device, uint32_t atlasSize = 4096);

    /**
     * Shutdown and release resources
     */
    void shutdown();

    // ========================================================================
    // Shadow Map Allocation
    // ========================================================================

    /**
     * Allocate a cascaded shadow map for a directional light
     * @param resolution Resolution for each cascade
     * @return Handle to the cascaded shadow map, or invalid if allocation failed
     */
    ShadowMapHandle allocateCascadedShadowMap(ShadowResolution resolution = ShadowResolution::Medium);

    /**
     * Allocate a single shadow map (for spot light)
     * @param resolution Shadow map resolution
     * @return Handle to the shadow map, or invalid if allocation failed
     */
    ShadowMapHandle allocateShadowMap(ShadowResolution resolution = ShadowResolution::Medium);

    /**
     * Allocate a cubemap shadow map (for point light - 6 faces)
     * @param resolution Shadow map resolution per face
     * @return Handle to the shadow map, or invalid if allocation failed
     */
    ShadowMapHandle allocateCubemapShadowMap(ShadowResolution resolution = ShadowResolution::Low);

    /**
     * Free a shadow map allocation
     */
    void freeShadowMap(ShadowMapHandle handle);

    /**
     * Free all shadow map allocations
     */
    void clear();

    // ========================================================================
    // Shadow Map Queries
    // ========================================================================

    /**
     * Get shadow region for a handle
     */
    const ShadowRegion* getShadowRegion(ShadowMapHandle handle) const;
    ShadowRegion* getShadowRegion(ShadowMapHandle handle);

    /**
     * Get cascaded shadow map data
     */
    const CascadedShadowMap* getCascadedShadowMap(ShadowMapHandle handle) const;
    CascadedShadowMap* getCascadedShadowMap(ShadowMapHandle handle);

    /**
     * Get UV transform for a shadow map (for sampling in shaders)
     */
    vec4 getUVTransform(ShadowMapHandle handle) const;

    /**
     * Get atlas texture
     */
    CatEngine::RHI::IRHITexture* getAtlasTexture() const { return m_atlasTexture; }

    /**
     * Get atlas texture view (used for binding as sampled image / depth attachment)
     */
    CatEngine::RHI::IRHITextureView* getAtlasTextureView() const { return m_atlasTextureView; }

    /**
     * Get atlas dimensions
     */
    uint32_t getAtlasWidth() const { return m_atlasWidth; }
    uint32_t getAtlasHeight() const { return m_atlasHeight; }

    /**
     * Get used space in atlas (0.0 - 1.0)
     */
    float getUsedSpace() const;

    /**
     * Check if atlas has space for a given resolution
     * Note: This may modify internal tracking state
     */
    bool hasSpace(ShadowResolution resolution);

    // ========================================================================
    // Shadow Rendering
    // ========================================================================

    /**
     * Update cascade shadow map matrices for a directional light
     * @param handle Cascaded shadow map handle
     * @param light Directional light
     * @param viewMatrix Camera view matrix
     * @param projectionMatrix Camera projection matrix
     */
    void updateCascadedShadowMatrices(
        ShadowMapHandle handle,
        const DirectionalLight& light,
        const mat4& viewMatrix,
        const mat4& projectionMatrix
    );

    /**
     * Update shadow matrix for a spot light
     * @param region Shadow region
     * @param light Spot light
     * @return Light space matrix for shadow rendering
     */
    mat4 updateSpotLightShadowMatrix(ShadowRegion& region, const SpotLight& light);

    /**
     * Update shadow matrices for a point light (6 faces)
     * @param handle Shadow map handle (must be cubemap allocation)
     * @param light Point light
     * @return Array of 6 light space matrices (one per cubemap face)
     */
    std::array<mat4, 6> updatePointLightShadowMatrices(ShadowMapHandle handle, const PointLight& light);

    /**
     * Clear a shadow map region (set to max depth 1.0, stencil 0).
     * Must be recorded inside an active depth-stencil render pass on @p cmd.
     * @param region Atlas sub-rectangle to clear
     * @param cmd Command buffer currently recording a depth render pass
     */
    void clearRegion(const ShadowRegion& region, CatEngine::RHI::IRHICommandBuffer* cmd);

    /**
     * Clear unused regions in the atlas.
     * Must be recorded inside an active depth-stencil render pass on @p cmd.
     */
    void clearUnusedRegions(CatEngine::RHI::IRHICommandBuffer* cmd);

private:
    /**
     * Shadow map allocation entry
     */
    struct AllocationEntry {
        ShadowRegion region;
        uint32_t generation = 0;
        bool isCascaded = false;       // Is this a cascaded shadow map?
        bool isCubemap = false;        // Is this a cubemap (6 faces)?
        uint32_t cascadeIndex = 0;     // For cascaded maps, which cascade (0-3)
        uint32_t parentIndex = UINT32_MAX; // For cascades, index of parent allocation
        bool active = false;
    };

    /**
     * Find free space in atlas for given size
     * Uses simple shelf packing algorithm
     * @return Optional region if space found
     */
    std::optional<ShadowRegion> findFreeSpace(uint32_t width, uint32_t height);

    /**
     * Allocate region in atlas
     */
    ShadowMapHandle allocateRegion(uint32_t width, uint32_t height);

    /**
     * Mark region as free
     */
    void freeRegion(uint32_t index);

    /**
     * Compute cascade split distances using logarithmic/linear blend
     * @param lambda Blend factor (0 = linear, 1 = logarithmic)
     */
    void computeCascadeSplits(float nearPlane, float farPlane, float lambda, float splits[4]);

    /**
     * Build light space matrix for orthographic shadow projection
     */
    mat4 buildOrthographicShadowMatrix(
        const vec3& lightDirection,
        const AABB& sceneBounds
    );

    /**
     * Build light space matrix for perspective shadow projection (spot light)
     */
    mat4 buildPerspectiveShadowMatrix(
        const vec3& position,
        const vec3& direction,
        float fov,
        float nearPlane,
        float farPlane
    );

    // Atlas parameters
    uint32_t m_atlasWidth;
    uint32_t m_atlasHeight;

    // RHI resources (not owned by the atlas — created/destroyed via m_device)
    CatEngine::RHI::IRHIDevice* m_device = nullptr;
    CatEngine::RHI::IRHITexture* m_atlasTexture = nullptr;
    CatEngine::RHI::IRHITextureView* m_atlasTextureView = nullptr;

    // Allocations
    std::vector<AllocationEntry> m_allocations;
    std::vector<CascadedShadowMap> m_cascadedMaps;

    // Shelf packing data (simple allocator)
    struct Shelf {
        uint32_t y = 0;          // Y position of shelf
        uint32_t height = 0;     // Height of shelf
        uint32_t usedWidth = 0;  // Used width in shelf
    };
    std::vector<Shelf> m_shelves;

    // State
    bool m_initialized = false;
    uint32_t m_nextGeneration = 1;
    uint32_t m_usedPixels = 0;
};

} // namespace Engine::Renderer

#endif // ENGINE_RENDERER_LIGHTING_SHADOW_ATLAS_HPP

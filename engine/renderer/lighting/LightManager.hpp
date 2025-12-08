#ifndef ENGINE_RENDERER_LIGHTING_LIGHT_MANAGER_HPP
#define ENGINE_RENDERER_LIGHTING_LIGHT_MANAGER_HPP

#include "Light.hpp"
#include "../../math/Frustum.hpp"
#include "../../rhi/RHIBuffer.hpp"
#include <vector>
#include <memory>
#include <optional>

namespace Engine::Renderer {

/**
 * Light handle for efficient light management
 * Uses index-based handles to avoid pointer invalidation
 */
struct LightHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;  // For validation

    bool isValid() const { return index != UINT32_MAX; }
    void invalidate() { index = UINT32_MAX; }

    bool operator==(const LightHandle& other) const {
        return index == other.index && generation == other.generation;
    }

    bool operator!=(const LightHandle& other) const {
        return !(*this == other);
    }
};

/**
 * Light Manager
 * Manages all lights in the scene and provides efficient GPU upload
 *
 * Limits:
 * - 1 directional light
 * - 256 point lights
 * - 128 spot lights
 */
class LightManager {
public:
    // Maximum light counts
    static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 1;
    static constexpr uint32_t MAX_POINT_LIGHTS = 256;
    static constexpr uint32_t MAX_SPOT_LIGHTS = 128;
    static constexpr uint32_t MAX_TOTAL_LIGHTS = MAX_DIRECTIONAL_LIGHTS + MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS;

    /**
     * Constructor
     */
    LightManager();

    /**
     * Destructor
     */
    ~LightManager();

    // Disable copy, allow move
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;
    LightManager(LightManager&&) = default;
    LightManager& operator=(LightManager&&) = default;

    /**
     * Initialize the light manager with RHI device
     * Creates GPU buffers for light data
     */
    bool initialize(CatEngine::RHI::IRHIBuffer* lightBuffer);

    /**
     * Shutdown and release resources
     */
    void shutdown();

    // ========================================================================
    // Light Management
    // ========================================================================

    /**
     * Add a directional light
     * Only one directional light is supported at a time
     * Returns handle to the light or invalid handle if limit reached
     */
    LightHandle addDirectionalLight(const DirectionalLight& light);

    /**
     * Add a point light
     * Returns handle to the light or invalid handle if limit reached
     */
    LightHandle addPointLight(const PointLight& light);

    /**
     * Add a spot light
     * Returns handle to the light or invalid handle if limit reached
     */
    LightHandle addSpotLight(const SpotLight& light);

    /**
     * Remove a light by handle
     */
    void removeLight(LightHandle handle);

    /**
     * Update an existing directional light
     */
    void updateDirectionalLight(LightHandle handle, const DirectionalLight& light);

    /**
     * Update an existing point light
     */
    void updatePointLight(LightHandle handle, const PointLight& light);

    /**
     * Update an existing spot light
     */
    void updateSpotLight(LightHandle handle, const SpotLight& light);

    /**
     * Remove all lights
     */
    void clear();

    // ========================================================================
    // Light Queries
    // ========================================================================

    /**
     * Get directional light by handle
     */
    const DirectionalLight* getDirectionalLight(LightHandle handle) const;
    DirectionalLight* getDirectionalLight(LightHandle handle);

    /**
     * Get point light by handle
     */
    const PointLight* getPointLight(LightHandle handle) const;
    PointLight* getPointLight(LightHandle handle);

    /**
     * Get spot light by handle
     */
    const SpotLight* getSpotLight(LightHandle handle) const;
    SpotLight* getSpotLight(LightHandle handle);

    /**
     * Get active light counts
     */
    uint32_t getDirectionalLightCount() const { return m_directionalLightCount; }
    uint32_t getPointLightCount() const { return m_pointLightCount; }
    uint32_t getSpotLightCount() const { return m_spotLightCount; }
    uint32_t getTotalLightCount() const {
        return m_directionalLightCount + m_pointLightCount + m_spotLightCount;
    }

    /**
     * Check if lights have been modified since last upload
     */
    bool isDirty() const { return m_dirty; }

    /**
     * Mark lights as dirty (need GPU upload)
     */
    void markDirty() { m_dirty = true; }

    // ========================================================================
    // GPU Upload
    // ========================================================================

    /**
     * Upload light data to GPU buffer
     * Only uploads if dirty flag is set
     * Returns true if upload occurred
     */
    bool uploadToGPU();

    /**
     * Force upload to GPU regardless of dirty flag
     */
    void forceUploadToGPU();

    /**
     * Get the GPU light buffer
     */
    CatEngine::RHI::IRHIBuffer* getLightBuffer() const { return m_lightBuffer; }

    /**
     * Get the GPU light data array (for manual upload)
     */
    const std::vector<GPULight>& getGPULights() const { return m_gpuLights; }

    // ========================================================================
    // Optional: CPU-side Frustum Culling
    // ========================================================================

    /**
     * Perform coarse frustum culling on lights
     * Updates active light indices based on frustum visibility
     * This is optional and can be done on GPU as well
     */
    void cullLights(const Frustum& frustum);

    /**
     * Get visible light indices after culling
     */
    const std::vector<uint32_t>& getVisibleLightIndices() const { return m_visibleLightIndices; }

private:
    /**
     * Internal light entry with metadata
     */
    template<typename T>
    struct LightEntry {
        T light;
        uint32_t generation = 0;
        bool active = false;
    };

    /**
     * Rebuild GPU light array from CPU lights
     */
    void rebuildGPULights();

    /**
     * Validate light handle
     */
    template<typename T>
    bool validateHandle(const LightHandle& handle, const std::vector<LightEntry<T>>& entries) const;

    // Light storage
    std::vector<LightEntry<DirectionalLight>> m_directionalLights;
    std::vector<LightEntry<PointLight>> m_pointLights;
    std::vector<LightEntry<SpotLight>> m_spotLights;

    // Light counts
    uint32_t m_directionalLightCount = 0;
    uint32_t m_pointLightCount = 0;
    uint32_t m_spotLightCount = 0;

    // GPU data
    std::vector<GPULight> m_gpuLights;
    CatEngine::RHI::IRHIBuffer* m_lightBuffer = nullptr;

    // Culling data (optional)
    std::vector<uint32_t> m_visibleLightIndices;

    // State tracking
    bool m_dirty = false;
    bool m_initialized = false;

    // Generation counter for handle validation
    uint32_t m_nextGeneration = 0;
};

} // namespace Engine::Renderer

#endif // ENGINE_RENDERER_LIGHTING_LIGHT_MANAGER_HPP

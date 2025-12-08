#ifndef ENGINE_RENDERER_LIGHTING_CLUSTERED_LIGHTING_HPP
#define ENGINE_RENDERER_LIGHTING_CLUSTERED_LIGHTING_HPP

#include "Light.hpp"
#include "LightManager.hpp"
#include "../../math/Vector.hpp"
#include "../../math/Matrix.hpp"
#include "../../math/AABB.hpp"
#include "../../rhi/RHIBuffer.hpp"
#include "../../rhi/RHIShader.hpp"
#include "../../rhi/RHICommandBuffer.hpp"
#include "../../rhi/RHIPipeline.hpp"
#include <vector>
#include <memory>

namespace Engine::Renderer {

/**
 * Clustered Lighting System
 *
 * Divides the view frustum into a 3D grid of clusters (16x9x24 = 3456 clusters)
 * For each cluster, determines which lights affect it
 * Uses a compute shader to build light lists per cluster on GPU
 *
 * Grid dimensions: 16 (X) x 9 (Y) x 24 (Z)
 * Z-slicing uses logarithmic distribution for better near-plane precision
 */
class ClusteredLighting {
public:
    // Cluster grid dimensions
    static constexpr uint32_t CLUSTER_GRID_X = 16;
    static constexpr uint32_t CLUSTER_GRID_Y = 9;
    static constexpr uint32_t CLUSTER_GRID_Z = 24;
    static constexpr uint32_t TOTAL_CLUSTERS = CLUSTER_GRID_X * CLUSTER_GRID_Y * CLUSTER_GRID_Z;

    // Maximum lights per cluster (affects memory usage)
    static constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 256;

    /**
     * Cluster data structure (CPU-side)
     * Represents an AABB in view space
     */
    struct Cluster {
        AABB bounds;             // Cluster bounds in view space
        vec3 minPoint;           // Minimum point in screen space
        vec3 maxPoint;           // Maximum point in screen space
        uint32_t lightCount;     // Number of lights affecting this cluster
        uint32_t lightOffset;    // Offset into global light index list
    };

    /**
     * GPU cluster data structure (aligned for GPU upload)
     * Stored in a buffer for shader access
     */
    struct alignas(16) GPUCluster {
        vec4 minBounds;          // xyz = min point, w = light count
        vec4 maxBounds;          // xyz = max point, w = light offset
    };

    /**
     * Clustered lighting parameters (uniform buffer)
     */
    struct alignas(16) ClusterParams {
        vec4 screenDimensions;   // xy = screen width/height, zw = inverse
        vec4 clusterParams;      // x = near, y = far, z = log(far/near), w = cluster scale
        vec4 gridDimensions;     // xyz = grid dimensions (16, 9, 24), w = total clusters
        vec4 invGridDimensions;  // xyz = 1.0 / grid dimensions, w = unused
    };

    /**
     * Constructor
     */
    ClusteredLighting();

    /**
     * Destructor
     */
    ~ClusteredLighting();

    // Disable copy, allow move
    ClusteredLighting(const ClusteredLighting&) = delete;
    ClusteredLighting& operator=(const ClusteredLighting&) = delete;
    ClusteredLighting(ClusteredLighting&&) = default;
    ClusteredLighting& operator=(ClusteredLighting&&) = default;

    /**
     * Initialize clustered lighting system
     * @param width Screen width
     * @param height Screen height
     * @param nearPlane Camera near plane
     * @param farPlane Camera far plane
     */
    bool initialize(uint32_t width, uint32_t height, float nearPlane, float farPlane);

    /**
     * Shutdown and release resources
     */
    void shutdown();

    /**
     * Update cluster grid for new screen dimensions or camera settings
     */
    void updateClusters(uint32_t width, uint32_t height, float nearPlane, float farPlane);

    /**
     * Build cluster grid in view space (CPU-side)
     * Called when camera parameters change
     */
    void buildClusterGrid(const mat4& inverseProjection);

    /**
     * Assign lights to clusters (GPU compute shader)
     * @param commandBuffer RHI command buffer for dispatching compute
     * @param lightManager Light manager containing active lights
     * @param viewMatrix Camera view matrix
     * @param projectionMatrix Camera projection matrix
     */
    void assignLightsToClusters(
        CatEngine::RHI::IRHICommandBuffer* commandBuffer,
        const LightManager& lightManager,
        const mat4& viewMatrix,
        const mat4& projectionMatrix
    );

    /**
     * Get cluster index from 3D grid coordinates
     */
    uint32_t getClusterIndex(uint32_t x, uint32_t y, uint32_t z) const {
        return x + (y * CLUSTER_GRID_X) + (z * CLUSTER_GRID_X * CLUSTER_GRID_Y);
    }

    /**
     * Get 3D grid coordinates from cluster index
     */
    void getClusterCoordinates(uint32_t index, uint32_t& x, uint32_t& y, uint32_t& z) const {
        z = index / (CLUSTER_GRID_X * CLUSTER_GRID_Y);
        uint32_t remainder = index % (CLUSTER_GRID_X * CLUSTER_GRID_Y);
        y = remainder / CLUSTER_GRID_X;
        x = remainder % CLUSTER_GRID_X;
    }

    /**
     * Get cluster from screen position and depth (for fragment shaders)
     * @param screenPos Screen position (0-1 range)
     * @param linearDepth Linear depth in view space
     * @return Cluster index
     */
    uint32_t getClusterFromScreenSpace(const vec2& screenPos, float linearDepth) const;

    /**
     * Get Z-slice index from linear depth using logarithmic distribution
     */
    uint32_t getZSliceFromDepth(float linearDepth) const;

    // ========================================================================
    // GPU Resources
    // ========================================================================

    /**
     * Get cluster buffer (contains cluster bounds and light counts/offsets)
     */
    CatEngine::RHI::IRHIBuffer* getClusterBuffer() const { return m_clusterBuffer; }

    /**
     * Get light index list buffer (contains concatenated light indices for all clusters)
     */
    CatEngine::RHI::IRHIBuffer* getLightIndexListBuffer() const { return m_lightIndexListBuffer; }

    /**
     * Get cluster parameters buffer (uniform buffer with cluster settings)
     */
    CatEngine::RHI::IRHIBuffer* getClusterParamsBuffer() const { return m_clusterParamsBuffer; }

    /**
     * Get compute pipeline for light assignment
     */
    CatEngine::RHI::IRHIPipeline* getComputePipeline() const { return m_computePipeline; }

    /**
     * Get total number of clusters
     */
    uint32_t getTotalClusters() const { return TOTAL_CLUSTERS; }

    /**
     * Get cluster grid dimensions
     */
    vec3 getGridDimensions() const { return vec3(CLUSTER_GRID_X, CLUSTER_GRID_Y, CLUSTER_GRID_Z); }

    /**
     * Get current screen dimensions
     */
    vec2 getScreenDimensions() const { return vec2(m_screenWidth, m_screenHeight); }

    /**
     * Get near and far planes
     */
    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane() const { return m_farPlane; }

private:
    /**
     * Create GPU buffers for cluster data
     */
    bool createBuffers();

    /**
     * Create compute pipeline for light assignment
     */
    bool createComputePipeline();

    /**
     * Update cluster parameters uniform buffer
     */
    void updateClusterParams();

    /**
     * Upload cluster data to GPU
     */
    void uploadClusterData();

    /**
     * Compute logarithmic Z-slice for a given depth
     * Uses logarithmic distribution: slice = log(depth/near) / log(far/near) * numSlices
     */
    float computeZSlice(float depth) const;

    // Screen and camera parameters
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;

    // Cluster data (CPU-side)
    std::vector<Cluster> m_clusters;
    std::vector<GPUCluster> m_gpuClusters;

    // Cluster parameters
    ClusterParams m_clusterParams;

    // GPU resources
    CatEngine::RHI::IRHIBuffer* m_clusterBuffer = nullptr;         // Cluster data buffer
    CatEngine::RHI::IRHIBuffer* m_lightIndexListBuffer = nullptr;  // Light index list
    CatEngine::RHI::IRHIBuffer* m_clusterParamsBuffer = nullptr;   // Cluster parameters
    CatEngine::RHI::IRHIBuffer* m_atomicCounterBuffer = nullptr;   // Atomic counter for light assignment

    // Compute pipeline
    CatEngine::RHI::IRHIPipeline* m_computePipeline = nullptr;
    CatEngine::RHI::IRHIShader* m_computeShader = nullptr;

    // State
    bool m_initialized = false;
    bool m_clustersDirty = true;
};

} // namespace Engine::Renderer

#endif // ENGINE_RENDERER_LIGHTING_CLUSTERED_LIGHTING_HPP

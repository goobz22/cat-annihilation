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
#include "../../rhi/RHIDescriptorSet.hpp"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace CatEngine::RHI {
    class IRHIDevice;
}

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
     * Initialize clustered lighting system (legacy — no device; buffers/pipeline will not be created)
     * @param width Screen width
     * @param height Screen height
     * @param nearPlane Camera near plane
     * @param farPlane Camera far plane
     */
    bool initialize(uint32_t width, uint32_t height, float nearPlane, float farPlane);

    /**
     * Initialize clustered lighting system with an RHI device.
     * Creates GPU buffers and the compute pipeline for light assignment.
     * @param device RHI device used to create GPU resources
     * @param width Screen width
     * @param height Screen height
     * @param nearPlane Camera near plane
     * @param farPlane Camera far plane
     * @param cameraUBO Optional per-frame camera UBO bound at set=0 binding=0
     * @param lightsSSBO Optional lights SSBO bound at set=0 binding=1
     */
    bool initialize(
        CatEngine::RHI::IRHIDevice* device,
        uint32_t width,
        uint32_t height,
        float nearPlane,
        float farPlane,
        CatEngine::RHI::IRHIBuffer* cameraUBO = nullptr,
        CatEngine::RHI::IRHIBuffer* lightsSSBO = nullptr
    );

    /**
     * Shutdown and release resources
     */
    void shutdown();

    /**
     * Update cluster grid for new screen dimensions or camera settings
     */
    void updateClusters(uint32_t width, uint32_t height, float nearPlane, float farPlane);

    /**
     * Record a GPU light-assignment pass for the current frame.
     * Binds the compute pipeline + descriptor set, dispatches one workgroup per
     * cluster, and inserts a pipeline barrier so subsequent lighting passes see
     * the populated cluster grid and light index list.
     *
     * Note: no view/projection parameters. The compute shader reads camera
     * matrices from the CameraData UBO bound at (set=0, binding=0), which is
     * owned and refreshed externally by the renderer each frame before this
     * dispatch runs. Passing matrices in here would invite callers to hand
     * in stale data that diverges from what the shader actually reads.
     *
     * @param commandBuffer RHI command buffer in a recording state
     */
    void updateClusters(CatEngine::RHI::IRHICommandBuffer* commandBuffer);

    /**
     * Build cluster grid in view space (CPU-side)
     * Called when camera parameters change
     */
    void buildClusterGrid(const mat4& inverseProjection);

    /**
     * Assign lights to clusters (GPU compute shader). The light SSBO is
     * wired into the descriptor set at initialize() time; the caller is
     * responsible for having already pushed fresh light data through
     * LightManager::uploadToGPU() for the current frame.
     *
     * @param commandBuffer RHI command buffer for dispatching compute
     */
    void assignLightsToClusters(CatEngine::RHI::IRHICommandBuffer* commandBuffer);

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
     * Get light index list buffer (contains concatenated light indices for all clusters)
     *
     * Note: there is no getClusterBuffer() — clustered.comp recomputes cluster
     * AABBs inline each dispatch, so no separate AABB buffer is uploaded.
     */
    CatEngine::RHI::IRHIBuffer* getLightIndexListBuffer() const { return m_lightIndexListBuffer; }

    /**
     * Get light grid buffer (per-cluster uvec2(offset, count) into the light index list)
     */
    CatEngine::RHI::IRHIBuffer* getLightGridBuffer() const { return m_lightGridBuffer; }

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

    /**
     * Read a SPIR-V binary blob from disk into a byte vector.
     * Returns an empty vector and logs to stderr on failure.
     */
    static std::vector<uint8_t> readSpirvFile(const std::string& path);

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

    // RHI device (non-owning)
    CatEngine::RHI::IRHIDevice* m_device = nullptr;

    // External resources bound into the descriptor set (non-owning)
    CatEngine::RHI::IRHIBuffer* m_cameraUBO = nullptr;
    CatEngine::RHI::IRHIBuffer* m_lightsSSBO = nullptr;

    // GPU resources (owned)
    // Note: no m_clusterBuffer (AABB data) — clustered.comp calculates
    // cluster AABBs inline per workgroup, so there is no GPU-side AABB
    // buffer to maintain. m_gpuClusters (CPU-side) is still populated for
    // debug/testing consumers but never uploaded.
    CatEngine::RHI::IRHIBuffer* m_lightIndexListBuffer = nullptr;  // Flat light index list (set=0, binding=2)
    CatEngine::RHI::IRHIBuffer* m_lightGridBuffer = nullptr;       // Per-cluster uvec2(offset, count) (set=0, binding=3)
    CatEngine::RHI::IRHIBuffer* m_clusterParamsBuffer = nullptr;   // Cluster parameters UBO (fallback for binding 0)
    CatEngine::RHI::IRHIBuffer* m_atomicCounterBuffer = nullptr;   // Atomic counter for light assignment (set=0, binding=4)

    // Compute pipeline (owned)
    CatEngine::RHI::IRHIShader* m_computeShader = nullptr;
    CatEngine::RHI::IRHIDescriptorSetLayout* m_descriptorSetLayout = nullptr;
    CatEngine::RHI::IRHIPipelineLayout* m_pipelineLayout = nullptr;
    CatEngine::RHI::IRHIPipeline* m_computePipeline = nullptr;
    CatEngine::RHI::IRHIDescriptorPool* m_descriptorPool = nullptr;
    CatEngine::RHI::IRHIDescriptorSet* m_descriptorSet = nullptr;

    // Dispatch sizing (matches shader local_size)
    static constexpr uint32_t LOCAL_SIZE_X = 16;
    static constexpr uint32_t LOCAL_SIZE_Y = 9;
    static constexpr uint32_t LOCAL_SIZE_Z = 1;

    // State
    bool m_initialized = false;
    bool m_clustersDirty = true;
};

} // namespace Engine::Renderer

#endif // ENGINE_RENDERER_LIGHTING_CLUSTERED_LIGHTING_HPP

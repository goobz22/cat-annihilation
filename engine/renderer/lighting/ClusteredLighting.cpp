#include "ClusteredLighting.hpp"
#include "../../math/Math.hpp"
#include <cmath>
#include <algorithm>

namespace Engine::Renderer {

ClusteredLighting::ClusteredLighting() {
    // Reserve space for all clusters
    m_clusters.resize(TOTAL_CLUSTERS);
    m_gpuClusters.resize(TOTAL_CLUSTERS);
}

ClusteredLighting::~ClusteredLighting() {
    shutdown();
}

bool ClusteredLighting::initialize(uint32_t width, uint32_t height, float nearPlane, float farPlane) {
    if (m_initialized) {
        return true;
    }

    m_screenWidth = width;
    m_screenHeight = height;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;

    // Create GPU buffers
    if (!createBuffers()) {
        return false;
    }

    // Create compute pipeline (would need actual implementation with RHI)
    // if (!createComputePipeline()) {
    //     return false;
    // }

    // Update cluster parameters
    updateClusterParams();

    m_initialized = true;
    m_clustersDirty = true;

    return true;
}

void ClusteredLighting::shutdown() {
    // Clean up resources
    // Note: In a real implementation, these would be smart pointers or managed by RHI
    m_clusterBuffer = nullptr;
    m_lightIndexListBuffer = nullptr;
    m_clusterParamsBuffer = nullptr;
    m_atomicCounterBuffer = nullptr;
    m_computePipeline = nullptr;
    m_computeShader = nullptr;

    m_initialized = false;
}

void ClusteredLighting::updateClusters(uint32_t width, uint32_t height, float nearPlane, float farPlane) {
    bool changed = false;

    if (m_screenWidth != width || m_screenHeight != height) {
        m_screenWidth = width;
        m_screenHeight = height;
        changed = true;
    }

    if (!Math::approximately(m_nearPlane, nearPlane) || !Math::approximately(m_farPlane, farPlane)) {
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        changed = true;
    }

    if (changed) {
        updateClusterParams();
        m_clustersDirty = true;
    }
}

void ClusteredLighting::buildClusterGrid(const mat4& inverseProjection) {
    // Build cluster AABBs in view space
    const float nearPlane = m_nearPlane;
    const float farPlane = m_farPlane;

    // For each cluster in the grid
    for (uint32_t z = 0; z < CLUSTER_GRID_Z; ++z) {
        for (uint32_t y = 0; y < CLUSTER_GRID_Y; ++y) {
            for (uint32_t x = 0; x < CLUSTER_GRID_X; ++x) {
                uint32_t index = getClusterIndex(x, y, z);
                Cluster& cluster = m_clusters[index];

                // Calculate screen space bounds (normalized device coordinates)
                float minX = (float)x / (float)CLUSTER_GRID_X * 2.0f - 1.0f;
                float maxX = (float)(x + 1) / (float)CLUSTER_GRID_X * 2.0f - 1.0f;
                float minY = (float)y / (float)CLUSTER_GRID_Y * 2.0f - 1.0f;
                float maxY = (float)(y + 1) / (float)CLUSTER_GRID_Y * 2.0f - 1.0f;

                // Calculate depth bounds using logarithmic distribution
                float minDepthNormalized = (float)z / (float)CLUSTER_GRID_Z;
                float maxDepthNormalized = (float)(z + 1) / (float)CLUSTER_GRID_Z;

                // Convert to linear depth using logarithmic distribution
                // depth = near * (far/near)^t where t is normalized depth
                float logRatio = std::log(farPlane / nearPlane);
                float minDepth = nearPlane * std::exp(minDepthNormalized * logRatio);
                float maxDepth = nearPlane * std::exp(maxDepthNormalized * logRatio);

                // Transform NDC corners to view space using inverse projection
                // Near plane corners
                vec4 nearCorners[4] = {
                    vec4(minX, minY, -1.0f, 1.0f),
                    vec4(maxX, minY, -1.0f, 1.0f),
                    vec4(minX, maxY, -1.0f, 1.0f),
                    vec4(maxX, maxY, -1.0f, 1.0f)
                };

                // Far plane corners
                vec4 farCorners[4] = {
                    vec4(minX, minY, 1.0f, 1.0f),
                    vec4(maxX, minY, 1.0f, 1.0f),
                    vec4(minX, maxY, 1.0f, 1.0f),
                    vec4(maxX, maxY, 1.0f, 1.0f)
                };

                // Build AABB in view space
                AABB viewSpaceAABB;

                // Transform corners and build AABB
                for (int i = 0; i < 4; ++i) {
                    vec4 nearView = inverseProjection * nearCorners[i];
                    nearView = nearView / nearView.w;

                    vec4 farView = inverseProjection * farCorners[i];
                    farView = farView / farView.w;

                    // Interpolate to actual depth bounds
                    vec3 nearPoint = nearView.xyz() * (minDepth / std::abs(nearView.z));
                    vec3 farPoint = farView.xyz() * (maxDepth / std::abs(farView.z));

                    viewSpaceAABB.expand(nearPoint);
                    viewSpaceAABB.expand(farPoint);
                }

                cluster.bounds = viewSpaceAABB;
                cluster.minPoint = vec3(minX, minY, minDepth);
                cluster.maxPoint = vec3(maxX, maxY, maxDepth);
                cluster.lightCount = 0;
                cluster.lightOffset = 0;

                // Convert to GPU format
                m_gpuClusters[index].minBounds = vec4(cluster.bounds.min, 0.0f);
                m_gpuClusters[index].maxBounds = vec4(cluster.bounds.max, 0.0f);
            }
        }
    }

    // Upload to GPU
    uploadClusterData();
    m_clustersDirty = false;
}

void ClusteredLighting::assignLightsToClusters(
    CatEngine::RHI::IRHICommandBuffer* commandBuffer,
    const LightManager& lightManager,
    const mat4& viewMatrix,
    const mat4& projectionMatrix)
{
    // In a real implementation, this would:
    // 1. Upload light data to GPU buffer (from LightManager)
    // 2. Bind compute shader and descriptor sets
    // 3. Dispatch compute shader to assign lights to clusters
    // 4. Synchronize with graphics pipeline

    // Pseudo-code for compute dispatch:
    //
    // commandBuffer->BindPipeline(m_computePipeline);
    // commandBuffer->BindDescriptorSet(0, clusterDescriptorSet);
    // commandBuffer->BindDescriptorSet(1, lightDescriptorSet);
    //
    // // Dispatch compute shader
    // // One thread group per cluster
    // uint32_t groupsX = (CLUSTER_GRID_X + 7) / 8;  // 8x8x1 thread groups
    // uint32_t groupsY = (CLUSTER_GRID_Y + 7) / 8;
    // uint32_t groupsZ = CLUSTER_GRID_Z;
    // commandBuffer->Dispatch(groupsX, groupsY, groupsZ);

    // Note: Actual GPU implementation would be in the compute shader (clustered.comp)
    // The shader would:
    // 1. For each cluster, get its AABB in view space
    // 2. Transform light positions to view space
    // 3. Test each light against the cluster AABB
    // 4. For intersecting lights, add light index to cluster's light list
    // 5. Update cluster light count and offset atomically
}

uint32_t ClusteredLighting::getClusterFromScreenSpace(const vec2& screenPos, float linearDepth) const {
    // Convert screen position (0-1) to grid coordinates
    uint32_t x = static_cast<uint32_t>(screenPos.x * CLUSTER_GRID_X);
    uint32_t y = static_cast<uint32_t>(screenPos.y * CLUSTER_GRID_Y);

    // Clamp to grid bounds
    x = std::min(x, CLUSTER_GRID_X - 1);
    y = std::min(y, CLUSTER_GRID_Y - 1);

    // Get Z-slice from depth
    uint32_t z = getZSliceFromDepth(linearDepth);

    return getClusterIndex(x, y, z);
}

uint32_t ClusteredLighting::getZSliceFromDepth(float linearDepth) const {
    // Logarithmic depth slicing
    float slice = computeZSlice(linearDepth);

    // Convert to integer slice index
    uint32_t z = static_cast<uint32_t>(slice);
    z = std::min(z, CLUSTER_GRID_Z - 1);

    return z;
}

float ClusteredLighting::computeZSlice(float depth) const {
    // Logarithmic distribution: slice = log(depth/near) / log(far/near) * numSlices
    if (depth <= m_nearPlane) {
        return 0.0f;
    }
    if (depth >= m_farPlane) {
        return static_cast<float>(CLUSTER_GRID_Z - 1);
    }

    float logRatio = std::log(m_farPlane / m_nearPlane);
    float logDepth = std::log(depth / m_nearPlane);
    return (logDepth / logRatio) * static_cast<float>(CLUSTER_GRID_Z);
}

// ============================================================================
// Private Methods
// ============================================================================

bool ClusteredLighting::createBuffers() {
    // In a real implementation, this would create GPU buffers using RHI
    // For now, we just set up the data structures

    // Cluster buffer: stores cluster bounds and light counts
    // Size: TOTAL_CLUSTERS * sizeof(GPUCluster)

    // Light index list buffer: stores concatenated light indices for all clusters
    // Size: TOTAL_CLUSTERS * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32_t)
    // This is conservative; actual size could be smaller with dynamic allocation

    // Cluster parameters buffer: uniform buffer with cluster settings
    // Size: sizeof(ClusterParams)

    // Atomic counter buffer: for dynamic light index allocation
    // Size: sizeof(uint32_t)

    // Note: Actual buffer creation would happen here with RHI calls
    // Example:
    // BufferDesc desc;
    // desc.size = TOTAL_CLUSTERS * sizeof(GPUCluster);
    // desc.usage = BufferUsage::Storage;
    // desc.memoryProperties = MemoryProperty::DeviceLocal;
    // m_clusterBuffer = RHI::CreateBuffer(desc);

    return true; // Placeholder
}

bool ClusteredLighting::createComputePipeline() {
    // In a real implementation, this would:
    // 1. Load the compute shader (clustered.comp)
    // 2. Create descriptor set layouts
    // 3. Create compute pipeline with the shader

    // Pseudo-code:
    //
    // ShaderDesc shaderDesc;
    // shaderDesc.stage = ShaderStage::Compute;
    // shaderDesc.code = LoadSPIRV("shaders/lighting/clustered.comp.spv");
    // m_computeShader = RHI::CreateShader(shaderDesc);
    //
    // ComputePipelineDesc pipelineDesc;
    // pipelineDesc.shader = m_computeShader;
    // m_computePipeline = RHI::CreateComputePipeline(pipelineDesc);

    return true; // Placeholder
}

void ClusteredLighting::updateClusterParams() {
    m_clusterParams.screenDimensions = vec4(
        static_cast<float>(m_screenWidth),
        static_cast<float>(m_screenHeight),
        1.0f / static_cast<float>(m_screenWidth),
        1.0f / static_cast<float>(m_screenHeight)
    );

    float logRatio = std::log(m_farPlane / m_nearPlane);
    float clusterScale = static_cast<float>(CLUSTER_GRID_Z) / logRatio;

    m_clusterParams.clusterParams = vec4(
        m_nearPlane,
        m_farPlane,
        logRatio,
        clusterScale
    );

    m_clusterParams.gridDimensions = vec4(
        static_cast<float>(CLUSTER_GRID_X),
        static_cast<float>(CLUSTER_GRID_Y),
        static_cast<float>(CLUSTER_GRID_Z),
        static_cast<float>(TOTAL_CLUSTERS)
    );

    m_clusterParams.invGridDimensions = vec4(
        1.0f / static_cast<float>(CLUSTER_GRID_X),
        1.0f / static_cast<float>(CLUSTER_GRID_Y),
        1.0f / static_cast<float>(CLUSTER_GRID_Z),
        0.0f
    );

    // Upload to GPU buffer
    if (m_clusterParamsBuffer) {
        m_clusterParamsBuffer->UpdateData(&m_clusterParams, sizeof(ClusterParams), 0);
    }
}

void ClusteredLighting::uploadClusterData() {
    if (m_clusterBuffer && !m_gpuClusters.empty()) {
        size_t dataSize = m_gpuClusters.size() * sizeof(GPUCluster);
        m_clusterBuffer->UpdateData(m_gpuClusters.data(), dataSize, 0);
    }
}

} // namespace Engine::Renderer

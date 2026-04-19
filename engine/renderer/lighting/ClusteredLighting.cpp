#include "ClusteredLighting.hpp"
#include "../../math/Math.hpp"
#include "../../rhi/RHI.hpp"
#include "../../rhi/vulkan/VulkanCommandBuffer.hpp"
#include "../../rhi/vulkan/VulkanBuffer.hpp"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vulkan/vulkan.h>

namespace Engine::Renderer {

namespace {

// Per-cluster entry in the light grid (uvec2 offset+count -> 8 bytes each).
constexpr uint64_t LIGHT_GRID_ENTRY_SIZE = sizeof(uint32_t) * 2;

} // namespace

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

    // No RHI device was supplied; the caller intends to set up GPU resources
    // externally. Just populate CPU-side cluster parameters so the grid math
    // is usable on the CPU (e.g. for getClusterFromScreenSpace).
    updateClusterParams();

    m_initialized = true;
    m_clustersDirty = true;

    return true;
}

bool ClusteredLighting::initialize(
    CatEngine::RHI::IRHIDevice* device,
    uint32_t width,
    uint32_t height,
    float nearPlane,
    float farPlane,
    CatEngine::RHI::IRHIBuffer* cameraUBO,
    CatEngine::RHI::IRHIBuffer* lightsSSBO)
{
    if (m_initialized) {
        return true;
    }

    if (device == nullptr) {
        std::cerr << "[ClusteredLighting] initialize: device is null" << std::endl;
        return false;
    }

    m_device = device;
    m_cameraUBO = cameraUBO;
    m_lightsSSBO = lightsSSBO;
    m_screenWidth = width;
    m_screenHeight = height;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;

    if (!createBuffers()) {
        std::cerr << "[ClusteredLighting] createBuffers failed" << std::endl;
        shutdown();
        return false;
    }

    if (!createComputePipeline()) {
        std::cerr << "[ClusteredLighting] createComputePipeline failed" << std::endl;
        shutdown();
        return false;
    }

    updateClusterParams();

    // If external buffers were supplied up front, wire them into the descriptor
    // set now so the first dispatch sees a fully-populated binding table.
    //
    // Bindings MUST match shaders/lighting/clustered.comp exactly:
    //   (set=0, binding=0) uniform  CameraData         -> m_cameraUBO
    //   (set=0, binding=1) uniform  LightData          -> m_lightsSSBO (UBO)
    //   (set=0, binding=2) buffer   ClusterLightIndices-> m_lightIndexListBuffer
    //   (set=0, binding=3) buffer   ClusterLightGrid   -> m_lightGridBuffer
    //   (set=0, binding=4) buffer   AtomicCounter      -> m_atomicCounterBuffer
    //
    // Note: binding 1 is declared `uniform LightData` in GLSL, i.e. a UBO, not
    // a storage buffer. The external `m_lightsSSBO` name is historical — the
    // buffer itself is bound as a uniform buffer descriptor here. The caller
    // (LightManager) is responsible for creating it with BufferUsage::Uniform.
    //
    // The old code also wrote an m_clusterBuffer (cluster AABBs) into
    // binding 2. That binding is actually clusterLightIndices in the shader,
    // and the shader recomputes AABBs inline (see calculateClusterAABB) — so
    // the AABB buffer had no consumer at all. It has been removed entirely.
    if (m_descriptorSet != nullptr) {
        CatEngine::RHI::DescriptorBufferInfo cameraInfo{};
        CatEngine::RHI::DescriptorBufferInfo lightsInfo{};
        CatEngine::RHI::DescriptorBufferInfo indexInfo{};
        CatEngine::RHI::DescriptorBufferInfo gridInfo{};
        CatEngine::RHI::DescriptorBufferInfo atomicInfo{};

        // Fallback to our own params buffer if no external camera UBO was
        // supplied — keeps the descriptor set valid even in headless tests
        // that never wire a camera.
        cameraInfo.buffer = m_cameraUBO != nullptr ? m_cameraUBO : m_clusterParamsBuffer;
        cameraInfo.offset = 0;
        cameraInfo.range = 0;

        lightsInfo.buffer = m_lightsSSBO;
        lightsInfo.offset = 0;
        lightsInfo.range = 0;

        indexInfo.buffer = m_lightIndexListBuffer;
        indexInfo.offset = 0;
        indexInfo.range = 0;

        gridInfo.buffer = m_lightGridBuffer;
        gridInfo.offset = 0;
        gridInfo.range = 0;

        atomicInfo.buffer = m_atomicCounterBuffer;
        atomicInfo.offset = 0;
        atomicInfo.range = 0;

        CatEngine::RHI::WriteDescriptor writes[5]{};

        writes[0].binding = 0;
        writes[0].descriptorType = CatEngine::RHI::DescriptorType::UniformBuffer;
        writes[0].descriptorCount = 1;
        writes[0].bufferInfo = &cameraInfo;

        // Binding 1 is `uniform LightData` in the shader — must be UB, not SSBO.
        writes[1].binding = 1;
        writes[1].descriptorType = CatEngine::RHI::DescriptorType::UniformBuffer;
        writes[1].descriptorCount = 1;
        writes[1].bufferInfo = &lightsInfo;

        writes[2].binding = 2;
        writes[2].descriptorType = CatEngine::RHI::DescriptorType::StorageBuffer;
        writes[2].descriptorCount = 1;
        writes[2].bufferInfo = &indexInfo;

        writes[3].binding = 3;
        writes[3].descriptorType = CatEngine::RHI::DescriptorType::StorageBuffer;
        writes[3].descriptorCount = 1;
        writes[3].bufferInfo = &gridInfo;

        writes[4].binding = 4;
        writes[4].descriptorType = CatEngine::RHI::DescriptorType::StorageBuffer;
        writes[4].descriptorCount = 1;
        writes[4].bufferInfo = &atomicInfo;

        uint32_t writeCount = 0;
        CatEngine::RHI::WriteDescriptor packed[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            if (writes[i].bufferInfo != nullptr && writes[i].bufferInfo->buffer != nullptr) {
                packed[writeCount++] = writes[i];
            }
        }
        if (writeCount > 0) {
            m_descriptorSet->Update(packed, writeCount);
        }
    }

    m_initialized = true;
    m_clustersDirty = true;

    std::cerr << "[ClusteredLighting] initialized " << CLUSTER_GRID_X << "x"
              << CLUSTER_GRID_Y << "x" << CLUSTER_GRID_Z << " clusters ("
              << TOTAL_CLUSTERS << " total)" << std::endl;

    return true;
}

void ClusteredLighting::shutdown() {
    if (m_device != nullptr) {
        if (m_descriptorSet != nullptr) {
            m_device->DestroyDescriptorSet(m_descriptorSet);
            m_descriptorSet = nullptr;
        }
        if (m_descriptorPool != nullptr) {
            m_device->DestroyDescriptorPool(m_descriptorPool);
            m_descriptorPool = nullptr;
        }
        if (m_computePipeline != nullptr) {
            m_device->DestroyPipeline(m_computePipeline);
            m_computePipeline = nullptr;
        }
        if (m_pipelineLayout != nullptr) {
            m_device->DestroyPipelineLayout(m_pipelineLayout);
            m_pipelineLayout = nullptr;
        }
        if (m_descriptorSetLayout != nullptr) {
            m_device->DestroyDescriptorSetLayout(m_descriptorSetLayout);
            m_descriptorSetLayout = nullptr;
        }
        if (m_computeShader != nullptr) {
            m_device->DestroyShader(m_computeShader);
            m_computeShader = nullptr;
        }

        if (m_atomicCounterBuffer != nullptr) {
            m_device->DestroyBuffer(m_atomicCounterBuffer);
            m_atomicCounterBuffer = nullptr;
        }
        if (m_clusterParamsBuffer != nullptr) {
            m_device->DestroyBuffer(m_clusterParamsBuffer);
            m_clusterParamsBuffer = nullptr;
        }
        if (m_lightGridBuffer != nullptr) {
            m_device->DestroyBuffer(m_lightGridBuffer);
            m_lightGridBuffer = nullptr;
        }
        if (m_lightIndexListBuffer != nullptr) {
            m_device->DestroyBuffer(m_lightIndexListBuffer);
            m_lightIndexListBuffer = nullptr;
        }
    } else {
        // No device available; null out the handles so we don't dangle.
        m_descriptorSet = nullptr;
        m_descriptorPool = nullptr;
        m_computePipeline = nullptr;
        m_pipelineLayout = nullptr;
        m_descriptorSetLayout = nullptr;
        m_computeShader = nullptr;
        m_atomicCounterBuffer = nullptr;
        m_clusterParamsBuffer = nullptr;
        m_lightGridBuffer = nullptr;
        m_lightIndexListBuffer = nullptr;
    }

    m_cameraUBO = nullptr;
    m_lightsSSBO = nullptr;
    m_device = nullptr;

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

void ClusteredLighting::updateClusters(CatEngine::RHI::IRHICommandBuffer* commandBuffer) {
    // Note: view/projection are deliberately NOT parameters. The compute
    // shader (shaders/lighting/clustered.comp) reads camera.view and
    // camera.projection directly from the CameraData UBO bound at
    // (set=0, binding=0) — see m_cameraUBO. That UBO is owned and refreshed
    // externally by the renderer each frame before this dispatch runs.
    // Accepting redundant matrices here would invite callers to pass stale
    // data that diverges from the UBO the shader is actually reading.
    if (commandBuffer == nullptr) {
        std::cerr << "[ClusteredLighting] updateClusters: null command buffer" << std::endl;
        return;
    }
    if (m_computePipeline == nullptr || m_pipelineLayout == nullptr || m_descriptorSet == nullptr) {
        std::cerr << "[ClusteredLighting] updateClusters: pipeline or descriptor set not ready" << std::endl;
        return;
    }

    // Reset the atomic allocation counter at the head of each dispatch so
    // offsets into the flat light index list restart from zero.
    if (m_atomicCounterBuffer != nullptr) {
        uint32_t zero = 0;
        m_atomicCounterBuffer->UpdateData(&zero, sizeof(uint32_t), 0);
    }

    commandBuffer->BindPipeline(m_computePipeline);

    CatEngine::RHI::IRHIDescriptorSet* sets[] = { m_descriptorSet };
    commandBuffer->BindDescriptorSets(
        CatEngine::RHI::PipelineBindPoint::Compute,
        m_pipelineLayout,
        /*firstSet=*/0,
        sets,
        /*descriptorSetCount=*/1
    );

    // One workgroup per cluster column (x,y) per Z-slice. Workgroup size is
    // (LOCAL_SIZE_X, LOCAL_SIZE_Y, LOCAL_SIZE_Z) matching clustered.comp.
    const uint32_t groupsX = (CLUSTER_GRID_X + LOCAL_SIZE_X - 1) / LOCAL_SIZE_X;
    const uint32_t groupsY = (CLUSTER_GRID_Y + LOCAL_SIZE_Y - 1) / LOCAL_SIZE_Y;
    const uint32_t groupsZ = (CLUSTER_GRID_Z + LOCAL_SIZE_Z - 1) / LOCAL_SIZE_Z;
    commandBuffer->Dispatch(groupsX, groupsY, groupsZ);

    // Post-dispatch barrier: the compute shader just wrote three SSBOs
    // (lightGrid, lightIndexList, atomicCounter) via atomicAdd + stores.
    // The lighting fragment shader that reads them next frame/pass must
    // happen-after those writes.
    //
    // The base IRHICommandBuffer::PipelineBarrier() is a wildcard
    // ALL_COMMANDS->ALL_COMMANDS memory barrier which is both unnecessarily
    // slow and semantically vague — replace it with a tight buffer memory
    // barrier addressed at the three buffers we actually touched:
    //   src stage:  COMPUTE_SHADER         (compute wrote the SSBOs)
    //   src access: SHADER_WRITE           (atomicAdd + .data[i] = ...)
    //   dst stage:  FRAGMENT_SHADER        (lighting pass samples them)
    //   dst access: SHADER_READ            (read-only in the fragment pass)
    //
    // Using VulkanCommandBuffer::PipelineBarrierFull (the Vulkan-side
    // extension on our RHI cmdbuf) to get access to buffer barriers
    // specifically rather than the generic memory-barrier shortcut.
    auto* vulkanCmd = static_cast<CatEngine::RHI::VulkanCommandBuffer*>(commandBuffer);

    VkBufferMemoryBarrier bufferBarriers[3]{};
    uint32_t barrierCount = 0;

    auto addBufferBarrier = [&](CatEngine::RHI::IRHIBuffer* buffer) {
        if (buffer == nullptr) {
            return;
        }
        auto* vulkanBuffer = static_cast<CatEngine::RHI::VulkanBuffer*>(buffer);
        VkBuffer handle = vulkanBuffer->GetHandle();
        if (handle == VK_NULL_HANDLE) {
            return;
        }
        VkBufferMemoryBarrier& b = bufferBarriers[barrierCount++];
        b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = handle;
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;
    };

    addBufferBarrier(m_lightGridBuffer);
    addBufferBarrier(m_lightIndexListBuffer);
    addBufferBarrier(m_atomicCounterBuffer);

    if (barrierCount > 0) {
        vulkanCmd->PipelineBarrierFull(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            /*dependencyFlags=*/0,
            /*memoryBarriers=*/nullptr, /*memoryBarrierCount=*/0,
            bufferBarriers, barrierCount,
            /*imageBarriers=*/nullptr, /*imageBarrierCount=*/0
        );
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

void ClusteredLighting::assignLightsToClusters(CatEngine::RHI::IRHICommandBuffer* commandBuffer) {
    // Delegate to the compute-dispatch overload. The LightManager's GPU buffer
    // is wired into the descriptor set at set=0 binding=1 at initialize() time;
    // the caller is responsible for having already called
    // LightManager::uploadToGPU() for the current frame. The camera UBO is
    // likewise updated externally by the renderer each frame.
    //
    // The old signature accepted LightManager + view + projection and ignored
    // them all (forwarded to updateClusters which also ignored the matrices).
    // Keeping those parameters just let callers pass stale data with no
    // enforcement that it matched what the shader was actually reading, so
    // they have been removed rather than rubber-stamped with (void) casts.
    updateClusters(commandBuffer);
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

std::vector<uint8_t> ClusteredLighting::readSpirvFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "[ClusteredLighting] readSpirvFile: failed to open '" << path << "'" << std::endl;
        return {};
    }

    const std::streampos end = file.tellg();
    if (end <= 0) {
        std::cerr << "[ClusteredLighting] readSpirvFile: empty or unseekable file '" << path << "'" << std::endl;
        return {};
    }

    const std::size_t byteCount = static_cast<std::size_t>(end);
    std::vector<uint8_t> bytes(byteCount);

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byteCount));
    if (!file) {
        std::cerr << "[ClusteredLighting] readSpirvFile: short read on '" << path
                  << "' (" << file.gcount() << "/" << byteCount << " bytes)" << std::endl;
        return {};
    }

    if ((byteCount % 4) != 0) {
        std::cerr << "[ClusteredLighting] readSpirvFile: '" << path
                  << "' size " << byteCount << " is not a multiple of 4 (not valid SPIR-V)" << std::endl;
        return {};
    }

    return bytes;
}

bool ClusteredLighting::createBuffers() {
    if (m_device == nullptr) {
        std::cerr << "[ClusteredLighting] createBuffers: no device" << std::endl;
        return false;
    }

    // NOTE: there is no m_clusterBuffer (cluster AABB buffer) any more.
    // clustered.comp recomputes cluster AABBs inline per workgroup in
    // calculateClusterAABB() using camera.nearPlane/farPlane + the inverse
    // projection. The old m_clusterBuffer was never consumed by any shader
    // and was incorrectly aliased to binding 2 (clusterLightIndices), which
    // corrupted the flat light index list at dispatch time. Removed.

    // --- Light index list buffer (flat uint32 array, worst-case sized) ----
    {
        CatEngine::RHI::BufferDesc desc{};
        desc.size = static_cast<uint64_t>(TOTAL_CLUSTERS)
                  * static_cast<uint64_t>(MAX_LIGHTS_PER_CLUSTER)
                  * sizeof(uint32_t);
        desc.usage = CatEngine::RHI::BufferUsage::Storage | CatEngine::RHI::BufferUsage::TransferDst;
        desc.memoryProperties = CatEngine::RHI::MemoryProperty::DeviceLocal;
        desc.debugName = "ClusteredLighting::lightIndexList";
        m_lightIndexListBuffer = m_device->CreateBuffer(desc);
        if (m_lightIndexListBuffer == nullptr) {
            std::cerr << "[ClusteredLighting] CreateBuffer(lightIndexList) failed" << std::endl;
            return false;
        }
    }

    // --- Light grid buffer (per-cluster uvec2 offset+count) ---------------
    {
        CatEngine::RHI::BufferDesc desc{};
        desc.size = static_cast<uint64_t>(TOTAL_CLUSTERS) * LIGHT_GRID_ENTRY_SIZE;
        desc.usage = CatEngine::RHI::BufferUsage::Storage | CatEngine::RHI::BufferUsage::TransferDst;
        desc.memoryProperties = CatEngine::RHI::MemoryProperty::DeviceLocal;
        desc.debugName = "ClusteredLighting::lightGrid";
        m_lightGridBuffer = m_device->CreateBuffer(desc);
        if (m_lightGridBuffer == nullptr) {
            std::cerr << "[ClusteredLighting] CreateBuffer(lightGrid) failed" << std::endl;
            return false;
        }
    }

    // --- Cluster parameters UBO ------------------------------------------
    {
        CatEngine::RHI::BufferDesc desc{};
        desc.size = sizeof(ClusterParams);
        desc.usage = CatEngine::RHI::BufferUsage::Uniform | CatEngine::RHI::BufferUsage::TransferDst;
        desc.memoryProperties = CatEngine::RHI::MemoryProperty::HostVisible
                              | CatEngine::RHI::MemoryProperty::HostCoherent;
        desc.debugName = "ClusteredLighting::clusterParams";
        m_clusterParamsBuffer = m_device->CreateBuffer(desc);
        if (m_clusterParamsBuffer == nullptr) {
            std::cerr << "[ClusteredLighting] CreateBuffer(clusterParams) failed" << std::endl;
            return false;
        }
    }

    // --- Atomic counter buffer (single uint32) ----------------------------
    {
        CatEngine::RHI::BufferDesc desc{};
        desc.size = sizeof(uint32_t);
        desc.usage = CatEngine::RHI::BufferUsage::Storage | CatEngine::RHI::BufferUsage::TransferDst;
        desc.memoryProperties = CatEngine::RHI::MemoryProperty::HostVisible
                              | CatEngine::RHI::MemoryProperty::HostCoherent;
        desc.debugName = "ClusteredLighting::atomicCounter";
        m_atomicCounterBuffer = m_device->CreateBuffer(desc);
        if (m_atomicCounterBuffer == nullptr) {
            std::cerr << "[ClusteredLighting] CreateBuffer(atomicCounter) failed" << std::endl;
            return false;
        }

        // Seed the counter with zero.
        uint32_t zero = 0;
        m_atomicCounterBuffer->UpdateData(&zero, sizeof(uint32_t), 0);
    }

    return true;
}

bool ClusteredLighting::createComputePipeline() {
    if (m_device == nullptr) {
        std::cerr << "[ClusteredLighting] createComputePipeline: no device" << std::endl;
        return false;
    }

    // 1) Load compiled SPIR-V from disk.
    const std::string spirvPath = "shaders/lighting/clustered.comp.spv";
    std::vector<uint8_t> spirv = readSpirvFile(spirvPath);
    if (spirv.empty()) {
        return false;
    }

    // 2) Create the shader module.
    {
        CatEngine::RHI::ShaderDesc desc{};
        desc.stage = CatEngine::RHI::ShaderStage::Compute;
        desc.code = spirv.data();
        desc.codeSize = static_cast<uint64_t>(spirv.size());
        desc.entryPoint = "main";
        desc.debugName = "ClusteredLighting::clustered.comp";
        m_computeShader = m_device->CreateShader(desc);
        if (m_computeShader == nullptr) {
            std::cerr << "[ClusteredLighting] CreateShader failed for '" << spirvPath << "'" << std::endl;
            return false;
        }
    }

    // 3) Descriptor set layout: MUST match clustered.comp byte-for-byte.
    //    binding 0: uniform  CameraData          (UBO)
    //    binding 1: uniform  LightData           (UBO — *not* storage!)
    //    binding 2: buffer   ClusterLightIndices (SSBO, flat uint index list)
    //    binding 3: buffer   ClusterLightGrid    (SSBO, uvec2 offset+count)
    //    binding 4: buffer   AtomicCounter       (SSBO, single uint head)
    //
    // Previously binding 1 was declared as StorageBuffer here, which would
    // cause a validation-layer type-mismatch at BindDescriptorSets time and
    // undefined behaviour at dispatch. clustered.comp uses `uniform LightData`
    // so it must be bound as UniformBuffer.
    {
        CatEngine::RHI::DescriptorSetLayoutDesc desc{};
        desc.debugName = "ClusteredLighting::descriptorSetLayout";
        desc.bindings.reserve(5);

        CatEngine::RHI::DescriptorBinding b{};

        b.binding = 0;
        b.descriptorType = CatEngine::RHI::DescriptorType::UniformBuffer;
        b.descriptorCount = 1;
        b.stageFlags = CatEngine::RHI::ShaderStage::Compute;
        desc.bindings.push_back(b);

        b.binding = 1;
        b.descriptorType = CatEngine::RHI::DescriptorType::UniformBuffer;
        b.descriptorCount = 1;
        b.stageFlags = CatEngine::RHI::ShaderStage::Compute;
        desc.bindings.push_back(b);

        b.binding = 2;
        b.descriptorType = CatEngine::RHI::DescriptorType::StorageBuffer;
        b.descriptorCount = 1;
        b.stageFlags = CatEngine::RHI::ShaderStage::Compute;
        desc.bindings.push_back(b);

        b.binding = 3;
        b.descriptorType = CatEngine::RHI::DescriptorType::StorageBuffer;
        b.descriptorCount = 1;
        b.stageFlags = CatEngine::RHI::ShaderStage::Compute;
        desc.bindings.push_back(b);

        b.binding = 4;
        b.descriptorType = CatEngine::RHI::DescriptorType::StorageBuffer;
        b.descriptorCount = 1;
        b.stageFlags = CatEngine::RHI::ShaderStage::Compute;
        desc.bindings.push_back(b);

        m_descriptorSetLayout = m_device->CreateDescriptorSetLayout(desc);
        if (m_descriptorSetLayout == nullptr) {
            std::cerr << "[ClusteredLighting] CreateDescriptorSetLayout failed" << std::endl;
            return false;
        }
    }

    // 4) Pipeline layout wraps the single descriptor set layout.
    {
        CatEngine::RHI::IRHIDescriptorSetLayout* setLayouts[] = { m_descriptorSetLayout };
        m_pipelineLayout = m_device->CreatePipelineLayout(setLayouts, 1);
        if (m_pipelineLayout == nullptr) {
            std::cerr << "[ClusteredLighting] CreatePipelineLayout failed" << std::endl;
            return false;
        }
    }

    // 5) Compute pipeline.
    {
        CatEngine::RHI::ComputePipelineDesc desc{};
        desc.shader = m_computeShader;
        desc.debugName = "ClusteredLighting::computePipeline";
        m_computePipeline = m_device->CreateComputePipeline(desc);
        if (m_computePipeline == nullptr) {
            std::cerr << "[ClusteredLighting] CreateComputePipeline failed" << std::endl;
            return false;
        }
    }

    // 6) Descriptor pool + set for this pipeline.
    {
        m_descriptorPool = m_device->CreateDescriptorPool(/*maxSets=*/1);
        if (m_descriptorPool == nullptr) {
            std::cerr << "[ClusteredLighting] CreateDescriptorPool failed" << std::endl;
            return false;
        }

        m_descriptorSet = m_descriptorPool->AllocateDescriptorSet(m_descriptorSetLayout);
        if (m_descriptorSet == nullptr) {
            std::cerr << "[ClusteredLighting] AllocateDescriptorSet failed" << std::endl;
            return false;
        }
    }

    return true;
}

void ClusteredLighting::updateClusterParams() {
    m_clusterParams.screenDimensions = vec4(
        static_cast<float>(m_screenWidth),
        static_cast<float>(m_screenHeight),
        m_screenWidth  > 0 ? 1.0f / static_cast<float>(m_screenWidth)  : 0.0f,
        m_screenHeight > 0 ? 1.0f / static_cast<float>(m_screenHeight) : 0.0f
    );

    float logRatio = std::log(m_farPlane / m_nearPlane);
    float clusterScale = (logRatio != 0.0f)
        ? static_cast<float>(CLUSTER_GRID_Z) / logRatio
        : 0.0f;

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
    // Intentionally empty on the GPU path. m_gpuClusters is kept on the CPU
    // only — the CPU-side buildClusterGrid() helper is still useful for
    // debug visualisation and for getClusterFromScreenSpace(), but the
    // compute shader (clustered.comp) recomputes AABBs per workgroup from
    // camera params and does not read a pre-populated AABB buffer. The
    // previous implementation uploaded to m_clusterBuffer, which has been
    // removed (see createBuffers).
}

} // namespace Engine::Renderer

#pragma once

#include "RHITypes.hpp"
#include "RHIBuffer.hpp"
#include "RHITexture.hpp"
#include "RHIShader.hpp"
#include "RHIPipeline.hpp"
#include "RHICommandBuffer.hpp"
#include "RHIDescriptorSet.hpp"
#include "RHIRenderPass.hpp"
#include "RHISwapchain.hpp"
#include <cstdint>

namespace CatEngine::RHI {

/**
 * RHI device capabilities
 */
struct DeviceLimits {
    uint32_t maxImageDimension1D = 0;
    uint32_t maxImageDimension2D = 0;
    uint32_t maxImageDimension3D = 0;
    uint32_t maxImageDimensionCube = 0;
    uint32_t maxImageArrayLayers = 0;
    uint32_t maxTexelBufferElements = 0;
    uint32_t maxUniformBufferRange = 0;
    uint32_t maxStorageBufferRange = 0;
    uint32_t maxPushConstantsSize = 0;
    uint32_t maxMemoryAllocationCount = 0;
    uint32_t maxSamplerAllocationCount = 0;
    uint32_t maxBoundDescriptorSets = 0;
    uint32_t maxPerStageDescriptorSamplers = 0;
    uint32_t maxPerStageDescriptorUniformBuffers = 0;
    uint32_t maxPerStageDescriptorStorageBuffers = 0;
    uint32_t maxPerStageDescriptorSampledImages = 0;
    uint32_t maxPerStageDescriptorStorageImages = 0;
    uint32_t maxPerStageResources = 0;
    uint32_t maxDescriptorSetSamplers = 0;
    uint32_t maxDescriptorSetUniformBuffers = 0;
    uint32_t maxDescriptorSetStorageBuffers = 0;
    uint32_t maxDescriptorSetSampledImages = 0;
    uint32_t maxDescriptorSetStorageImages = 0;
    uint32_t maxVertexInputAttributes = 0;
    uint32_t maxVertexInputBindings = 0;
    uint32_t maxVertexInputAttributeOffset = 0;
    uint32_t maxVertexInputBindingStride = 0;
    uint32_t maxVertexOutputComponents = 0;
    uint32_t maxComputeSharedMemorySize = 0;
    uint32_t maxComputeWorkGroupCount[3] = {0, 0, 0};
    uint32_t maxComputeWorkGroupInvocations = 0;
    uint32_t maxComputeWorkGroupSize[3] = {0, 0, 0};
    uint32_t maxViewports = 0;
    uint32_t maxViewportDimensions[2] = {0, 0};
    uint32_t maxFramebufferWidth = 0;
    uint32_t maxFramebufferHeight = 0;
    uint32_t maxFramebufferLayers = 0;
    uint32_t maxColorAttachments = 0;
    float maxSamplerAnisotropy = 0.0f;
};

/**
 * RHI device features
 */
struct DeviceFeatures {
    bool geometryShader = false;
    bool tessellationShader = false;
    bool computeShader = false;
    bool multiDrawIndirect = false;
    bool drawIndirectFirstInstance = false;
    bool depthClamp = false;
    bool depthBiasClamp = false;
    bool fillModeNonSolid = false;
    bool depthBounds = false;
    bool wideLines = false;
    bool largePoints = false;
    bool alphaToOne = false;
    bool multiViewport = false;
    bool samplerAnisotropy = false;
    bool textureCompressionBC = false;
    bool occlusionQueryPrecise = false;
    bool pipelineStatisticsQuery = false;
    bool vertexPipelineStoresAndAtomics = false;
    bool fragmentStoresAndAtomics = false;
    bool shaderStorageImageExtendedFormats = false;
    bool shaderStorageImageMultisample = false;
    bool shaderUniformBufferArrayDynamicIndexing = false;
    bool shaderSampledImageArrayDynamicIndexing = false;
    bool shaderStorageBufferArrayDynamicIndexing = false;
    bool shaderStorageImageArrayDynamicIndexing = false;
};

/**
 * RHI initialization descriptor
 */
struct RHIDesc {
    const char* applicationName = "CatEngine Application";
    uint32_t applicationVersion = 1;
    bool enableValidation = false;
    bool enableGPUValidation = false;
    const char* const* requiredExtensions = nullptr;
    uint32_t requiredExtensionCount = 0;
};

/**
 * Abstract interface for RHI Device
 * Main factory for creating RHI resources
 */
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // ========================================================================
    // Device Information
    // ========================================================================

    /**
     * Get device name
     */
    virtual const char* GetDeviceName() const = 0;

    /**
     * Get device limits
     */
    virtual const DeviceLimits& GetLimits() const = 0;

    /**
     * Get device features
     */
    virtual const DeviceFeatures& GetFeatures() const = 0;

    // ========================================================================
    // Resource Creation
    // ========================================================================

    /**
     * Create buffer
     */
    virtual IRHIBuffer* CreateBuffer(const BufferDesc& desc) = 0;

    /**
     * Destroy buffer
     */
    virtual void DestroyBuffer(IRHIBuffer* buffer) = 0;

    /**
     * Map buffer memory for CPU access
     * @param buffer Buffer to map
     * @return Pointer to mapped memory, or nullptr on failure
     */
    virtual void* MapBuffer(IRHIBuffer* buffer) = 0;

    /**
     * Unmap buffer memory
     * @param buffer Buffer to unmap
     */
    virtual void UnmapBuffer(IRHIBuffer* buffer) = 0;

    /**
     * Create texture
     */
    virtual IRHITexture* CreateTexture(const TextureDesc& desc) = 0;

    /**
     * Destroy texture
     */
    virtual void DestroyTexture(IRHITexture* texture) = 0;

    /**
     * Create texture view
     * @param texture Source texture
     * @param format View format (Undefined = use texture format)
     * @param baseMipLevel Base mip level
     * @param mipLevelCount Number of mip levels (0 = all remaining)
     * @param baseArrayLayer Base array layer
     * @param arrayLayerCount Number of array layers (0 = all remaining)
     */
    virtual IRHITextureView* CreateTextureView(
        IRHITexture* texture,
        TextureFormat format = TextureFormat::Undefined,
        uint32_t baseMipLevel = 0,
        uint32_t mipLevelCount = 0,
        uint32_t baseArrayLayer = 0,
        uint32_t arrayLayerCount = 0
    ) = 0;

    /**
     * Destroy texture view
     */
    virtual void DestroyTextureView(IRHITextureView* view) = 0;

    /**
     * Create sampler
     */
    virtual IRHISampler* CreateSampler(const SamplerDesc& desc) = 0;

    /**
     * Destroy sampler
     */
    virtual void DestroySampler(IRHISampler* sampler) = 0;

    /**
     * Create shader
     */
    virtual IRHIShader* CreateShader(const ShaderDesc& desc) = 0;

    /**
     * Destroy shader
     */
    virtual void DestroyShader(IRHIShader* shader) = 0;

    /**
     * Create descriptor set layout
     */
    virtual IRHIDescriptorSetLayout* CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) = 0;

    /**
     * Destroy descriptor set layout
     */
    virtual void DestroyDescriptorSetLayout(IRHIDescriptorSetLayout* layout) = 0;

    /**
     * Create pipeline layout
     * @param setLayouts Array of descriptor set layouts
     * @param setLayoutCount Number of descriptor set layouts
     */
    virtual IRHIPipelineLayout* CreatePipelineLayout(
        IRHIDescriptorSetLayout** setLayouts,
        uint32_t setLayoutCount
    ) = 0;

    /**
     * Destroy pipeline layout
     */
    virtual void DestroyPipelineLayout(IRHIPipelineLayout* layout) = 0;

    /**
     * Create render pass
     */
    virtual IRHIRenderPass* CreateRenderPass(const RenderPassDesc& desc) = 0;

    /**
     * Destroy render pass
     */
    virtual void DestroyRenderPass(IRHIRenderPass* renderPass) = 0;

    /**
     * Create framebuffer
     * @param renderPass Compatible render pass
     * @param attachments Array of texture views
     * @param attachmentCount Number of attachments
     * @param width Framebuffer width
     * @param height Framebuffer height
     * @param layers Number of layers
     */
    virtual IRHIFramebuffer* CreateFramebuffer(
        IRHIRenderPass* renderPass,
        IRHITextureView** attachments,
        uint32_t attachmentCount,
        uint32_t width,
        uint32_t height,
        uint32_t layers = 1
    ) = 0;

    /**
     * Destroy framebuffer
     */
    virtual void DestroyFramebuffer(IRHIFramebuffer* framebuffer) = 0;

    /**
     * Create graphics pipeline
     */
    virtual IRHIPipeline* CreateGraphicsPipeline(const PipelineDesc& desc) = 0;

    /**
     * Create compute pipeline
     */
    virtual IRHIPipeline* CreateComputePipeline(const ComputePipelineDesc& desc) = 0;

    /**
     * Destroy pipeline
     */
    virtual void DestroyPipeline(IRHIPipeline* pipeline) = 0;

    /**
     * Create descriptor pool
     * @param maxSets Maximum number of descriptor sets
     */
    virtual IRHIDescriptorPool* CreateDescriptorPool(uint32_t maxSets) = 0;

    /**
     * Destroy descriptor pool
     */
    virtual void DestroyDescriptorPool(IRHIDescriptorPool* pool) = 0;

    /**
     * Destroy descriptor set
     * @param descriptorSet Descriptor set to destroy
     */
    virtual void DestroyDescriptorSet(IRHIDescriptorSet* descriptorSet) = 0;

    /**
     * Create command buffer
     */
    virtual IRHICommandBuffer* CreateCommandBuffer() = 0;

    /**
     * Destroy command buffer
     */
    virtual void DestroyCommandBuffer(IRHICommandBuffer* commandBuffer) = 0;

    /**
     * Create swapchain
     */
    virtual IRHISwapchain* CreateSwapchain(const SwapchainDesc& desc) = 0;

    /**
     * Destroy swapchain
     */
    virtual void DestroySwapchain(IRHISwapchain* swapchain) = 0;

    // ========================================================================
    // Command Submission
    // ========================================================================

    /**
     * Submit command buffers for execution
     * @param commandBuffers Array of command buffers
     * @param count Number of command buffers
     */
    virtual void Submit(IRHICommandBuffer** commandBuffers, uint32_t count) = 0;

    /**
     * Wait for all GPU operations to complete
     */
    virtual void WaitIdle() = 0;

    // ========================================================================
    // Synchronization
    // ========================================================================

    /**
     * Begin new frame
     */
    virtual void BeginFrame() = 0;

    /**
     * End current frame
     */
    virtual void EndFrame() = 0;
};

/**
 * Create RHI device instance
 * @param desc RHI initialization descriptor
 * @return RHI device instance or nullptr on failure
 */
IRHIDevice* CreateRHIDevice(const RHIDesc& desc);

/**
 * Destroy RHI device instance
 */
void DestroyRHIDevice(IRHIDevice* device);

// Type alias for backward compatibility
using IRHI = IRHIDevice;

} // namespace CatEngine::RHI

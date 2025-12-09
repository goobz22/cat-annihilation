#pragma once

#include "../RHI.hpp"
#include "VulkanDevice.hpp"
#include "VulkanDebug.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>

namespace CatEngine::RHI {
class VulkanCommandPool;
}

namespace CatEngine::RHI {

/**
 * Vulkan RHI device implementation
 * Main factory for creating RHI resources using Vulkan backend
 */
class VulkanRHI : public IRHIDevice {
public:
    VulkanRHI();
    ~VulkanRHI() override;

    // Disable copy
    VulkanRHI(const VulkanRHI&) = delete;
    VulkanRHI& operator=(const VulkanRHI&) = delete;

    /**
     * Initialize Vulkan RHI
     * @param desc Initialization descriptor
     * @return true on success
     */
    bool Initialize(const RHIDesc& desc);

    /**
     * Initialize device with surface (must be called after Initialize)
     * @param surface Vulkan surface for presentation
     * @return true on success
     */
    bool InitializeDevice(VkSurfaceKHR surface);

    /**
     * Shutdown and cleanup all resources
     */
    void Shutdown();

    // ========================================================================
    // IRHIDevice Interface - Device Information
    // ========================================================================

    const char* GetDeviceName() const override;
    const DeviceLimits& GetLimits() const override;
    const DeviceFeatures& GetFeatures() const override;

    // ========================================================================
    // IRHIDevice Interface - Resource Creation
    // ========================================================================

    IRHIBuffer* CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(IRHIBuffer* buffer) override;
    void* MapBuffer(IRHIBuffer* buffer) override;
    void UnmapBuffer(IRHIBuffer* buffer) override;

    IRHITexture* CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(IRHITexture* texture) override;

    IRHITextureView* CreateTextureView(
        IRHITexture* texture,
        TextureFormat format = TextureFormat::Undefined,
        uint32_t baseMipLevel = 0,
        uint32_t mipLevelCount = 0,
        uint32_t baseArrayLayer = 0,
        uint32_t arrayLayerCount = 0
    ) override;
    void DestroyTextureView(IRHITextureView* view) override;

    IRHISampler* CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(IRHISampler* sampler) override;

    IRHIShader* CreateShader(const ShaderDesc& desc) override;
    void DestroyShader(IRHIShader* shader) override;

    IRHIDescriptorSetLayout* CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
    void DestroyDescriptorSetLayout(IRHIDescriptorSetLayout* layout) override;

    IRHIPipelineLayout* CreatePipelineLayout(
        IRHIDescriptorSetLayout** setLayouts,
        uint32_t setLayoutCount
    ) override;
    void DestroyPipelineLayout(IRHIPipelineLayout* layout) override;

    IRHIRenderPass* CreateRenderPass(const RenderPassDesc& desc) override;
    void DestroyRenderPass(IRHIRenderPass* renderPass) override;

    IRHIFramebuffer* CreateFramebuffer(
        IRHIRenderPass* renderPass,
        IRHITextureView** attachments,
        uint32_t attachmentCount,
        uint32_t width,
        uint32_t height,
        uint32_t layers = 1
    ) override;
    void DestroyFramebuffer(IRHIFramebuffer* framebuffer) override;

    IRHIPipeline* CreateGraphicsPipeline(const PipelineDesc& desc) override;
    IRHIPipeline* CreateComputePipeline(const ComputePipelineDesc& desc) override;
    void DestroyPipeline(IRHIPipeline* pipeline) override;

    IRHIDescriptorPool* CreateDescriptorPool(uint32_t maxSets) override;
    void DestroyDescriptorPool(IRHIDescriptorPool* pool) override;
    void DestroyDescriptorSet(IRHIDescriptorSet* descriptorSet) override;

    IRHICommandBuffer* CreateCommandBuffer() override;
    void DestroyCommandBuffer(IRHICommandBuffer* commandBuffer) override;

    IRHISwapchain* CreateSwapchain(const SwapchainDesc& desc) override;
    void DestroySwapchain(IRHISwapchain* swapchain) override;

    // ========================================================================
    // IRHIDevice Interface - Command Submission
    // ========================================================================

    void Submit(IRHICommandBuffer** commandBuffers, uint32_t count) override;
    void WaitIdle() override;

    // ========================================================================
    // IRHIDevice Interface - Synchronization
    // ========================================================================

    void BeginFrame() override;
    void EndFrame() override;

    // ========================================================================
    // Vulkan-Specific
    // ========================================================================

    VkInstance GetInstance() const { return m_instance; }
    VulkanDevice* GetDevice() { return &m_device; }
    const VulkanDevice* GetDevice() const { return &m_device; }

private:
    /**
     * Create Vulkan instance
     */
    bool CreateInstance(const RHIDesc& desc);

    /**
     * Get required instance extensions
     */
    std::vector<const char*> GetRequiredExtensions(const RHIDesc& desc);

    /**
     * Check validation layer support
     */
    bool CheckValidationLayerSupport();

    /**
     * Setup debug messenger
     */
    void SetupDebugMessenger();

    // Vulkan instance
    VkInstance m_instance = VK_NULL_HANDLE;

    // Debug messenger
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    bool m_validationEnabled = false;

    // Device
    VulkanDevice m_device;

    // Frame management
    uint32_t m_frameIndex = 0;

    // Command pool for creating command buffers
    std::unique_ptr<VulkanCommandPool> m_commandPool;

    // Validation layers
    static const std::vector<const char*> s_validationLayers;
};

} // namespace CatEngine::RHI

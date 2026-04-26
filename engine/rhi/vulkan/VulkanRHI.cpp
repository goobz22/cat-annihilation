#include "VulkanRHI.hpp"
#include "VulkanSwapchain.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanShader.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanDescriptor.hpp"
#include "VulkanRenderPass.hpp"
#include "VulkanCommandBuffer.hpp"
#include "../../core/Window.hpp"
#include <iostream>
#include <set>
#include <cstring>
#include <GLFW/glfw3.h>

namespace CatEngine::RHI {

// Validation layers
const std::vector<const char*> VulkanRHI::s_validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

VulkanRHI::VulkanRHI() = default;

VulkanRHI::~VulkanRHI() {
    Shutdown();
}

bool VulkanRHI::Initialize(const RHIDesc& desc) {
    // Create Vulkan instance
    if (!CreateInstance(desc)) {
        std::cerr << "[VulkanRHI] Failed to create Vulkan instance" << std::endl;
        return false;
    }

    // Setup debug messenger if validation is enabled
    if (desc.enableValidation) {
        SetupDebugMessenger();
    }

    std::cout << "[VulkanRHI] Vulkan instance created successfully" << std::endl;
    return true;
}

bool VulkanRHI::InitializeDevice(VkSurfaceKHR surface) {
    // Initialize device with surface
    if (!m_device.Initialize(m_instance, surface, m_validationEnabled)) {
        std::cerr << "[VulkanRHI] Failed to initialize Vulkan device" << std::endl;
        return false;
    }

    std::cout << "[VulkanRHI] Device initialized: " << GetDeviceName() << std::endl;
    return true;
}

void VulkanRHI::Shutdown() {
    // Wait for device to finish — nothing must still be in-flight when we
    // start tearing the device down.
    if (m_device.GetDevice() != VK_NULL_HANDLE) {
        m_device.WaitIdle();
    }

    // ------------------------------------------------------------------
    // Ordering note: the RHI-owned command pool MUST be destroyed while
    // the VkDevice is still live.
    //
    // m_commandPool is a std::unique_ptr<VulkanCommandPool>. Its destructor
    // eventually calls vkDestroyCommandPool(m_device->GetDevice(), ...).
    // Member destructors in the enclosing VulkanRHI run in reverse
    // declaration order *after* this Shutdown() returns, which means that
    // without this explicit reset() the pool would otherwise be torn down
    // only after m_device.Shutdown() has already set the VkDevice handle
    // to VK_NULL_HANDLE — producing the "vkDestroyCommandPool: Invalid
    // device [VUID-vkDestroyCommandPool-device-parameter]" validation
    // error seen at shutdown. Resetting here keeps the teardown order
    // pool-before-device, matching VulkanDevice::Shutdown() itself (which
    // tears down its own internal m_commandPool before destroying the
    // VkDevice handle on line 39-46 of VulkanDevice.cpp).
    // ------------------------------------------------------------------
    m_commandPool.reset();

    // Cleanup device (destroys VkDevice; after this every VkDevice handle
    // derived from it is invalid).
    m_device.Shutdown();

    // Cleanup debug messenger
    if (m_debugMessenger != VK_NULL_HANDLE) {
        VulkanDebug::DestroyDebugMessenger(m_instance, m_debugMessenger);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    // Cleanup instance
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

const char* VulkanRHI::GetDeviceName() const {
    return m_device.GetDeviceName();
}

const DeviceLimits& VulkanRHI::GetLimits() const {
    return m_device.GetRHILimits();
}

const DeviceFeatures& VulkanRHI::GetFeatures() const {
    return m_device.GetRHIFeatures();
}

IRHIBuffer* VulkanRHI::CreateBuffer(const BufferDesc& desc) {
    auto* buffer = new VulkanBuffer(&m_device, desc);
    return buffer;
}

void VulkanRHI::DestroyBuffer(IRHIBuffer* buffer) {
    delete buffer;
}

void* VulkanRHI::MapBuffer(IRHIBuffer* buffer) {
    if (!buffer) {
        return nullptr;
    }
    return buffer->Map();
}

void VulkanRHI::UnmapBuffer(IRHIBuffer* buffer) {
    if (!buffer) {
        return;
    }
    buffer->Unmap();
}

IRHITexture* VulkanRHI::CreateTexture(const TextureDesc& desc) {
    auto* texture = new VulkanTexture(&m_device, desc);
    return texture;
}

void VulkanRHI::DestroyTexture(IRHITexture* texture) {
    delete texture;
}

IRHITextureView* VulkanRHI::CreateTextureView(
    IRHITexture* texture,
    TextureFormat format,
    uint32_t baseMipLevel,
    uint32_t mipLevelCount,
    uint32_t baseArrayLayer,
    uint32_t arrayLayerCount
) {
    if (!texture) {
        std::cerr << "[VulkanRHI] CreateTextureView: null texture" << std::endl;
        return nullptr;
    }
    auto* vulkanTexture = static_cast<VulkanTexture*>(texture);

    // Use texture's format if not specified
    if (format == TextureFormat::Undefined) {
        format = texture->GetFormat();
    }
    // Use all remaining mip levels if not specified
    if (mipLevelCount == 0) {
        mipLevelCount = texture->GetMipLevels() - baseMipLevel;
    }
    // Use all remaining array layers if not specified
    if (arrayLayerCount == 0) {
        arrayLayerCount = texture->GetArrayLayers() - baseArrayLayer;
    }

    auto* view = new VulkanTextureView(&m_device, vulkanTexture, format,
                                        baseMipLevel, mipLevelCount,
                                        baseArrayLayer, arrayLayerCount);
    return view;
}

void VulkanRHI::DestroyTextureView(IRHITextureView* view) {
    delete view;
}

IRHISampler* VulkanRHI::CreateSampler(const SamplerDesc& desc) {
    auto* sampler = new VulkanSampler(&m_device, desc);
    return sampler;
}

void VulkanRHI::DestroySampler(IRHISampler* sampler) {
    delete sampler;
}

IRHIShader* VulkanRHI::CreateShader(const ShaderDesc& desc) {
    auto* shader = new VulkanShader(&m_device, desc);
    return shader;
}

void VulkanRHI::DestroyShader(IRHIShader* shader) {
    delete shader;
}

IRHIDescriptorSetLayout* VulkanRHI::CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    auto* layout = new VulkanDescriptorSetLayout(&m_device, desc);
    return layout;
}

void VulkanRHI::DestroyDescriptorSetLayout(IRHIDescriptorSetLayout* layout) {
    delete layout;
}

IRHIPipelineLayout* VulkanRHI::CreatePipelineLayout(
    IRHIDescriptorSetLayout** setLayouts,
    uint32_t setLayoutCount
) {
    std::vector<IRHIDescriptorSetLayout*> layouts;
    if (setLayouts && setLayoutCount > 0) {
        layouts.assign(setLayouts, setLayouts + setLayoutCount);
    }
    std::vector<VulkanPipelineLayout::PushConstantRange> pushConstants;
    auto* layout = new VulkanPipelineLayout(&m_device, layouts, pushConstants);
    return layout;
}

void VulkanRHI::DestroyPipelineLayout(IRHIPipelineLayout* layout) {
    delete layout;
}

IRHIRenderPass* VulkanRHI::CreateRenderPass(const RenderPassDesc& desc) {
    auto* renderPass = new VulkanRenderPass(&m_device, desc);
    return renderPass;
}

void VulkanRHI::DestroyRenderPass(IRHIRenderPass* renderPass) {
    delete renderPass;
}

IRHIFramebuffer* VulkanRHI::CreateFramebuffer(
    IRHIRenderPass* renderPass,
    IRHITextureView** attachments,
    uint32_t attachmentCount,
    uint32_t width,
    uint32_t height,
    uint32_t layers
) {
    if (!renderPass) {
        std::cerr << "[VulkanRHI] CreateFramebuffer: null render pass" << std::endl;
        return nullptr;
    }
    auto* vulkanRenderPass = static_cast<VulkanRenderPass*>(renderPass);
    auto* framebuffer = new VulkanFramebuffer(&m_device, vulkanRenderPass,
                                               attachments, attachmentCount,
                                               width, height, layers);
    return framebuffer;
}

void VulkanRHI::DestroyFramebuffer(IRHIFramebuffer* framebuffer) {
    delete framebuffer;
}

IRHIPipeline* VulkanRHI::CreateGraphicsPipeline(const PipelineDesc& desc) {
    // If the caller supplied a pipeline layout (the usual case for passes
    // that bind descriptors), route through the layout-aware constructor
    // so the VkPipeline is created against that layout instead of an empty
    // default. An empty default pipeline layout compiles fine but produces
    // validation errors the moment any BindDescriptorSets call arrives
    // against the pipeline — the default declares zero descriptor sets.
    if (desc.pipelineLayout != nullptr) {
        auto* layout = static_cast<VulkanPipelineLayout*>(desc.pipelineLayout);
        auto* pipeline = new VulkanGraphicsPipeline(&m_device, desc, layout);
        return pipeline;
    }
    auto* pipeline = new VulkanGraphicsPipeline(&m_device, desc);
    return pipeline;
}

IRHIPipeline* VulkanRHI::CreateComputePipeline(const ComputePipelineDesc& desc) {
    // Same layout contract as CreateGraphicsPipeline: use the caller's
    // layout when provided so BindDescriptorSets in compute dispatches
    // resolves against the correct set layouts instead of an empty default.
    if (desc.pipelineLayout != nullptr) {
        auto* layout = static_cast<VulkanPipelineLayout*>(desc.pipelineLayout);
        auto* pipeline = new VulkanComputePipeline(&m_device, desc, layout);
        return pipeline;
    }
    auto* pipeline = new VulkanComputePipeline(&m_device, desc);
    return pipeline;
}

void VulkanRHI::DestroyPipeline(IRHIPipeline* pipeline) {
    delete pipeline;
}

IRHIDescriptorPool* VulkanRHI::CreateDescriptorPool(uint32_t maxSets) {
    auto poolSizes = VulkanDescriptorHelper::CreateDefaultPoolSizes(maxSets);
    auto* pool = new VulkanDescriptorPool(&m_device, maxSets, poolSizes);
    return pool;
}

void VulkanRHI::DestroyDescriptorPool(IRHIDescriptorPool* pool) {
    delete pool;
}

void VulkanRHI::DestroyDescriptorSet(IRHIDescriptorSet* descriptorSet) {
    // Descriptor sets are typically managed by their pool
    // Individual destruction is handled through the pool's FreeDescriptorSet method
    if (descriptorSet) {
        // The descriptor set will be freed when the pool is destroyed or reset
        // For explicit freeing, the caller should use the pool's FreeDescriptorSet
    }
}

IRHICommandBuffer* VulkanRHI::CreateCommandBuffer() {
    // Create a command pool if we don't have one for the graphics queue
    if (!m_commandPool) {
        m_commandPool = std::make_unique<VulkanCommandPool>(
            &m_device, m_device.GetQueueFamilyIndices().graphics.value()
        );
    }
    auto* commandBuffer = new VulkanCommandBuffer(&m_device, m_commandPool.get());
    return commandBuffer;
}

void VulkanRHI::DestroyCommandBuffer(IRHICommandBuffer* commandBuffer) {
    delete commandBuffer;
}

IRHISwapchain* VulkanRHI::CreateSwapchain(const SwapchainDesc& desc) {
    // Create surface from window handle
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    if (desc.windowHandle) {
        // Assuming windowHandle is a pointer to Engine::Window
        auto window = static_cast<Engine::Window*>(desc.windowHandle);
        surface = window->createVulkanSurface(m_instance);

        if (surface == VK_NULL_HANDLE) {
            std::cerr << "[VulkanRHI] Failed to create Vulkan surface" << std::endl;
            return nullptr;
        }
    } else {
        std::cerr << "[VulkanRHI] Invalid window handle" << std::endl;
        return nullptr;
    }

    // Initialize device with surface if not already initialized
    if (m_device.GetDevice() == VK_NULL_HANDLE) {
        if (!InitializeDevice(surface)) {
            std::cerr << "[VulkanRHI] Failed to initialize device for swapchain" << std::endl;
            return nullptr;
        }
    }

    // Create swapchain (pass instance so the surface can be destroyed at shutdown)
    VulkanSwapchain* swapchain = new VulkanSwapchain(&m_device, m_instance, surface, desc);
    return swapchain;
}

void VulkanRHI::DestroySwapchain(IRHISwapchain* swapchain) {
    delete swapchain;
}

void VulkanRHI::Submit(IRHICommandBuffer** commandBuffers, uint32_t count) {
    if (!commandBuffers || count == 0) {
        return;
    }

    // Collect Vulkan command buffers
    std::vector<VkCommandBuffer> vkCommandBuffers;
    vkCommandBuffers.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (commandBuffers[i]) {
            auto* vulkanCmdBuffer = static_cast<VulkanCommandBuffer*>(commandBuffers[i]);
            vkCommandBuffers.push_back(vulkanCmdBuffer->GetHandle());
        }
    }

    if (vkCommandBuffers.empty()) {
        return;
    }

    // Submit to graphics queue
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<uint32_t>(vkCommandBuffers.size());
    submitInfo.pCommandBuffers = vkCommandBuffers.data();

    VkResult result = vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        std::cerr << "[VulkanRHI] Failed to submit command buffers: " << result << std::endl;
    }
}

void VulkanRHI::WaitIdle() {
    m_device.WaitIdle();
}

void VulkanRHI::BeginFrame() {
    m_frameIndex++;
}

void VulkanRHI::EndFrame() {
    // Frame management logic
}

bool VulkanRHI::CreateInstance(const RHIDesc& desc) {
    m_validationEnabled = desc.enableValidation;

    // Check validation layer support
    if (m_validationEnabled && !CheckValidationLayerSupport()) {
        std::cerr << "[VulkanRHI] Validation layers requested but not available" << std::endl;
        return false;
    }

    // Application info
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = desc.applicationName;
    appInfo.applicationVersion = desc.applicationVersion;
    appInfo.pEngineName = "CatEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Get required extensions
    std::vector<const char*> extensions = GetRequiredExtensions(desc);

    // Instance create info
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (m_validationEnabled) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(s_validationLayers.size());
        createInfo.ppEnabledLayerNames = s_validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // Create instance
    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        std::cerr << "[VulkanRHI] Failed to create Vulkan instance: " << result << std::endl;
        return false;
    }

    return true;
}

std::vector<const char*> VulkanRHI::GetRequiredExtensions(const RHIDesc& desc) {
    // Get GLFW required extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    // Add debug utils extension if validation is enabled
    if (desc.enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Add user-requested extensions
    if (desc.requiredExtensions && desc.requiredExtensionCount > 0) {
        for (uint32_t i = 0; i < desc.requiredExtensionCount; i++) {
            extensions.push_back(desc.requiredExtensions[i]);
        }
    }

    return extensions;
}

bool VulkanRHI::CheckValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : s_validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (std::strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

void VulkanRHI::SetupDebugMessenger() {
    m_debugMessenger = VulkanDebug::CreateDebugMessenger(m_instance);

    if (m_debugMessenger == VK_NULL_HANDLE) {
        std::cerr << "[VulkanRHI] Failed to setup debug messenger" << std::endl;
    } else {
        std::cout << "[VulkanRHI] Debug messenger enabled" << std::endl;
    }
}

} // namespace CatEngine::RHI

// ============================================================================
// Factory Functions
// ============================================================================

namespace CatEngine::RHI {

IRHIDevice* CreateRHIDevice(const RHIDesc& desc) {
    auto* device = new VulkanRHI();
    if (!device->Initialize(desc)) {
        delete device;
        return nullptr;
    }
    return device;
}

void DestroyRHIDevice(IRHIDevice* device) {
    delete device;
}

} // namespace CatEngine::RHI

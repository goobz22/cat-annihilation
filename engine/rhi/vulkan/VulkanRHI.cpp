#include "VulkanRHI.hpp"
#include "VulkanSwapchain.hpp"
#include "../../core/Window.hpp"
#include <iostream>
#include <set>
#include <cstring>
#include <GLFW/glfw3.h>

namespace CatEngine::RHI::Vulkan {

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
    // Wait for device to finish
    if (m_device.GetDevice() != VK_NULL_HANDLE) {
        m_device.WaitIdle();
    }

    // Cleanup device
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
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateBuffer not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyBuffer(IRHIBuffer* buffer) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyBuffer not yet implemented" << std::endl;
}

IRHITexture* VulkanRHI::CreateTexture(const TextureDesc& desc) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateTexture not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyTexture(IRHITexture* texture) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyTexture not yet implemented" << std::endl;
}

IRHITextureView* VulkanRHI::CreateTextureView(
    IRHITexture* texture,
    TextureFormat format,
    uint32_t baseMipLevel,
    uint32_t mipLevelCount,
    uint32_t baseArrayLayer,
    uint32_t arrayLayerCount
) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateTextureView not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyTextureView(IRHITextureView* view) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyTextureView not yet implemented" << std::endl;
}

IRHISampler* VulkanRHI::CreateSampler(const SamplerDesc& desc) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateSampler not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroySampler(IRHISampler* sampler) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroySampler not yet implemented" << std::endl;
}

IRHIShader* VulkanRHI::CreateShader(const ShaderDesc& desc) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateShader not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyShader(IRHIShader* shader) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyShader not yet implemented" << std::endl;
}

IRHIDescriptorSetLayout* VulkanRHI::CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateDescriptorSetLayout not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyDescriptorSetLayout(IRHIDescriptorSetLayout* layout) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyDescriptorSetLayout not yet implemented" << std::endl;
}

IRHIPipelineLayout* VulkanRHI::CreatePipelineLayout(
    IRHIDescriptorSetLayout** setLayouts,
    uint32_t setLayoutCount
) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreatePipelineLayout not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyPipelineLayout(IRHIPipelineLayout* layout) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyPipelineLayout not yet implemented" << std::endl;
}

IRHIRenderPass* VulkanRHI::CreateRenderPass(const RenderPassDesc& desc) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateRenderPass not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyRenderPass(IRHIRenderPass* renderPass) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyRenderPass not yet implemented" << std::endl;
}

IRHIFramebuffer* VulkanRHI::CreateFramebuffer(
    IRHIRenderPass* renderPass,
    IRHITextureView** attachments,
    uint32_t attachmentCount,
    uint32_t width,
    uint32_t height,
    uint32_t layers
) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateFramebuffer not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyFramebuffer(IRHIFramebuffer* framebuffer) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyFramebuffer not yet implemented" << std::endl;
}

IRHIPipeline* VulkanRHI::CreateGraphicsPipeline(const PipelineDesc& desc) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateGraphicsPipeline not yet implemented" << std::endl;
    return nullptr;
}

IRHIPipeline* VulkanRHI::CreateComputePipeline(const ComputePipelineDesc& desc) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateComputePipeline not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyPipeline(IRHIPipeline* pipeline) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyPipeline not yet implemented" << std::endl;
}

IRHIDescriptorPool* VulkanRHI::CreateDescriptorPool(uint32_t maxSets) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateDescriptorPool not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyDescriptorPool(IRHIDescriptorPool* pool) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyDescriptorPool not yet implemented" << std::endl;
}

IRHICommandBuffer* VulkanRHI::CreateCommandBuffer() {
    // TODO: Implement
    std::cerr << "[VulkanRHI] CreateCommandBuffer not yet implemented" << std::endl;
    return nullptr;
}

void VulkanRHI::DestroyCommandBuffer(IRHICommandBuffer* commandBuffer) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] DestroyCommandBuffer not yet implemented" << std::endl;
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

    // Create swapchain
    VulkanSwapchain* swapchain = new VulkanSwapchain(&m_device, surface, desc);
    return swapchain;
}

void VulkanRHI::DestroySwapchain(IRHISwapchain* swapchain) {
    delete swapchain;
}

void VulkanRHI::Submit(IRHICommandBuffer** commandBuffers, uint32_t count) {
    // TODO: Implement
    std::cerr << "[VulkanRHI] Submit not yet implemented" << std::endl;
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

} // namespace CatEngine::RHI::Vulkan

// ============================================================================
// Factory Functions
// ============================================================================

namespace CatEngine::RHI {

IRHIDevice* CreateRHIDevice(const RHIDesc& desc) {
    auto* device = new Vulkan::VulkanRHI();
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

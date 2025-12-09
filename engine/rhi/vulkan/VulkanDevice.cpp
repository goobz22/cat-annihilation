#include "VulkanDevice.hpp"
#include "VulkanDebug.hpp"
#include <iostream>
#include <set>
#include <algorithm>

namespace CatEngine::RHI {

// Required device extensions
const std::vector<const char*> VulkanDevice::s_deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

VulkanDevice::~VulkanDevice() {
    Shutdown();
}

bool VulkanDevice::Initialize(VkInstance instance, VkSurfaceKHR surface, bool enableValidation) {
    // Select physical device
    if (!SelectPhysicalDevice(instance, surface)) {
        std::cerr << "[VulkanDevice] Failed to select physical device" << std::endl;
        return false;
    }

    // Query device properties
    QueryDeviceProperties();

    // Create logical device
    if (!CreateLogicalDevice(enableValidation)) {
        std::cerr << "[VulkanDevice] Failed to create logical device" << std::endl;
        return false;
    }

    std::cout << "[VulkanDevice] Successfully initialized device: " << GetDeviceName() << std::endl;
    return true;
}

void VulkanDevice::Shutdown() {
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    m_physicalDevice = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_computeQueue = VK_NULL_HANDLE;
    m_transferQueue = VK_NULL_HANDLE;
}

void VulkanDevice::WaitIdle() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkSurfaceCapabilitiesKHR VulkanDevice::GetSurfaceCapabilities(VkSurfaceKHR surface) const {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, surface, &capabilities);
    return capabilities;
}

std::vector<VkSurfaceFormatKHR> VulkanDevice::GetSurfaceFormats(VkSurfaceKHR surface) const {
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, nullptr);

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, formats.data());
    }
    return formats;
}

std::vector<VkPresentModeKHR> VulkanDevice::GetSurfacePresentModes(VkSurfaceKHR surface) const {
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, surface, &presentModeCount, nullptr);

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, surface, &presentModeCount, presentModes.data());
    }
    return presentModes;
}

bool VulkanDevice::SelectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cerr << "[VulkanDevice] Failed to find GPUs with Vulkan support" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Rate devices and select the best one
    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (const auto& device : devices) {
        if (IsDeviceSuitable(device, surface)) {
            int score = RateDeviceSuitability(device);
            if (score > bestScore) {
                bestScore = score;
                bestDevice = device;
            }
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        std::cerr << "[VulkanDevice] Failed to find suitable GPU" << std::endl;
        return false;
    }

    m_physicalDevice = bestDevice;
    m_queueFamilyIndices = FindQueueFamilies(bestDevice, surface);

    return true;
}

bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    // Check queue families
    QueueFamilyIndices indices = FindQueueFamilies(device, surface);
    if (!indices.IsComplete()) {
        return false;
    }

    // Check extension support
    if (!CheckDeviceExtensionSupport(device)) {
        return false;
    }

    // Check swapchain support
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (formatCount == 0 || presentModeCount == 0) {
        return false;
    }

    // Check Vulkan 1.3 support
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);

    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return false;
    }

    return true;
}

int VulkanDevice::RateDeviceSuitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(device, &properties);
    vkGetPhysicalDeviceFeatures(device, &features);

    int score = 0;

    // Discrete GPUs have a significant performance advantage
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 10000;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 1000;
    }

    // Maximum possible size of textures affects graphics quality
    score += properties.limits.maxImageDimension2D;

    // Prefer devices with geometry shader support
    if (features.geometryShader) {
        score += 100;
    }

    // Prefer devices with anisotropic filtering
    if (features.samplerAnisotropy) {
        score += 50;
    }

    return score;
}

QueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        const auto& queueFamily = queueFamilies[i];

        // Graphics queue (also check for presentation support)
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

            if (presentSupport) {
                indices.graphics = i;
            }
        }

        // Compute queue (prefer dedicated compute queue)
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            if (!indices.compute.has_value()) {
                indices.compute = i;
            } else if (!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                // Prefer dedicated compute queue
                indices.compute = i;
            }
        }

        // Transfer queue (prefer dedicated transfer queue)
        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
            if (!indices.transfer.has_value()) {
                indices.transfer = i;
            } else if (!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                // Prefer dedicated transfer queue
                indices.transfer = i;
            }
        }
    }

    return indices;
}

bool VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(s_deviceExtensions.begin(), s_deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

bool VulkanDevice::CreateLogicalDevice(bool enableValidation) {
    // Get unique queue family indices
    std::vector<uint32_t> uniqueQueueFamilies = m_queueFamilyIndices.GetUniqueIndices();

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Device features
    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.geometryShader = VK_TRUE;
    deviceFeatures.tessellationShader = VK_TRUE;

    // Create device
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(s_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = s_deviceExtensions.data();

    // Validation layers (deprecated for device, but set for compatibility)
    if (enableValidation) {
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        std::cerr << "[VulkanDevice] Failed to create logical device" << std::endl;
        return false;
    }

    // Get queues
    vkGetDeviceQueue(m_device, *m_queueFamilyIndices.graphics, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, *m_queueFamilyIndices.compute, 0, &m_computeQueue);
    vkGetDeviceQueue(m_device, *m_queueFamilyIndices.transfer, 0, &m_transferQueue);

    // Create command pool for one-time commands
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = *m_queueFamilyIndices.graphics;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        std::cerr << "[VulkanDevice] Failed to create command pool" << std::endl;
    }

    // Check for debug utils support
    m_debugUtilsSupported = enableValidation;

    return true;
}

void VulkanDevice::QueryDeviceProperties() {
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &m_deviceFeatures);
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);

    // Get device UUID
    VkPhysicalDeviceIDProperties idProperties = {};
    idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 properties2 = {};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &idProperties;

    vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2);

    std::copy(std::begin(idProperties.deviceUUID), std::end(idProperties.deviceUUID), m_deviceUUID.begin());

    // Populate RHI abstractions
    PopulateRHILimits();
    PopulateRHIFeatures();
}

void VulkanDevice::PopulateRHILimits() {
    const auto& limits = m_deviceProperties.limits;

    m_rhiLimits.maxImageDimension1D = limits.maxImageDimension1D;
    m_rhiLimits.maxImageDimension2D = limits.maxImageDimension2D;
    m_rhiLimits.maxImageDimension3D = limits.maxImageDimension3D;
    m_rhiLimits.maxImageDimensionCube = limits.maxImageDimensionCube;
    m_rhiLimits.maxImageArrayLayers = limits.maxImageArrayLayers;
    m_rhiLimits.maxTexelBufferElements = limits.maxTexelBufferElements;
    m_rhiLimits.maxUniformBufferRange = limits.maxUniformBufferRange;
    m_rhiLimits.maxStorageBufferRange = limits.maxStorageBufferRange;
    m_rhiLimits.maxPushConstantsSize = limits.maxPushConstantsSize;
    m_rhiLimits.maxMemoryAllocationCount = limits.maxMemoryAllocationCount;
    m_rhiLimits.maxSamplerAllocationCount = limits.maxSamplerAllocationCount;
    m_rhiLimits.maxBoundDescriptorSets = limits.maxBoundDescriptorSets;
    m_rhiLimits.maxPerStageDescriptorSamplers = limits.maxPerStageDescriptorSamplers;
    m_rhiLimits.maxPerStageDescriptorUniformBuffers = limits.maxPerStageDescriptorUniformBuffers;
    m_rhiLimits.maxPerStageDescriptorStorageBuffers = limits.maxPerStageDescriptorStorageBuffers;
    m_rhiLimits.maxPerStageDescriptorSampledImages = limits.maxPerStageDescriptorSampledImages;
    m_rhiLimits.maxPerStageDescriptorStorageImages = limits.maxPerStageDescriptorStorageImages;
    m_rhiLimits.maxPerStageResources = limits.maxPerStageResources;
    m_rhiLimits.maxDescriptorSetSamplers = limits.maxDescriptorSetSamplers;
    m_rhiLimits.maxDescriptorSetUniformBuffers = limits.maxDescriptorSetUniformBuffers;
    m_rhiLimits.maxDescriptorSetStorageBuffers = limits.maxDescriptorSetStorageBuffers;
    m_rhiLimits.maxDescriptorSetSampledImages = limits.maxDescriptorSetSampledImages;
    m_rhiLimits.maxDescriptorSetStorageImages = limits.maxDescriptorSetStorageImages;
    m_rhiLimits.maxVertexInputAttributes = limits.maxVertexInputAttributes;
    m_rhiLimits.maxVertexInputBindings = limits.maxVertexInputBindings;
    m_rhiLimits.maxVertexInputAttributeOffset = limits.maxVertexInputAttributeOffset;
    m_rhiLimits.maxVertexInputBindingStride = limits.maxVertexInputBindingStride;
    m_rhiLimits.maxVertexOutputComponents = limits.maxVertexOutputComponents;
    m_rhiLimits.maxComputeSharedMemorySize = limits.maxComputeSharedMemorySize;
    m_rhiLimits.maxComputeWorkGroupCount[0] = limits.maxComputeWorkGroupCount[0];
    m_rhiLimits.maxComputeWorkGroupCount[1] = limits.maxComputeWorkGroupCount[1];
    m_rhiLimits.maxComputeWorkGroupCount[2] = limits.maxComputeWorkGroupCount[2];
    m_rhiLimits.maxComputeWorkGroupInvocations = limits.maxComputeWorkGroupInvocations;
    m_rhiLimits.maxComputeWorkGroupSize[0] = limits.maxComputeWorkGroupSize[0];
    m_rhiLimits.maxComputeWorkGroupSize[1] = limits.maxComputeWorkGroupSize[1];
    m_rhiLimits.maxComputeWorkGroupSize[2] = limits.maxComputeWorkGroupSize[2];
    m_rhiLimits.maxViewports = limits.maxViewports;
    m_rhiLimits.maxViewportDimensions[0] = limits.maxViewportDimensions[0];
    m_rhiLimits.maxViewportDimensions[1] = limits.maxViewportDimensions[1];
    m_rhiLimits.maxFramebufferWidth = limits.maxFramebufferWidth;
    m_rhiLimits.maxFramebufferHeight = limits.maxFramebufferHeight;
    m_rhiLimits.maxFramebufferLayers = limits.maxFramebufferLayers;
    m_rhiLimits.maxColorAttachments = limits.maxColorAttachments;
    m_rhiLimits.maxSamplerAnisotropy = limits.maxSamplerAnisotropy;
}

void VulkanDevice::PopulateRHIFeatures() {
    m_rhiFeatures.geometryShader = m_deviceFeatures.geometryShader;
    m_rhiFeatures.tessellationShader = m_deviceFeatures.tessellationShader;
    m_rhiFeatures.computeShader = true; // Required by Vulkan 1.3
    m_rhiFeatures.multiDrawIndirect = m_deviceFeatures.multiDrawIndirect;
    m_rhiFeatures.drawIndirectFirstInstance = m_deviceFeatures.drawIndirectFirstInstance;
    m_rhiFeatures.depthClamp = m_deviceFeatures.depthClamp;
    m_rhiFeatures.depthBiasClamp = m_deviceFeatures.depthBiasClamp;
    m_rhiFeatures.fillModeNonSolid = m_deviceFeatures.fillModeNonSolid;
    m_rhiFeatures.depthBounds = m_deviceFeatures.depthBounds;
    m_rhiFeatures.wideLines = m_deviceFeatures.wideLines;
    m_rhiFeatures.largePoints = m_deviceFeatures.largePoints;
    m_rhiFeatures.alphaToOne = m_deviceFeatures.alphaToOne;
    m_rhiFeatures.multiViewport = m_deviceFeatures.multiViewport;
    m_rhiFeatures.samplerAnisotropy = m_deviceFeatures.samplerAnisotropy;
    m_rhiFeatures.textureCompressionBC = m_deviceFeatures.textureCompressionBC;
    m_rhiFeatures.occlusionQueryPrecise = m_deviceFeatures.occlusionQueryPrecise;
    m_rhiFeatures.pipelineStatisticsQuery = m_deviceFeatures.pipelineStatisticsQuery;
    m_rhiFeatures.vertexPipelineStoresAndAtomics = m_deviceFeatures.vertexPipelineStoresAndAtomics;
    m_rhiFeatures.fragmentStoresAndAtomics = m_deviceFeatures.fragmentStoresAndAtomics;
    m_rhiFeatures.shaderStorageImageExtendedFormats = m_deviceFeatures.shaderStorageImageExtendedFormats;
    m_rhiFeatures.shaderStorageImageMultisample = m_deviceFeatures.shaderStorageImageMultisample;
    m_rhiFeatures.shaderUniformBufferArrayDynamicIndexing = m_deviceFeatures.shaderUniformBufferArrayDynamicIndexing;
    m_rhiFeatures.shaderSampledImageArrayDynamicIndexing = m_deviceFeatures.shaderSampledImageArrayDynamicIndexing;
    m_rhiFeatures.shaderStorageBufferArrayDynamicIndexing = m_deviceFeatures.shaderStorageBufferArrayDynamicIndexing;
    m_rhiFeatures.shaderStorageImageArrayDynamicIndexing = m_deviceFeatures.shaderStorageImageArrayDynamicIndexing;
}

void VulkanDevice::SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name) const {
    if (!m_debugUtilsSupported || !name) {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT nameInfo = {};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = name;

    auto func = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(m_device, "vkSetDebugUtilsObjectNameEXT"));
    if (func) {
        func(m_device, &nameInfo);
    }
}

void VulkanDevice::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) const {
    if (m_commandPool == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanDevice::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
    CopyBufferToImage(buffer, image, width, height, 0, 0);
}

void VulkanDevice::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                                     uint32_t mipLevel, uint32_t arrayLayer) const {
    if (m_commandPool == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanDevice::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                         uint32_t baseMipLevel, uint32_t levelCount,
                                         uint32_t baseArrayLayer, uint32_t layerCount) const {
    if (m_commandPool == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanDevice::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                              VkDeviceSize srcOffset, VkDeviceSize dstOffset) const {
    if (m_commandPool == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

} // namespace CatEngine::RHI

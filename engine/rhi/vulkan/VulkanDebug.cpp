#include "VulkanDebug.hpp"
#include <iostream>
#include <cstring>

namespace CatEngine::RHI::Vulkan {

// Static member initialization
DebugSeverity VulkanDebug::s_minSeverity = DebugSeverity::Warning;

VkDebugUtilsMessengerEXT VulkanDebug::CreateDebugMessenger(
    VkInstance instance,
    DebugSeverity minSeverity
) {
    s_minSeverity = minSeverity;

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    // Set severity flags based on minimum severity
    createInfo.messageSeverity = 0;
    if (minSeverity <= DebugSeverity::Verbose) {
        createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    }
    if (minSeverity <= DebugSeverity::Info) {
        createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    }
    if (minSeverity <= DebugSeverity::Warning) {
        createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    }
    createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    createInfo.pfnUserCallback = DebugCallback;
    createInfo.pUserData = nullptr;

    // Load function pointer
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance,
        "vkCreateDebugUtilsMessengerEXT"
    );

    if (func == nullptr) {
        std::cerr << "[Vulkan] Failed to load vkCreateDebugUtilsMessengerEXT" << std::endl;
        return VK_NULL_HANDLE;
    }

    VkDebugUtilsMessengerEXT messenger;
    VkResult result = func(instance, &createInfo, nullptr, &messenger);

    if (result != VK_SUCCESS) {
        std::cerr << "[Vulkan] Failed to create debug messenger: " << result << std::endl;
        return VK_NULL_HANDLE;
    }

    return messenger;
}

void VulkanDebug::DestroyDebugMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger
) {
    if (messenger == VK_NULL_HANDLE) {
        return;
    }

    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance,
        "vkDestroyDebugUtilsMessengerEXT"
    );

    if (func != nullptr) {
        func(instance, messenger, nullptr);
    }
}

void VulkanDebug::SetObjectName(
    VkDevice device,
    VkObjectType objectType,
    uint64_t objectHandle,
    const char* name
) {
    if (name == nullptr || std::strlen(name) == 0) {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT nameInfo = {};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = name;

    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
        device,
        "vkSetDebugUtilsObjectNameEXT"
    );

    if (func != nullptr) {
        func(device, &nameInfo);
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebug::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
    // Determine severity prefix
    const char* severityPrefix = "[UNKNOWN]";
    std::ostream* stream = &std::cout;

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severityPrefix = "[ERROR]";
        stream = &std::cerr;
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severityPrefix = "[WARNING]";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severityPrefix = "[INFO]";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        severityPrefix = "[VERBOSE]";
    }

    // Determine message type
    const char* typePrefix = "";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        typePrefix = "[VALIDATION]";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        typePrefix = "[PERFORMANCE]";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        typePrefix = "[GENERAL]";
    }

    // Print the message
    *stream << "[Vulkan] " << severityPrefix << " " << typePrefix << " "
            << pCallbackData->pMessage << std::endl;

    // Return VK_FALSE to continue execution (VK_TRUE would abort)
    return VK_FALSE;
}

} // namespace CatEngine::RHI::Vulkan

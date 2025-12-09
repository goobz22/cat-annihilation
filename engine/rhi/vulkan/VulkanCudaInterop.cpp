// Platform-specific defines must come before vulkan.h
#ifdef _WIN32
    #define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "VulkanCudaInterop.hpp"
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace CatEngine {
namespace RHI {

// =============================================================================
// VulkanCudaExtensions
// =============================================================================

std::vector<const char*> VulkanCudaExtensions::getRequiredExtensions() {
    return std::vector<const char*>(
        std::begin(requiredDeviceExtensions),
        std::end(requiredDeviceExtensions)
    );
}

bool VulkanCudaExtensions::checkExtensionSupport(VkPhysicalDevice physicalDevice) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    for (const char* required : requiredDeviceExtensions) {
        bool found = false;
        for (const auto& available : availableExtensions) {
            if (strcmp(required, available.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// VulkanCudaInterop
// =============================================================================

cudaUUID_t VulkanCudaInterop::getVulkanDeviceUUID(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceIDProperties deviceIDProps = {};
    deviceIDProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &deviceIDProps;

    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

    cudaUUID_t uuid;
    memcpy(uuid.bytes, deviceIDProps.deviceUUID, VK_UUID_SIZE);
    return uuid;
}

std::optional<int> VulkanCudaInterop::matchCudaDevice(VkPhysicalDevice physicalDevice) {
    cudaUUID_t vulkanUUID = getVulkanDeviceUUID(physicalDevice);
    return CUDA::CudaContext::findDeviceByUUID(vulkanUUID);
}

VkExternalMemoryHandleTypeFlagBits VulkanCudaInterop::getExternalMemoryHandleType() {
#ifdef _WIN32
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
}

VkExternalSemaphoreHandleTypeFlagBits VulkanCudaInterop::getExternalSemaphoreHandleType() {
#ifdef _WIN32
    return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
}

cudaExternalMemoryHandleType VulkanCudaInterop::getCudaExternalMemoryHandleType() {
#ifdef _WIN32
    return cudaExternalMemoryHandleTypeOpaqueWin32;
#else
    return cudaExternalMemoryHandleTypeOpaqueFd;
#endif
}

cudaExternalSemaphoreHandleType VulkanCudaInterop::getCudaExternalSemaphoreHandleType() {
#ifdef _WIN32
    return cudaExternalSemaphoreHandleTypeOpaqueWin32;
#else
    return cudaExternalSemaphoreHandleTypeOpaqueFd;
#endif
}

bool VulkanCudaInterop::verifyCudaExternalMemorySupport(int cudaDeviceId) {
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, cudaDeviceId));

    // Check for compute capability 6.0 or higher (required for external memory)
    return (prop.major >= 6);
}

// =============================================================================
// CudaVulkanBuffer
// =============================================================================

CudaVulkanBuffer::CudaVulkanBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    size_t size,
    VkBufferUsageFlags usage,
    const CUDA::CudaContext& cudaContext
)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_buffer(VK_NULL_HANDLE)
    , m_memory(VK_NULL_HANDLE)
    , m_size(size)
    , m_externalMemory(nullptr)
    , m_cudaPtr(nullptr)
    , m_cudaContext(cudaContext)
{
    if (!VulkanCudaInterop::verifyCudaExternalMemorySupport(cudaContext.getDeviceId())) {
        throw std::runtime_error("CUDA device does not support external memory (requires compute capability 6.0+)");
    }

    createVulkanBuffer(usage);
    allocateExternalMemory();
    importToCuda();
}

CudaVulkanBuffer::~CudaVulkanBuffer() {
    cleanup();
}

CudaVulkanBuffer::CudaVulkanBuffer(CudaVulkanBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_physicalDevice(other.m_physicalDevice)
    , m_buffer(other.m_buffer)
    , m_memory(other.m_memory)
    , m_size(other.m_size)
    , m_externalMemory(other.m_externalMemory)
    , m_cudaPtr(other.m_cudaPtr)
    , m_cudaContext(other.m_cudaContext)
{
    other.m_buffer = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_externalMemory = nullptr;
    other.m_cudaPtr = nullptr;
    other.m_size = 0;
}

CudaVulkanBuffer& CudaVulkanBuffer::operator=(CudaVulkanBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = other.m_device;
        m_physicalDevice = other.m_physicalDevice;
        m_buffer = other.m_buffer;
        m_memory = other.m_memory;
        m_size = other.m_size;
        m_externalMemory = other.m_externalMemory;
        m_cudaPtr = other.m_cudaPtr;

        other.m_buffer = VK_NULL_HANDLE;
        other.m_memory = VK_NULL_HANDLE;
        other.m_externalMemory = nullptr;
        other.m_cudaPtr = nullptr;
        other.m_size = 0;
    }
    return *this;
}

void CudaVulkanBuffer::createVulkanBuffer(VkBufferUsageFlags usage) {
    VkExternalMemoryBufferCreateInfo externalInfo = {};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalInfo.handleTypes = VulkanCudaInterop::getExternalMemoryHandleType();

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = &externalInfo;
    bufferInfo.size = m_size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer with external memory");
    }
}

void CudaVulkanBuffer::allocateExternalMemory() {
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_buffer, &memRequirements);

    // Find suitable memory type
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = UINT32_MAX;
    VkMemoryPropertyFlags requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("Failed to find suitable memory type for external buffer");
    }

    VkExportMemoryAllocateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.handleTypes = VulkanCudaInterop::getExternalMemoryHandleType();

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &exportInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate external memory for buffer");
    }

    vkBindBufferMemory(m_device, m_buffer, m_memory, 0);
}

void CudaVulkanBuffer::importToCuda() {
#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR handleInfo = {};
    handleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.memory = m_memory;
    handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    HANDLE handle;
    PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR =
        (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(m_device, "vkGetMemoryWin32HandleKHR");

    if (!vkGetMemoryWin32HandleKHR) {
        throw std::runtime_error("Failed to get vkGetMemoryWin32HandleKHR function pointer");
    }

    VkResult result = vkGetMemoryWin32HandleKHR(m_device, &handleInfo, &handle);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to get Win32 handle for Vulkan memory");
    }
#else
    VkMemoryGetFdInfoKHR fdInfo = {};
    fdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fdInfo.memory = m_memory;
    fdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd;
    PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR =
        (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(m_device, "vkGetMemoryFdKHR");

    if (!vkGetMemoryFdKHR) {
        throw std::runtime_error("Failed to get vkGetMemoryFdKHR function pointer");
    }

    VkResult result = vkGetMemoryFdKHR(m_device, &fdInfo, &fd);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to get FD for Vulkan memory");
    }
#endif

    // Import to CUDA
    cudaExternalMemoryHandleDesc externalMemDesc = {};
    externalMemDesc.type = VulkanCudaInterop::getCudaExternalMemoryHandleType();
    externalMemDesc.size = m_size;

#ifdef _WIN32
    externalMemDesc.handle.win32.handle = handle;
#else
    externalMemDesc.handle.fd = fd;
#endif

    CUDA_CHECK(cudaImportExternalMemory(&m_externalMemory, &externalMemDesc));

    // Map buffer to CUDA pointer
    cudaExternalMemoryBufferDesc bufferDesc = {};
    bufferDesc.offset = 0;
    bufferDesc.size = m_size;
    bufferDesc.flags = 0;

    CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(&m_cudaPtr, m_externalMemory, &bufferDesc));
}

void CudaVulkanBuffer::cleanup() {
    if (m_cudaPtr != nullptr) {
        // Note: cudaFree should not be called on external memory mapped buffers
        m_cudaPtr = nullptr;
    }

    if (m_externalMemory != nullptr) {
        cudaDestroyExternalMemory(m_externalMemory);
        m_externalMemory = nullptr;
    }

    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }

    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

// =============================================================================
// CudaVulkanImage
// =============================================================================

CudaVulkanImage::CudaVulkanImage(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    const CUDA::CudaContext& cudaContext
)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_image(VK_NULL_HANDLE)
    , m_memory(VK_NULL_HANDLE)
    , m_width(width)
    , m_height(height)
    , m_format(format)
    , m_externalMemory(nullptr)
    , m_mipmappedArray(nullptr)
    , m_array(nullptr)
    , m_surfaceObject(0)
    , m_textureObject(0)
    , m_cudaContext(cudaContext)
{
    if (!VulkanCudaInterop::verifyCudaExternalMemorySupport(cudaContext.getDeviceId())) {
        throw std::runtime_error("CUDA device does not support external memory (requires compute capability 6.0+)");
    }

    createVulkanImage(usage);
    allocateExternalMemory();
    importToCuda();
}

CudaVulkanImage::~CudaVulkanImage() {
    cleanup();
}

CudaVulkanImage::CudaVulkanImage(CudaVulkanImage&& other) noexcept
    : m_device(other.m_device)
    , m_physicalDevice(other.m_physicalDevice)
    , m_image(other.m_image)
    , m_memory(other.m_memory)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_format(other.m_format)
    , m_externalMemory(other.m_externalMemory)
    , m_mipmappedArray(other.m_mipmappedArray)
    , m_array(other.m_array)
    , m_surfaceObject(other.m_surfaceObject)
    , m_textureObject(other.m_textureObject)
    , m_cudaContext(other.m_cudaContext)
{
    other.m_image = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_externalMemory = nullptr;
    other.m_mipmappedArray = nullptr;
    other.m_array = nullptr;
    other.m_surfaceObject = 0;
    other.m_textureObject = 0;
}

CudaVulkanImage& CudaVulkanImage::operator=(CudaVulkanImage&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = other.m_device;
        m_physicalDevice = other.m_physicalDevice;
        m_image = other.m_image;
        m_memory = other.m_memory;
        m_width = other.m_width;
        m_height = other.m_height;
        m_format = other.m_format;
        m_externalMemory = other.m_externalMemory;
        m_mipmappedArray = other.m_mipmappedArray;
        m_array = other.m_array;
        m_surfaceObject = other.m_surfaceObject;
        m_textureObject = other.m_textureObject;

        other.m_image = VK_NULL_HANDLE;
        other.m_memory = VK_NULL_HANDLE;
        other.m_externalMemory = nullptr;
        other.m_mipmappedArray = nullptr;
        other.m_array = nullptr;
        other.m_surfaceObject = 0;
        other.m_textureObject = 0;
    }
    return *this;
}

void CudaVulkanImage::createVulkanImage(VkImageUsageFlags usage) {
    VkExternalMemoryImageCreateInfo externalInfo = {};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalInfo.handleTypes = VulkanCudaInterop::getExternalMemoryHandleType();

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &externalInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_format;
    imageInfo.extent.width = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(m_device, &imageInfo, nullptr, &m_image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image with external memory");
    }
}

void CudaVulkanImage::allocateExternalMemory() {
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = UINT32_MAX;
    VkMemoryPropertyFlags requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        throw std::runtime_error("Failed to find suitable memory type for external image");
    }

    VkExportMemoryAllocateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.handleTypes = VulkanCudaInterop::getExternalMemoryHandleType();

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &exportInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate external memory for image");
    }

    vkBindImageMemory(m_device, m_image, m_memory, 0);
}

void CudaVulkanImage::importToCuda() {
#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR handleInfo = {};
    handleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.memory = m_memory;
    handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    HANDLE handle;
    PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR =
        (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(m_device, "vkGetMemoryWin32HandleKHR");

    if (!vkGetMemoryWin32HandleKHR) {
        throw std::runtime_error("Failed to get vkGetMemoryWin32HandleKHR function pointer");
    }

    VkResult result = vkGetMemoryWin32HandleKHR(m_device, &handleInfo, &handle);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to get Win32 handle for Vulkan image memory");
    }
#else
    VkMemoryGetFdInfoKHR fdInfo = {};
    fdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fdInfo.memory = m_memory;
    fdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd;
    PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR =
        (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(m_device, "vkGetMemoryFdKHR");

    if (!vkGetMemoryFdKHR) {
        throw std::runtime_error("Failed to get vkGetMemoryFdKHR function pointer");
    }

    VkResult result = vkGetMemoryFdKHR(m_device, &fdInfo, &fd);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to get FD for Vulkan image memory");
    }
#endif

    // Get memory requirements for size
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

    // Import to CUDA
    cudaExternalMemoryHandleDesc externalMemDesc = {};
    externalMemDesc.type = VulkanCudaInterop::getCudaExternalMemoryHandleType();
    externalMemDesc.size = memRequirements.size;

#ifdef _WIN32
    externalMemDesc.handle.win32.handle = handle;
#else
    externalMemDesc.handle.fd = fd;
#endif

    CUDA_CHECK(cudaImportExternalMemory(&m_externalMemory, &externalMemDesc));

    // Map to CUDA mipmapped array
    cudaExternalMemoryMipmappedArrayDesc mipmapDesc = {};
    mipmapDesc.offset = 0;
    mipmapDesc.formatDesc = getCudaChannelFormat();
    mipmapDesc.extent = make_cudaExtent(m_width, m_height, 0);
    mipmapDesc.flags = 0;
    mipmapDesc.numLevels = 1;

    CUDA_CHECK(cudaExternalMemoryGetMappedMipmappedArray(&m_mipmappedArray, m_externalMemory, &mipmapDesc));

    // Get level 0 array
    CUDA_CHECK(cudaGetMipmappedArrayLevel(&m_array, m_mipmappedArray, 0));
}

cudaChannelFormatDesc CudaVulkanImage::getCudaChannelFormat() const {
    // Map Vulkan formats to CUDA channel formats
    switch (m_format) {
        case VK_FORMAT_R8_UNORM:
            return cudaCreateChannelDesc(8, 0, 0, 0, cudaChannelFormatKindUnsigned);
        case VK_FORMAT_R8G8_UNORM:
            return cudaCreateChannelDesc(8, 8, 0, 0, cudaChannelFormatKindUnsigned);
        case VK_FORMAT_R8G8B8A8_UNORM:
            return cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsigned);
        case VK_FORMAT_R16_SFLOAT:
            return cudaCreateChannelDesc(16, 0, 0, 0, cudaChannelFormatKindFloat);
        case VK_FORMAT_R16G16_SFLOAT:
            return cudaCreateChannelDesc(16, 16, 0, 0, cudaChannelFormatKindFloat);
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return cudaCreateChannelDesc(16, 16, 16, 16, cudaChannelFormatKindFloat);
        case VK_FORMAT_R32_SFLOAT:
            return cudaCreateChannelDesc(32, 0, 0, 0, cudaChannelFormatKindFloat);
        case VK_FORMAT_R32G32_SFLOAT:
            return cudaCreateChannelDesc(32, 32, 0, 0, cudaChannelFormatKindFloat);
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return cudaCreateChannelDesc(32, 32, 32, 32, cudaChannelFormatKindFloat);
        default:
            throw std::runtime_error("Unsupported Vulkan format for CUDA interop");
    }
}

cudaSurfaceObject_t CudaVulkanImage::getCudaSurface() {
    if (m_surfaceObject == 0) {
        createCudaSurface();
    }
    return m_surfaceObject;
}

cudaTextureObject_t CudaVulkanImage::getCudaTexture() {
    if (m_textureObject == 0) {
        createCudaTexture();
    }
    return m_textureObject;
}

void CudaVulkanImage::createCudaSurface() {
    cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = m_array;

    CUDA_CHECK(cudaCreateSurfaceObject(&m_surfaceObject, &resDesc));
}

void CudaVulkanImage::createCudaTexture() {
    cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = m_array;

    cudaTextureDesc texDesc = {};
    texDesc.addressMode[0] = cudaAddressModeClamp;
    texDesc.addressMode[1] = cudaAddressModeClamp;
    texDesc.filterMode = cudaFilterModeLinear;
    texDesc.readMode = cudaReadModeElementType;
    texDesc.normalizedCoords = 0;

    CUDA_CHECK(cudaCreateTextureObject(&m_textureObject, &resDesc, &texDesc, nullptr));
}

void CudaVulkanImage::cleanup() {
    if (m_textureObject != 0) {
        cudaDestroyTextureObject(m_textureObject);
        m_textureObject = 0;
    }

    if (m_surfaceObject != 0) {
        cudaDestroySurfaceObject(m_surfaceObject);
        m_surfaceObject = 0;
    }

    // Don't free m_array - it's owned by m_mipmappedArray
    m_array = nullptr;

    if (m_mipmappedArray != nullptr) {
        cudaFreeMipmappedArray(m_mipmappedArray);
        m_mipmappedArray = nullptr;
    }

    if (m_externalMemory != nullptr) {
        cudaDestroyExternalMemory(m_externalMemory);
        m_externalMemory = nullptr;
    }

    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }

    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

// =============================================================================
// CudaVulkanSemaphore
// =============================================================================

CudaVulkanSemaphore::CudaVulkanSemaphore(VkDevice device, const CUDA::CudaContext& cudaContext)
    : m_device(device)
    , m_semaphore(VK_NULL_HANDLE)
    , m_cudaSemaphore(nullptr)
    , m_cudaContext(cudaContext)
{
    createVulkanSemaphore();
    importToCuda();
}

CudaVulkanSemaphore::~CudaVulkanSemaphore() {
    cleanup();
}

CudaVulkanSemaphore::CudaVulkanSemaphore(CudaVulkanSemaphore&& other) noexcept
    : m_device(other.m_device)
    , m_semaphore(other.m_semaphore)
    , m_cudaSemaphore(other.m_cudaSemaphore)
    , m_cudaContext(other.m_cudaContext)
{
    other.m_semaphore = VK_NULL_HANDLE;
    other.m_cudaSemaphore = nullptr;
}

CudaVulkanSemaphore& CudaVulkanSemaphore::operator=(CudaVulkanSemaphore&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = other.m_device;
        m_semaphore = other.m_semaphore;
        m_cudaSemaphore = other.m_cudaSemaphore;

        other.m_semaphore = VK_NULL_HANDLE;
        other.m_cudaSemaphore = nullptr;
    }
    return *this;
}

void CudaVulkanSemaphore::createVulkanSemaphore() {
    VkExportSemaphoreCreateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportInfo.handleTypes = VulkanCudaInterop::getExternalSemaphoreHandleType();

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &exportInfo;

    VkResult result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_semaphore);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan semaphore with external export");
    }
}

void CudaVulkanSemaphore::importToCuda() {
#ifdef _WIN32
    VkSemaphoreGetWin32HandleInfoKHR handleInfo = {};
    handleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.semaphore = m_semaphore;
    handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    HANDLE handle;
    PFN_vkGetSemaphoreWin32HandleKHR vkGetSemaphoreWin32HandleKHR =
        (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(m_device, "vkGetSemaphoreWin32HandleKHR");

    if (!vkGetSemaphoreWin32HandleKHR) {
        throw std::runtime_error("Failed to get vkGetSemaphoreWin32HandleKHR function pointer");
    }

    VkResult result = vkGetSemaphoreWin32HandleKHR(m_device, &handleInfo, &handle);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to get Win32 handle for Vulkan semaphore");
    }
#else
    VkSemaphoreGetFdInfoKHR fdInfo = {};
    fdInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    fdInfo.semaphore = m_semaphore;
    fdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd;
    PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR =
        (PFN_vkGetSemaphoreFdKHR)vkGetDeviceProcAddr(m_device, "vkGetSemaphoreFdKHR");

    if (!vkGetSemaphoreFdKHR) {
        throw std::runtime_error("Failed to get vkGetSemaphoreFdKHR function pointer");
    }

    VkResult result = vkGetSemaphoreFdKHR(m_device, &fdInfo, &fd);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to get FD for Vulkan semaphore");
    }
#endif

    // Import to CUDA
    cudaExternalSemaphoreHandleDesc externalSemDesc = {};
    externalSemDesc.type = VulkanCudaInterop::getCudaExternalSemaphoreHandleType();
    externalSemDesc.flags = 0;

#ifdef _WIN32
    externalSemDesc.handle.win32.handle = handle;
#else
    externalSemDesc.handle.fd = fd;
#endif

    CUDA_CHECK(cudaImportExternalSemaphore(&m_cudaSemaphore, &externalSemDesc));
}

void CudaVulkanSemaphore::cudaWait(cudaStream_t stream, uint64_t value) {
    cudaExternalSemaphoreWaitParams waitParams = {};
    waitParams.flags = 0;
    waitParams.params.fence.value = value;

    CUDA_CHECK(cudaWaitExternalSemaphoresAsync(&m_cudaSemaphore, &waitParams, 1, stream));
}

void CudaVulkanSemaphore::cudaSignal(cudaStream_t stream, uint64_t value) {
    cudaExternalSemaphoreSignalParams signalParams = {};
    signalParams.flags = 0;
    signalParams.params.fence.value = value;

    CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&m_cudaSemaphore, &signalParams, 1, stream));
}

void CudaVulkanSemaphore::cleanup() {
    if (m_cudaSemaphore != nullptr) {
        cudaDestroyExternalSemaphore(m_cudaSemaphore);
        m_cudaSemaphore = nullptr;
    }

    if (m_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device, m_semaphore, nullptr);
        m_semaphore = VK_NULL_HANDLE;
    }
}

} // namespace RHI
} // namespace CatEngine

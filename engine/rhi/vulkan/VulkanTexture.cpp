#include "VulkanTexture.hpp"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace CatEngine::RHI {

// ============================================================================
// Format Conversion
// ============================================================================

VkFormat ToVkFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::Undefined: return VK_FORMAT_UNDEFINED;

        // 8-bit formats
        case TextureFormat::R8_UNORM: return VK_FORMAT_R8_UNORM;
        case TextureFormat::R8_SNORM: return VK_FORMAT_R8_SNORM;
        case TextureFormat::R8_UINT: return VK_FORMAT_R8_UINT;
        case TextureFormat::R8_SINT: return VK_FORMAT_R8_SINT;

        // 16-bit formats
        case TextureFormat::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::RG8_SNORM: return VK_FORMAT_R8G8_SNORM;
        case TextureFormat::RG8_UINT: return VK_FORMAT_R8G8_UINT;
        case TextureFormat::RG8_SINT: return VK_FORMAT_R8G8_SINT;

        case TextureFormat::R16_UNORM: return VK_FORMAT_R16_UNORM;
        case TextureFormat::R16_SNORM: return VK_FORMAT_R16_SNORM;
        case TextureFormat::R16_UINT: return VK_FORMAT_R16_UINT;
        case TextureFormat::R16_SINT: return VK_FORMAT_R16_SINT;
        case TextureFormat::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;

        // 32-bit formats
        case TextureFormat::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::RGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
        case TextureFormat::RGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
        case TextureFormat::RGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;

        case TextureFormat::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;

        case TextureFormat::RG16_UNORM: return VK_FORMAT_R16G16_UNORM;
        case TextureFormat::RG16_SNORM: return VK_FORMAT_R16G16_SNORM;
        case TextureFormat::RG16_UINT: return VK_FORMAT_R16G16_UINT;
        case TextureFormat::RG16_SINT: return VK_FORMAT_R16G16_SINT;
        case TextureFormat::RG16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;

        case TextureFormat::R32_UINT: return VK_FORMAT_R32_UINT;
        case TextureFormat::R32_SINT: return VK_FORMAT_R32_SINT;
        case TextureFormat::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;

        // 64-bit formats
        case TextureFormat::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
        case TextureFormat::RGBA16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
        case TextureFormat::RGBA16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
        case TextureFormat::RGBA16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
        case TextureFormat::RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;

        case TextureFormat::RG32_UINT: return VK_FORMAT_R32G32_UINT;
        case TextureFormat::RG32_SINT: return VK_FORMAT_R32G32_SINT;
        case TextureFormat::RG32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;

        // 128-bit formats
        case TextureFormat::RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
        case TextureFormat::RGBA32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
        case TextureFormat::RGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;

        // Depth/Stencil formats
        case TextureFormat::D16_UNORM: return VK_FORMAT_D16_UNORM;
        case TextureFormat::D32_SFLOAT: return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::D32_SFLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case TextureFormat::S8_UINT: return VK_FORMAT_S8_UINT;

        // Compressed formats
        case TextureFormat::BC1_RGB_UNORM: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case TextureFormat::BC1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case TextureFormat::BC1_RGBA_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case TextureFormat::BC1_RGBA_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case TextureFormat::BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
        case TextureFormat::BC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
        case TextureFormat::BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
        case TextureFormat::BC4_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
        case TextureFormat::BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
        case TextureFormat::BC5_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
        case TextureFormat::BC6H_UFLOAT: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case TextureFormat::BC6H_SFLOAT: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case TextureFormat::BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
        case TextureFormat::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;

        default: return VK_FORMAT_UNDEFINED;
    }
}

TextureFormat FromVkFormat(VkFormat format) {
    // Reverse mapping (partial implementation)
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM: return TextureFormat::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return TextureFormat::RGBA8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM: return TextureFormat::BGRA8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return TextureFormat::BGRA8_SRGB;
        case VK_FORMAT_D32_SFLOAT: return TextureFormat::D32_SFLOAT;
        case VK_FORMAT_D24_UNORM_S8_UINT: return TextureFormat::D24_UNORM_S8_UINT;
        default: return TextureFormat::Undefined;
    }
}

static VkImageUsageFlags ToVkImageUsage(TextureUsage usage) {
    VkImageUsageFlags flags = 0;

    if (static_cast<uint32_t>(usage & TextureUsage::Sampled))
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (static_cast<uint32_t>(usage & TextureUsage::Storage))
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (static_cast<uint32_t>(usage & TextureUsage::RenderTarget))
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (static_cast<uint32_t>(usage & TextureUsage::DepthStencil))
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (static_cast<uint32_t>(usage & TextureUsage::TransferSrc))
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (static_cast<uint32_t>(usage & TextureUsage::TransferDst))
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (static_cast<uint32_t>(usage & TextureUsage::Transient))
        flags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

    return flags;
}

// ============================================================================
// VulkanTexture Implementation
// ============================================================================

VulkanTexture::VulkanTexture(VulkanDevice* device, const TextureDesc& desc)
    : m_Device(device)
    , m_Image(VK_NULL_HANDLE)
    , m_ImageView(VK_NULL_HANDLE)
    , m_Memory(VK_NULL_HANDLE)
    , m_CurrentLayout(VK_IMAGE_LAYOUT_UNDEFINED)
    , m_Type(desc.type)
    , m_Format(desc.format)
    , m_Usage(desc.usage)
    , m_Width(desc.width)
    , m_Height(desc.height)
    , m_Depth(desc.depth)
    , m_MipLevels(desc.mipLevels)
    , m_ArrayLayers(desc.arrayLayers)
    , m_SampleCount(desc.sampleCount)
    , m_DebugName(desc.debugName ? desc.debugName : "")
{
    CreateImage();
    AllocateMemory();
    CreateImageView();
}

VulkanTexture::~VulkanTexture() {
    if (m_Device == nullptr) return;

    VkDevice device = m_Device->GetVkDevice();

    if (m_ImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_ImageView, nullptr);
        m_ImageView = VK_NULL_HANDLE;
    }

    if (m_Image != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_Image, nullptr);
        m_Image = VK_NULL_HANDLE;
    }

    if (m_Memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_Memory, nullptr);
        m_Memory = VK_NULL_HANDLE;
    }
}

VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept
    : m_Device(other.m_Device)
    , m_Image(other.m_Image)
    , m_ImageView(other.m_ImageView)
    , m_Memory(other.m_Memory)
    , m_CurrentLayout(other.m_CurrentLayout)
    , m_Type(other.m_Type)
    , m_Format(other.m_Format)
    , m_Usage(other.m_Usage)
    , m_Width(other.m_Width)
    , m_Height(other.m_Height)
    , m_Depth(other.m_Depth)
    , m_MipLevels(other.m_MipLevels)
    , m_ArrayLayers(other.m_ArrayLayers)
    , m_SampleCount(other.m_SampleCount)
    , m_DebugName(std::move(other.m_DebugName))
{
    other.m_Device = nullptr;
    other.m_Image = VK_NULL_HANDLE;
    other.m_ImageView = VK_NULL_HANDLE;
    other.m_Memory = VK_NULL_HANDLE;
}

VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept {
    if (this != &other) {
        this->~VulkanTexture();

        m_Device = other.m_Device;
        m_Image = other.m_Image;
        m_ImageView = other.m_ImageView;
        m_Memory = other.m_Memory;
        m_CurrentLayout = other.m_CurrentLayout;
        m_Type = other.m_Type;
        m_Format = other.m_Format;
        m_Usage = other.m_Usage;
        m_Width = other.m_Width;
        m_Height = other.m_Height;
        m_Depth = other.m_Depth;
        m_MipLevels = other.m_MipLevels;
        m_ArrayLayers = other.m_ArrayLayers;
        m_SampleCount = other.m_SampleCount;
        m_DebugName = std::move(other.m_DebugName);

        other.m_Device = nullptr;
        other.m_Image = VK_NULL_HANDLE;
        other.m_ImageView = VK_NULL_HANDLE;
        other.m_Memory = VK_NULL_HANDLE;
    }
    return *this;
}

VkImageType VulkanTexture::GetVkImageType() const {
    switch (m_Type) {
        case TextureType::Texture1D:
        case TextureType::Texture1DArray:
            return VK_IMAGE_TYPE_1D;
        case TextureType::Texture2D:
        case TextureType::Texture2DArray:
        case TextureType::TextureCube:
        case TextureType::TextureCubeArray:
            return VK_IMAGE_TYPE_2D;
        case TextureType::Texture3D:
            return VK_IMAGE_TYPE_3D;
        default:
            return VK_IMAGE_TYPE_2D;
    }
}

VkImageViewType VulkanTexture::GetVkImageViewType() const {
    switch (m_Type) {
        case TextureType::Texture1D: return VK_IMAGE_VIEW_TYPE_1D;
        case TextureType::Texture2D: return VK_IMAGE_VIEW_TYPE_2D;
        case TextureType::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
        case TextureType::TextureCube: return VK_IMAGE_VIEW_TYPE_CUBE;
        case TextureType::Texture1DArray: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case TextureType::Texture2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TextureType::TextureCubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}

VkSampleCountFlagBits VulkanTexture::GetVkSampleCount() const {
    switch (m_SampleCount) {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

void VulkanTexture::CreateImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = GetVkImageType();
    imageInfo.format = ToVkFormat(m_Format);
    imageInfo.extent.width = m_Width;
    imageInfo.extent.height = m_Height;
    imageInfo.extent.depth = m_Depth;
    imageInfo.mipLevels = m_MipLevels;
    imageInfo.arrayLayers = m_ArrayLayers;
    imageInfo.samples = GetVkSampleCount();
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = ToVkImageUsage(m_Usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Cube map flag
    if (m_Type == TextureType::TextureCube || m_Type == TextureType::TextureCubeArray) {
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreateImage(device, &imageInfo, nullptr, &m_Image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanTexture: Failed to create image");
    }

    // Set debug name
    if (!m_DebugName.empty() && m_Device->IsDebugUtilsSupported()) {
        m_Device->SetObjectName(VK_OBJECT_TYPE_IMAGE, (uint64_t)m_Image, m_DebugName.c_str());
    }
}

void VulkanTexture::AllocateMemory() {
    VkDevice device = m_Device->GetVkDevice();

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_Image, &memRequirements);

    // Prefer device-local memory for images
    uint32_t memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(device, &allocInfo, nullptr, &m_Memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanTexture: Failed to allocate image memory");
    }

    // Bind image to memory
    result = vkBindImageMemory(device, m_Image, m_Memory, 0);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanTexture: Failed to bind image memory");
    }
}

void VulkanTexture::CreateImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = GetVkImageViewType();
    viewInfo.format = ToVkFormat(m_Format);

    // Component swizzle (identity)
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // Subresource range
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (static_cast<uint32_t>(m_Usage & TextureUsage::DepthStencil)) {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        // Add stencil aspect if format has stencil
        if (m_Format == TextureFormat::D24_UNORM_S8_UINT ||
            m_Format == TextureFormat::D32_SFLOAT_S8_UINT) {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_MipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_ArrayLayers;

    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &m_ImageView);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanTexture: Failed to create image view");
    }
}

uint32_t VulkanTexture::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_Device->GetVkPhysicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("VulkanTexture: Failed to find suitable memory type");
}

void VulkanTexture::TransitionLayout(VkImageLayout newLayout, uint32_t baseMipLevel,
                                     uint32_t levelCount, uint32_t baseArrayLayer,
                                     uint32_t layerCount) {
    if (levelCount == VK_REMAINING_MIP_LEVELS) {
        levelCount = m_MipLevels - baseMipLevel;
    }
    if (layerCount == VK_REMAINING_ARRAY_LAYERS) {
        layerCount = m_ArrayLayers - baseArrayLayer;
    }

    m_Device->TransitionImageLayout(m_Image, m_CurrentLayout, newLayout,
                                     baseMipLevel, levelCount,
                                     baseArrayLayer, layerCount);
    m_CurrentLayout = newLayout;
}

void VulkanTexture::UploadData(const void* data, uint64_t size, uint32_t mipLevel, uint32_t arrayLayer) {
    // Create staging buffer
    BufferDesc stagingDesc{};
    stagingDesc.size = size;
    stagingDesc.usage = BufferUsage::TransferSrc;
    stagingDesc.memoryProperties = MemoryProperty::HostVisible | MemoryProperty::HostCoherent;
    stagingDesc.debugName = "TextureStagingBuffer";

    VulkanBuffer stagingBuffer(m_Device, stagingDesc);
    stagingBuffer.UpdateData(data, size, 0);

    // Transition to transfer dst
    TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel, 1, arrayLayer, 1);

    // Copy buffer to image
    m_Device->CopyBufferToImage(stagingBuffer.GetVkBuffer(), m_Image,
                                 m_Width, m_Height, mipLevel, arrayLayer);

    // Transition to shader read optimal
    TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevel, 1, arrayLayer, 1);
}

// ============================================================================
// VulkanSampler Implementation
// ============================================================================

static VkFilter ToVkFilter(Filter filter) {
    switch (filter) {
        case Filter::Nearest: return VK_FILTER_NEAREST;
        case Filter::Linear: return VK_FILTER_LINEAR;
        default: return VK_FILTER_LINEAR;
    }
}

static VkSamplerMipmapMode ToVkMipmapMode(MipmapMode mode) {
    switch (mode) {
        case MipmapMode::Nearest: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case MipmapMode::Linear: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

static VkSamplerAddressMode ToVkAddressMode(AddressMode mode) {
    switch (mode) {
        case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case AddressMode::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkBorderColor ToVkBorderColor(BorderColor color) {
    switch (color) {
        case BorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case BorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case BorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        default: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
}

static VkCompareOp ToVkCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return VK_COMPARE_OP_NEVER;
        case CompareOp::Less: return VK_COMPARE_OP_LESS;
        case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_NEVER;
    }
}

VulkanSampler::VulkanSampler(VulkanDevice* device, const SamplerDesc& desc)
    : m_Device(device)
    , m_Sampler(VK_NULL_HANDLE)
    , m_Desc(desc)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = ToVkFilter(desc.magFilter);
    samplerInfo.minFilter = ToVkFilter(desc.minFilter);
    samplerInfo.mipmapMode = ToVkMipmapMode(desc.mipmapMode);
    samplerInfo.addressModeU = ToVkAddressMode(desc.addressModeU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.addressModeV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.addressModeW);
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;
    samplerInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = ToVkCompareOp(desc.compareOp);
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    samplerInfo.borderColor = ToVkBorderColor(desc.borderColor);
    samplerInfo.unnormalizedCoordinates = desc.unnormalizedCoordinates ? VK_TRUE : VK_FALSE;

    VkDevice vkDevice = m_Device->GetVkDevice();
    VkResult result = vkCreateSampler(vkDevice, &samplerInfo, nullptr, &m_Sampler);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanSampler: Failed to create sampler");
    }
}

VulkanSampler::~VulkanSampler() {
    if (m_Device && m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device->GetVkDevice(), m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }
}

VulkanSampler::VulkanSampler(VulkanSampler&& other) noexcept
    : m_Device(other.m_Device)
    , m_Sampler(other.m_Sampler)
    , m_Desc(other.m_Desc)
{
    other.m_Device = nullptr;
    other.m_Sampler = VK_NULL_HANDLE;
}

VulkanSampler& VulkanSampler::operator=(VulkanSampler&& other) noexcept {
    if (this != &other) {
        this->~VulkanSampler();

        m_Device = other.m_Device;
        m_Sampler = other.m_Sampler;
        m_Desc = other.m_Desc;

        other.m_Device = nullptr;
        other.m_Sampler = VK_NULL_HANDLE;
    }
    return *this;
}

// ============================================================================
// VulkanTextureView Implementation
// ============================================================================

VulkanTextureView::VulkanTextureView(VulkanDevice* device, VulkanTexture* texture,
                                     TextureFormat format, uint32_t baseMipLevel,
                                     uint32_t mipLevelCount, uint32_t baseArrayLayer,
                                     uint32_t arrayLayerCount)
    : m_Device(device)
    , m_Texture(texture)
    , m_ImageView(VK_NULL_HANDLE)
    , m_Format(format)
    , m_BaseMipLevel(baseMipLevel)
    , m_MipLevelCount(mipLevelCount)
    , m_BaseArrayLayer(baseArrayLayer)
    , m_ArrayLayerCount(arrayLayerCount)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->GetVkImage();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // Could be derived from texture type
    viewInfo.format = ToVkFormat(format);
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = baseMipLevel;
    viewInfo.subresourceRange.levelCount = mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    viewInfo.subresourceRange.layerCount = arrayLayerCount;

    VkDevice vkDevice = m_Device->GetVkDevice();
    VkResult result = vkCreateImageView(vkDevice, &viewInfo, nullptr, &m_ImageView);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanTextureView: Failed to create image view");
    }
}

VulkanTextureView::~VulkanTextureView() {
    if (m_Device && m_ImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device->GetVkDevice(), m_ImageView, nullptr);
        m_ImageView = VK_NULL_HANDLE;
    }
}

VulkanTextureView::VulkanTextureView(VulkanTextureView&& other) noexcept
    : m_Device(other.m_Device)
    , m_Texture(other.m_Texture)
    , m_ImageView(other.m_ImageView)
    , m_Format(other.m_Format)
    , m_BaseMipLevel(other.m_BaseMipLevel)
    , m_MipLevelCount(other.m_MipLevelCount)
    , m_BaseArrayLayer(other.m_BaseArrayLayer)
    , m_ArrayLayerCount(other.m_ArrayLayerCount)
{
    other.m_Device = nullptr;
    other.m_ImageView = VK_NULL_HANDLE;
}

VulkanTextureView& VulkanTextureView::operator=(VulkanTextureView&& other) noexcept {
    if (this != &other) {
        this->~VulkanTextureView();

        m_Device = other.m_Device;
        m_Texture = other.m_Texture;
        m_ImageView = other.m_ImageView;
        m_Format = other.m_Format;
        m_BaseMipLevel = other.m_BaseMipLevel;
        m_MipLevelCount = other.m_MipLevelCount;
        m_BaseArrayLayer = other.m_BaseArrayLayer;
        m_ArrayLayerCount = other.m_ArrayLayerCount;

        other.m_Device = nullptr;
        other.m_ImageView = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace CatEngine::RHI

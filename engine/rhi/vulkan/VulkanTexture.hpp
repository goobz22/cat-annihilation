#pragma once

#include "../RHITexture.hpp"
#include "../RHITypes.hpp"
#include <vulkan/vulkan.h>
#include <string>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;

/**
 * Vulkan implementation of IRHITexture
 * Manages VkImage and VkDeviceMemory
 */
class VulkanTexture : public IRHITexture {
public:
    VulkanTexture(VulkanDevice* device, const TextureDesc& desc);
    ~VulkanTexture() override;

    // Disable copy, allow move
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&&) noexcept;
    VulkanTexture& operator=(VulkanTexture&&) noexcept;

    // IRHITexture interface
    TextureType GetType() const override { return m_Type; }
    TextureFormat GetFormat() const override { return m_Format; }
    TextureUsage GetUsage() const override { return m_Usage; }
    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    uint32_t GetDepth() const override { return m_Depth; }
    uint32_t GetMipLevels() const override { return m_MipLevels; }
    uint32_t GetArrayLayers() const override { return m_ArrayLayers; }
    uint32_t GetSampleCount() const override { return m_SampleCount; }
    const char* GetDebugName() const override { return m_DebugName.c_str(); }

    // Vulkan-specific getters
    VkImage GetVkImage() const { return m_Image; }
    VkImageView GetVkImageView() const { return m_ImageView; }
    VkDeviceMemory GetVkDeviceMemory() const { return m_Memory; }
    VkImageLayout GetCurrentLayout() const { return m_CurrentLayout; }

    /**
     * Transition image layout
     */
    void TransitionLayout(VkImageLayout newLayout, uint32_t baseMipLevel = 0,
                         uint32_t levelCount = VK_REMAINING_MIP_LEVELS,
                         uint32_t baseArrayLayer = 0,
                         uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS);

    /**
     * Upload texture data from host memory
     */
    void UploadData(const void* data, uint64_t size, uint32_t mipLevel = 0, uint32_t arrayLayer = 0);

private:
    void CreateImage();
    void AllocateMemory();
    void CreateImageView();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkImageType GetVkImageType() const;
    VkImageViewType GetVkImageViewType() const;
    VkSampleCountFlagBits GetVkSampleCount() const;

private:
    VulkanDevice* m_Device;
    VkImage m_Image;
    VkImageView m_ImageView;
    VkDeviceMemory m_Memory;
    VkImageLayout m_CurrentLayout;

    TextureType m_Type;
    TextureFormat m_Format;
    TextureUsage m_Usage;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_Depth;
    uint32_t m_MipLevels;
    uint32_t m_ArrayLayers;
    uint32_t m_SampleCount;
    std::string m_DebugName;
};

/**
 * Vulkan implementation of IRHISampler
 */
class VulkanSampler : public IRHISampler {
public:
    VulkanSampler(VulkanDevice* device, const SamplerDesc& desc);
    ~VulkanSampler() override;

    // Disable copy, allow move
    VulkanSampler(const VulkanSampler&) = delete;
    VulkanSampler& operator=(const VulkanSampler&) = delete;
    VulkanSampler(VulkanSampler&&) noexcept;
    VulkanSampler& operator=(VulkanSampler&&) noexcept;

    // IRHISampler interface
    const SamplerDesc& GetDesc() const override { return m_Desc; }

    // Vulkan-specific getters
    VkSampler GetVkSampler() const { return m_Sampler; }
    VkSampler GetHandle() const { return m_Sampler; }

private:
    VulkanDevice* m_Device;
    VkSampler m_Sampler;
    SamplerDesc m_Desc;
};

/**
 * Vulkan implementation of IRHITextureView
 */
class VulkanTextureView : public IRHITextureView {
public:
    VulkanTextureView(VulkanDevice* device, VulkanTexture* texture,
                      TextureFormat format, uint32_t baseMipLevel,
                      uint32_t mipLevelCount, uint32_t baseArrayLayer,
                      uint32_t arrayLayerCount);
    ~VulkanTextureView() override;

    // Disable copy, allow move
    VulkanTextureView(const VulkanTextureView&) = delete;
    VulkanTextureView& operator=(const VulkanTextureView&) = delete;
    VulkanTextureView(VulkanTextureView&&) noexcept;
    VulkanTextureView& operator=(VulkanTextureView&&) noexcept;

    // IRHITextureView interface
    IRHITexture* GetTexture() const override { return m_Texture; }
    TextureFormat GetFormat() const override { return m_Format; }
    uint32_t GetBaseMipLevel() const override { return m_BaseMipLevel; }
    uint32_t GetMipLevelCount() const override { return m_MipLevelCount; }
    uint32_t GetBaseArrayLayer() const override { return m_BaseArrayLayer; }
    uint32_t GetArrayLayerCount() const override { return m_ArrayLayerCount; }

    // Vulkan-specific getters
    VkImageView GetVkImageView() const { return m_ImageView; }
    VkImageView GetHandle() const { return m_ImageView; }

private:
    VulkanDevice* m_Device;
    VulkanTexture* m_Texture;
    VkImageView m_ImageView;

    TextureFormat m_Format;
    uint32_t m_BaseMipLevel;
    uint32_t m_MipLevelCount;
    uint32_t m_BaseArrayLayer;
    uint32_t m_ArrayLayerCount;
};

// Helper functions for format conversion
VkFormat ToVkFormat(TextureFormat format);
TextureFormat FromVkFormat(VkFormat format);

} // namespace CatEngine::RHI

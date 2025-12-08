#pragma once

#include "../RHIPipeline.hpp"
#include "../RHITypes.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;
class VulkanShader;
class IRHIRenderPass;
class IRHIDescriptorSetLayout;

/**
 * Vulkan implementation of IRHIPipelineLayout
 * Manages VkPipelineLayout with descriptor sets and push constants
 */
class VulkanPipelineLayout : public IRHIPipelineLayout {
public:
    struct PushConstantRange {
        ShaderStage stageFlags;
        uint32_t offset;
        uint32_t size;
    };

    VulkanPipelineLayout(VulkanDevice* device,
                         const std::vector<IRHIDescriptorSetLayout*>& descriptorSetLayouts,
                         const std::vector<PushConstantRange>& pushConstantRanges,
                         const char* debugName = nullptr);

    ~VulkanPipelineLayout() override;

    // Disable copy, allow move
    VulkanPipelineLayout(const VulkanPipelineLayout&) = delete;
    VulkanPipelineLayout& operator=(const VulkanPipelineLayout&) = delete;
    VulkanPipelineLayout(VulkanPipelineLayout&&) noexcept;
    VulkanPipelineLayout& operator=(VulkanPipelineLayout&&) noexcept;

    // IRHIPipelineLayout interface
    uint32_t GetDescriptorSetCount() const override { return static_cast<uint32_t>(m_DescriptorSetLayouts.size()); }
    const char* GetDebugName() const override { return m_DebugName.c_str(); }

    // Vulkan-specific getters
    VkPipelineLayout GetVkPipelineLayout() const { return m_PipelineLayout; }
    const std::vector<PushConstantRange>& GetPushConstantRanges() const { return m_PushConstantRanges; }

private:
    VulkanDevice* m_Device;
    VkPipelineLayout m_PipelineLayout;

    std::vector<IRHIDescriptorSetLayout*> m_DescriptorSetLayouts;
    std::vector<PushConstantRange> m_PushConstantRanges;
    std::string m_DebugName;
};

/**
 * Vulkan implementation of IRHIPipeline (Graphics Pipeline)
 * Manages VkPipeline with all graphics state
 */
class VulkanGraphicsPipeline : public IRHIPipeline {
public:
    VulkanGraphicsPipeline(VulkanDevice* device, const PipelineDesc& desc);
    VulkanGraphicsPipeline(VulkanDevice* device, const PipelineDesc& desc,
                           VulkanPipelineLayout* layout, VkPipelineCache cache = VK_NULL_HANDLE);

    ~VulkanGraphicsPipeline() override;

    // Disable copy, allow move
    VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline(VulkanGraphicsPipeline&&) noexcept;
    VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&&) noexcept;

    // IRHIPipeline interface
    PipelineBindPoint GetBindPoint() const override { return PipelineBindPoint::Graphics; }
    const char* GetDebugName() const override { return m_DebugName.c_str(); }

    // Vulkan-specific getters
    VkPipeline GetVkPipeline() const { return m_Pipeline; }
    VulkanPipelineLayout* GetLayout() const { return m_Layout.get(); }
    VkPipelineLayout GetVkPipelineLayout() const { return m_Layout->GetVkPipelineLayout(); }

private:
    void CreatePipeline(const PipelineDesc& desc, VkPipelineCache cache);
    VkPipelineLayout CreateDefaultLayout(const PipelineDesc& desc);

private:
    VulkanDevice* m_Device;
    VkPipeline m_Pipeline;
    std::unique_ptr<VulkanPipelineLayout> m_Layout;
    std::string m_DebugName;
};

/**
 * Vulkan implementation of IRHIPipeline (Compute Pipeline)
 * Manages VkPipeline for compute shaders
 */
class VulkanComputePipeline : public IRHIPipeline {
public:
    VulkanComputePipeline(VulkanDevice* device, const ComputePipelineDesc& desc);
    VulkanComputePipeline(VulkanDevice* device, const ComputePipelineDesc& desc,
                          VulkanPipelineLayout* layout, VkPipelineCache cache = VK_NULL_HANDLE);

    ~VulkanComputePipeline() override;

    // Disable copy, allow move
    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline(VulkanComputePipeline&&) noexcept;
    VulkanComputePipeline& operator=(VulkanComputePipeline&&) noexcept;

    // IRHIPipeline interface
    PipelineBindPoint GetBindPoint() const override { return PipelineBindPoint::Compute; }
    const char* GetDebugName() const override { return m_DebugName.c_str(); }

    // Vulkan-specific getters
    VkPipeline GetVkPipeline() const { return m_Pipeline; }
    VulkanPipelineLayout* GetLayout() const { return m_Layout.get(); }
    VkPipelineLayout GetVkPipelineLayout() const { return m_Layout->GetVkPipelineLayout(); }

private:
    void CreatePipeline(const ComputePipelineDesc& desc, VkPipelineCache cache);
    VkPipelineLayout CreateDefaultLayout(const ComputePipelineDesc& desc);

private:
    VulkanDevice* m_Device;
    VkPipeline m_Pipeline;
    std::unique_ptr<VulkanPipelineLayout> m_Layout;
    std::string m_DebugName;
};

/**
 * Pipeline cache for faster pipeline creation
 * Can be saved to disk and loaded for subsequent runs
 */
class VulkanPipelineCache {
public:
    VulkanPipelineCache(VulkanDevice* device);
    ~VulkanPipelineCache();

    // Disable copy, allow move
    VulkanPipelineCache(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;

    /**
     * Get the Vulkan pipeline cache handle
     */
    VkPipelineCache GetVkPipelineCache() const { return m_Cache; }

    /**
     * Save cache data to file
     */
    bool SaveToFile(const char* filepath);

    /**
     * Load cache data from file
     */
    bool LoadFromFile(const char* filepath);

    /**
     * Get cache data size
     */
    size_t GetCacheSize() const;

private:
    VulkanDevice* m_Device;
    VkPipelineCache m_Cache;
};

} // namespace CatEngine::RHI

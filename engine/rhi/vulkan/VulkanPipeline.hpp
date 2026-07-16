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
    VulkanPipelineLayout* GetLayout() const {
        // When the caller supplied its own layout we hold it in
        // m_BorrowedLayout (non-owning); otherwise we own a default in
        // m_Layout. Unify the two access paths so call-site code never
        // has to branch on ownership.
        return m_OwnsLayout ? m_Layout.get() : m_BorrowedLayout;
    }
    VkPipelineLayout GetVkPipelineLayout() const {
        return GetLayout()->GetVkPipelineLayout();
    }

private:
    void CreatePipeline(const PipelineDesc& desc, VkPipelineCache cache);
    VkPipelineLayout CreateDefaultLayout(const PipelineDesc& desc);

private:
    VulkanDevice* m_Device;
    VkPipeline m_Pipeline;
    // Owned default-layout case — populated by the single-arg constructor
    // (no caller-supplied layout). Destructor frees the wrapped
    // VkPipelineLayout via VulkanPipelineLayout's own destructor.
    std::unique_ptr<VulkanPipelineLayout> m_Layout;
    // Borrowed-layout case — non-owning pointer to a layout that the
    // caller created and will destroy itself. Set by the layout-aware
    // constructor; we read it only via GetLayout() / GetVkPipelineLayout().
    // Storing as a raw pointer (vs. std::unique_ptr) is intentional: a
    // unique_ptr would call delete on a layout the caller still believes
    // it owns, producing the double-free that motivated this split.
    VulkanPipelineLayout* m_BorrowedLayout = nullptr;
    // True when m_Layout owns the layout. False when m_BorrowedLayout
    // points to a caller-owned layout. Move semantics also propagate
    // this flag so a moved-from instance does not free a borrowed handle.
    bool m_OwnsLayout = true;
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
    VulkanPipelineLayout* GetLayout() const {
        return m_OwnsLayout ? m_Layout.get() : m_BorrowedLayout;
    }
    VkPipelineLayout GetVkPipelineLayout() const {
        return GetLayout()->GetVkPipelineLayout();
    }

private:
    void CreatePipeline(const ComputePipelineDesc& desc, VkPipelineCache cache);
    VkPipelineLayout CreateDefaultLayout(const ComputePipelineDesc& desc);

private:
    VulkanDevice* m_Device;
    VkPipeline m_Pipeline;
    // Same owned-vs-borrowed layout split as VulkanGraphicsPipeline (see
    // the comment block on that class above). The layout-aware constructor
    // populates m_BorrowedLayout with a caller-owned pointer that we must
    // NOT free in the destructor; the single-arg constructor populates the
    // owned m_Layout. m_OwnsLayout disambiguates the two cases at access /
    // destruction time so we neither leak a default nor double-free a
    // shared layout.
    std::unique_ptr<VulkanPipelineLayout> m_Layout;
    VulkanPipelineLayout* m_BorrowedLayout = nullptr;
    bool m_OwnsLayout = true;
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

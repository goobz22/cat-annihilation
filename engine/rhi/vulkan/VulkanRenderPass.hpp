#pragma once

#include "../RHIRenderPass.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;
class VulkanTextureView;

/**
 * Vulkan implementation of IRHIRenderPass
 * Defines the structure of a rendering operation including attachments and subpasses
 */
class VulkanRenderPass : public IRHIRenderPass {
public:
    VulkanRenderPass(VulkanDevice* device, const RenderPassDesc& desc);
    ~VulkanRenderPass() override;

    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

    // ========================================================================
    // IRHIRenderPass Interface
    // ========================================================================

    uint32_t GetAttachmentCount() const override { return static_cast<uint32_t>(m_attachments.size()); }
    const AttachmentDesc& GetAttachment(uint32_t index) const override { return m_attachments[index]; }
    uint32_t GetSubpassCount() const override { return static_cast<uint32_t>(m_subpasses.size()); }
    const SubpassDesc& GetSubpass(uint32_t index) const override { return m_subpasses[index]; }
    const char* GetDebugName() const override { return m_debugName.c_str(); }

    // ========================================================================
    // Vulkan-specific
    // ========================================================================

    VkRenderPass GetHandle() const { return m_renderPass; }

private:
    VulkanDevice* m_device;
    VkRenderPass m_renderPass;
    std::vector<AttachmentDesc> m_attachments;
    std::vector<SubpassDesc> m_subpasses;
    std::string m_debugName;
};

/**
 * Vulkan implementation of IRHIFramebuffer
 * Collection of texture attachments used with a render pass
 */
class VulkanFramebuffer : public IRHIFramebuffer {
public:
    VulkanFramebuffer(
        VulkanDevice* device,
        VulkanRenderPass* renderPass,
        IRHITextureView** attachments,
        uint32_t attachmentCount,
        uint32_t width,
        uint32_t height,
        uint32_t layers
    );
    ~VulkanFramebuffer() override;

    VulkanFramebuffer(const VulkanFramebuffer&) = delete;
    VulkanFramebuffer& operator=(const VulkanFramebuffer&) = delete;

    // ========================================================================
    // IRHIFramebuffer Interface
    // ========================================================================

    IRHIRenderPass* GetRenderPass() const override { return m_renderPass; }
    uint32_t GetWidth() const override { return m_width; }
    uint32_t GetHeight() const override { return m_height; }
    uint32_t GetLayers() const override { return m_layers; }
    uint32_t GetAttachmentCount() const override { return static_cast<uint32_t>(m_attachments.size()); }
    IRHITextureView* GetAttachment(uint32_t index) const override { return m_attachments[index]; }
    const char* GetDebugName() const override { return "VulkanFramebuffer"; }

    // ========================================================================
    // Vulkan-specific
    // ========================================================================

    VkFramebuffer GetHandle() const { return m_framebuffer; }

private:
    VulkanDevice* m_device;
    VkFramebuffer m_framebuffer;
    VulkanRenderPass* m_renderPass;
    std::vector<IRHITextureView*> m_attachments;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_layers;
};

/**
 * Helper functions for render pass operations
 */
namespace VulkanRenderPassHelper {

    /**
     * Convert RHI texture format to Vulkan format
     */
    VkFormat ToVulkanFormat(TextureFormat format);

    /**
     * Convert RHI load operation to Vulkan load operation
     */
    VkAttachmentLoadOp ToVulkanLoadOp(LoadOp loadOp);

    /**
     * Convert RHI store operation to Vulkan store operation
     */
    VkAttachmentStoreOp ToVulkanStoreOp(StoreOp storeOp);

    /**
     * Convert RHI pipeline bind point to Vulkan pipeline bind point
     */
    VkPipelineBindPoint ToVulkanPipelineBindPoint(PipelineBindPoint bindPoint);

    /**
     * Determine image layout for attachment based on format and usage
     */
    VkImageLayout DetermineAttachmentLayout(TextureFormat format, bool isDepthStencil);

} // namespace VulkanRenderPassHelper

} // namespace CatEngine::RHI

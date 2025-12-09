#include "VulkanRenderPass.hpp"
#include "VulkanDevice.hpp"
#include "VulkanTexture.hpp"
#include <stdexcept>
#include <algorithm>

namespace CatEngine::RHI {

// ============================================================================
// VulkanRenderPass Implementation
// ============================================================================

VulkanRenderPass::VulkanRenderPass(VulkanDevice* device, const RenderPassDesc& desc)
    : m_device(device)
    , m_renderPass(VK_NULL_HANDLE)
    , m_attachments(desc.attachments)
    , m_subpasses(desc.subpasses)
    , m_debugName(desc.debugName ? desc.debugName : "") {

    // Convert attachment descriptions
    std::vector<VkAttachmentDescription> vkAttachments(desc.attachments.size());
    for (size_t i = 0; i < desc.attachments.size(); ++i) {
        const auto& attachment = desc.attachments[i];

        vkAttachments[i].flags = 0;
        vkAttachments[i].format = VulkanRenderPassHelper::ToVulkanFormat(attachment.format);
        vkAttachments[i].samples = static_cast<VkSampleCountFlagBits>(attachment.sampleCount);
        vkAttachments[i].loadOp = VulkanRenderPassHelper::ToVulkanLoadOp(attachment.loadOp);
        vkAttachments[i].storeOp = VulkanRenderPassHelper::ToVulkanStoreOp(attachment.storeOp);
        vkAttachments[i].stencilLoadOp = VulkanRenderPassHelper::ToVulkanLoadOp(attachment.stencilLoadOp);
        vkAttachments[i].stencilStoreOp = VulkanRenderPassHelper::ToVulkanStoreOp(attachment.stencilStoreOp);

        // Determine layouts based on format
        bool isDepthStencil = (attachment.format == TextureFormat::D16_UNORM ||
                               attachment.format == TextureFormat::D32_SFLOAT ||
                               attachment.format == TextureFormat::D24_UNORM_S8_UINT ||
                               attachment.format == TextureFormat::D32_SFLOAT_S8_UINT);

        if (attachment.loadOp == LoadOp::Clear || attachment.loadOp == LoadOp::DontCare) {
            vkAttachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        } else {
            vkAttachments[i].initialLayout = VulkanRenderPassHelper::DetermineAttachmentLayout(attachment.format, isDepthStencil);
        }

        vkAttachments[i].finalLayout = VulkanRenderPassHelper::DetermineAttachmentLayout(attachment.format, isDepthStencil);
    }

    // Convert subpass descriptions
    std::vector<VkSubpassDescription> vkSubpasses(desc.subpasses.size());
    std::vector<std::vector<VkAttachmentReference>> colorAttachmentRefs(desc.subpasses.size());
    std::vector<std::vector<VkAttachmentReference>> inputAttachmentRefs(desc.subpasses.size());
    std::vector<VkAttachmentReference> depthAttachmentRefs(desc.subpasses.size());

    for (size_t i = 0; i < desc.subpasses.size(); ++i) {
        const auto& subpass = desc.subpasses[i];

        // Color attachments
        colorAttachmentRefs[i].resize(subpass.colorAttachments.size());
        for (size_t j = 0; j < subpass.colorAttachments.size(); ++j) {
            colorAttachmentRefs[i][j].attachment = subpass.colorAttachments[j].attachmentIndex;
            colorAttachmentRefs[i][j].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        // Input attachments
        inputAttachmentRefs[i].resize(subpass.inputAttachments.size());
        for (size_t j = 0; j < subpass.inputAttachments.size(); ++j) {
            inputAttachmentRefs[i][j].attachment = subpass.inputAttachments[j].attachmentIndex;
            inputAttachmentRefs[i][j].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        // Depth attachment
        VkAttachmentReference* pDepthAttachment = nullptr;
        if (subpass.depthStencilAttachment) {
            depthAttachmentRefs[i].attachment = subpass.depthStencilAttachment->attachmentIndex;
            depthAttachmentRefs[i].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            pDepthAttachment = &depthAttachmentRefs[i];
        }

        vkSubpasses[i].flags = 0;
        vkSubpasses[i].pipelineBindPoint = VulkanRenderPassHelper::ToVulkanPipelineBindPoint(subpass.bindPoint);
        vkSubpasses[i].inputAttachmentCount = static_cast<uint32_t>(inputAttachmentRefs[i].size());
        vkSubpasses[i].pInputAttachments = inputAttachmentRefs[i].empty() ? nullptr : inputAttachmentRefs[i].data();
        vkSubpasses[i].colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs[i].size());
        vkSubpasses[i].pColorAttachments = colorAttachmentRefs[i].empty() ? nullptr : colorAttachmentRefs[i].data();
        vkSubpasses[i].pResolveAttachments = nullptr;
        vkSubpasses[i].pDepthStencilAttachment = pDepthAttachment;
        vkSubpasses[i].preserveAttachmentCount = 0;
        vkSubpasses[i].pPreserveAttachments = nullptr;
    }

    // Create subpass dependencies
    std::vector<VkSubpassDependency> dependencies;
    if (desc.subpasses.size() > 1) {
        for (size_t i = 0; i < desc.subpasses.size() - 1; ++i) {
            VkSubpassDependency dependency{};
            dependency.srcSubpass = static_cast<uint32_t>(i);
            dependency.dstSubpass = static_cast<uint32_t>(i + 1);
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            dependencies.push_back(dependency);
        }
    }

    // External dependencies (before and after render pass)
    VkSubpassDependency externalDependencyBefore{};
    externalDependencyBefore.srcSubpass = VK_SUBPASS_EXTERNAL;
    externalDependencyBefore.dstSubpass = 0;
    externalDependencyBefore.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    externalDependencyBefore.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    externalDependencyBefore.srcAccessMask = 0;
    externalDependencyBefore.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    externalDependencyBefore.dependencyFlags = 0;
    dependencies.push_back(externalDependencyBefore);

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(vkAttachments.size());
    renderPassInfo.pAttachments = vkAttachments.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(vkSubpasses.size());
    renderPassInfo.pSubpasses = vkSubpasses.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(m_device->GetDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
    m_ownsHandle = true;
}

VulkanRenderPass::VulkanRenderPass(VulkanDevice* device, VkRenderPass existingRenderPass,
                                   const std::vector<AttachmentDesc>& attachments,
                                   const std::vector<SubpassDesc>& subpasses,
                                   const char* debugName)
    : m_device(device)
    , m_renderPass(existingRenderPass)
    , m_attachments(attachments)
    , m_subpasses(subpasses)
    , m_debugName(debugName ? debugName : "")
    , m_ownsHandle(false) {  // We do NOT own this handle
}

VulkanRenderPass::~VulkanRenderPass() {
    if (m_ownsHandle && m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->GetDevice(), m_renderPass, nullptr);
    }
}

// ============================================================================
// VulkanFramebuffer Implementation
// ============================================================================

VulkanFramebuffer::VulkanFramebuffer(
    VulkanDevice* device,
    VulkanRenderPass* renderPass,
    IRHITextureView** attachments,
    uint32_t attachmentCount,
    uint32_t width,
    uint32_t height,
    uint32_t layers
)
    : m_device(device)
    , m_framebuffer(VK_NULL_HANDLE)
    , m_renderPass(renderPass)
    , m_width(width)
    , m_height(height)
    , m_layers(layers) {

    // Store attachment pointers
    m_attachments.resize(attachmentCount);
    for (uint32_t i = 0; i < attachmentCount; ++i) {
        m_attachments[i] = attachments[i];
    }

    // Convert to Vulkan image views
    std::vector<VkImageView> vkAttachments(attachmentCount);
    for (uint32_t i = 0; i < attachmentCount; ++i) {
        vkAttachments[i] = static_cast<VulkanTextureView*>(attachments[i])->GetHandle();
    }

    // Create framebuffer
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass->GetHandle();
    framebufferInfo.attachmentCount = attachmentCount;
    framebufferInfo.pAttachments = vkAttachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = layers;

    if (vkCreateFramebuffer(m_device->GetDevice(), &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer");
    }
}

VulkanFramebuffer::~VulkanFramebuffer() {
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device->GetDevice(), m_framebuffer, nullptr);
    }
}

// ============================================================================
// VulkanRenderPassHelper Implementation
// ============================================================================

VkFormat VulkanRenderPassHelper::ToVulkanFormat(TextureFormat format) {
    switch (format) {
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

        default:
            return VK_FORMAT_UNDEFINED;
    }
}

VkAttachmentLoadOp VulkanRenderPassHelper::ToVulkanLoadOp(LoadOp loadOp) {
    switch (loadOp) {
        case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

VkAttachmentStoreOp VulkanRenderPassHelper::ToVulkanStoreOp(StoreOp storeOp) {
    switch (storeOp) {
        case StoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: return VK_ATTACHMENT_STORE_OP_STORE;
    }
}

VkPipelineBindPoint VulkanRenderPassHelper::ToVulkanPipelineBindPoint(PipelineBindPoint bindPoint) {
    switch (bindPoint) {
        case PipelineBindPoint::Graphics: return VK_PIPELINE_BIND_POINT_GRAPHICS;
        case PipelineBindPoint::Compute: return VK_PIPELINE_BIND_POINT_COMPUTE;
        default: return VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
}

VkImageLayout VulkanRenderPassHelper::DetermineAttachmentLayout(TextureFormat format, bool isDepthStencil) {
    if (isDepthStencil) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

} // namespace CatEngine::RHI

#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace CatEngine::RHI {

/**
 * Abstract interface for render passes
 * Defines the structure of a rendering operation including attachments and subpasses
 */
class IRHIRenderPass {
public:
    virtual ~IRHIRenderPass() = default;

    /**
     * Get number of attachments
     */
    virtual uint32_t GetAttachmentCount() const = 0;

    /**
     * Get attachment descriptor
     */
    virtual const AttachmentDesc& GetAttachment(uint32_t index) const = 0;

    /**
     * Get number of subpasses
     */
    virtual uint32_t GetSubpassCount() const = 0;

    /**
     * Get subpass descriptor
     */
    virtual const SubpassDesc& GetSubpass(uint32_t index) const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

/**
 * Abstract interface for framebuffers
 * Collection of texture attachments used with a render pass
 */
class IRHIFramebuffer {
public:
    virtual ~IRHIFramebuffer() = default;

    /**
     * Get the render pass this framebuffer is compatible with
     */
    virtual IRHIRenderPass* GetRenderPass() const = 0;

    /**
     * Get framebuffer width
     */
    virtual uint32_t GetWidth() const = 0;

    /**
     * Get framebuffer height
     */
    virtual uint32_t GetHeight() const = 0;

    /**
     * Get number of layers
     */
    virtual uint32_t GetLayers() const = 0;

    /**
     * Get number of attachments
     */
    virtual uint32_t GetAttachmentCount() const = 0;

    /**
     * Get attachment texture view
     */
    virtual class IRHITextureView* GetAttachment(uint32_t index) const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

} // namespace CatEngine::RHI

#pragma once

#include "../../rhi/RHI.hpp"
#include <string>

namespace CatEngine::Renderer {

// Forward declarations
class Renderer;

/**
 * Base interface for all render passes
 * Provides a common interface for setup, execution, and cleanup
 * Used by the RenderGraph system for automatic resource management
 */
class RenderPass {
public:
    virtual ~RenderPass() = default;

    /**
     * Setup the render pass - create pipelines, descriptor sets, etc.
     * Called once during initialization or when resources need to be recreated
     * @param rhi RHI device interface
     * @param renderer Reference to the main renderer
     */
    virtual void Setup(RHI::IRHI* rhi, Renderer* renderer) = 0;

    /**
     * Execute the render pass
     * Called every frame to record commands into the command buffer
     * @param commandBuffer Command buffer to record into
     * @param frameIndex Current frame index (for multi-buffering)
     */
    virtual void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) = 0;

    /**
     * Cleanup the render pass - destroy pipelines, descriptor sets, etc.
     * Called when shutting down or when resources need to be recreated
     */
    virtual void Cleanup() = 0;

    /**
     * Get the name of this render pass (for debugging)
     */
    virtual const char* GetName() const = 0;

    /**
     * Check if this pass is enabled
     */
    virtual bool IsEnabled() const { return m_Enabled; }

    /**
     * Enable or disable this pass
     */
    virtual void SetEnabled(bool enabled) { m_Enabled = enabled; }

protected:
    bool m_Enabled = true;
};

} // namespace CatEngine::Renderer

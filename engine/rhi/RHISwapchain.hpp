#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace CatEngine::RHI {

// Forward declarations
class IRHITexture;

/**
 * Abstract interface for swapchains
 * Manages presentation of rendered images to the screen
 */
class IRHISwapchain {
public:
    virtual ~IRHISwapchain() = default;

    /**
     * Get swapchain width
     */
    virtual uint32_t GetWidth() const = 0;

    /**
     * Get swapchain height
     */
    virtual uint32_t GetHeight() const = 0;

    /**
     * Get swapchain image format
     */
    virtual TextureFormat GetFormat() const = 0;

    /**
     * Get number of swapchain images
     */
    virtual uint32_t GetImageCount() const = 0;

    /**
     * Get swapchain image by index
     */
    virtual IRHITexture* GetImage(uint32_t index) const = 0;

    /**
     * Acquire next image for rendering
     * @param timeout Timeout in nanoseconds (UINT64_MAX = no timeout)
     * @return Image index or UINT32_MAX on failure/timeout
     */
    virtual uint32_t AcquireNextImage(uint64_t timeout = UINT64_MAX) = 0;

    /**
     * Present the current image to the screen
     * @return true on success, false if swapchain needs recreation
     */
    virtual bool Present() = 0;

    /**
     * Resize the swapchain
     * @param width New width
     * @param height New height
     * @return true on success
     */
    virtual bool Resize(uint32_t width, uint32_t height) = 0;

    /**
     * Check if vsync is enabled
     */
    virtual bool IsVSyncEnabled() const = 0;

    /**
     * Set vsync enabled/disabled
     * May require swapchain recreation
     */
    virtual void SetVSync(bool enabled) = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

} // namespace CatEngine::RHI

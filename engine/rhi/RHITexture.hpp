#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace CatEngine::RHI {

/**
 * Abstract interface for GPU textures/images
 * Represents 1D/2D/3D textures, texture arrays, cubemaps, etc.
 */
class IRHITexture {
public:
    virtual ~IRHITexture() = default;

    /**
     * Get texture type
     */
    virtual TextureType GetType() const = 0;

    /**
     * Get texture format
     */
    virtual TextureFormat GetFormat() const = 0;

    /**
     * Get texture usage flags
     */
    virtual TextureUsage GetUsage() const = 0;

    /**
     * Get texture width in pixels
     */
    virtual uint32_t GetWidth() const = 0;

    /**
     * Get texture height in pixels
     */
    virtual uint32_t GetHeight() const = 0;

    /**
     * Get texture depth (for 3D textures)
     */
    virtual uint32_t GetDepth() const = 0;

    /**
     * Get number of mip levels
     */
    virtual uint32_t GetMipLevels() const = 0;

    /**
     * Get number of array layers
     */
    virtual uint32_t GetArrayLayers() const = 0;

    /**
     * Get sample count (for multisampled textures)
     */
    virtual uint32_t GetSampleCount() const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

/**
 * Abstract interface for texture samplers
 * Defines how textures are sampled in shaders
 */
class IRHISampler {
public:
    virtual ~IRHISampler() = default;

    /**
     * Get sampler descriptor
     */
    virtual const SamplerDesc& GetDesc() const = 0;
};

/**
 * Abstract interface for texture views
 * Allows different interpretations of texture data
 */
class IRHITextureView {
public:
    virtual ~IRHITextureView() = default;

    /**
     * Get the underlying texture
     */
    virtual IRHITexture* GetTexture() const = 0;

    /**
     * Get view format (may differ from base texture format)
     */
    virtual TextureFormat GetFormat() const = 0;

    /**
     * Get base mip level
     */
    virtual uint32_t GetBaseMipLevel() const = 0;

    /**
     * Get number of mip levels in view
     */
    virtual uint32_t GetMipLevelCount() const = 0;

    /**
     * Get base array layer
     */
    virtual uint32_t GetBaseArrayLayer() const = 0;

    /**
     * Get number of array layers in view
     */
    virtual uint32_t GetArrayLayerCount() const = 0;
};

} // namespace CatEngine::RHI

#pragma once

#include "RHITypes.hpp"

namespace CatEngine::RHI {

/**
 * Abstract interface for graphics pipeline state
 * Encapsulates shaders, vertex input, rasterization, blending, etc.
 */
class IRHIPipeline {
public:
    virtual ~IRHIPipeline() = default;

    /**
     * Get pipeline bind point
     */
    virtual PipelineBindPoint GetBindPoint() const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

/**
 * Abstract interface for pipeline layout
 * Describes the interface between shader stages and resources
 */
class IRHIPipelineLayout {
public:
    virtual ~IRHIPipelineLayout() = default;

    /**
     * Get number of descriptor set layouts
     */
    virtual uint32_t GetDescriptorSetCount() const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

} // namespace CatEngine::RHI

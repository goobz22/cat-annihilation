#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace CatEngine::RHI {

// Forward declarations
class IRHIBuffer;
class IRHITexture;
class IRHITextureView;
class IRHISampler;

/**
 * Descriptor buffer info for uniform/storage buffers
 */
struct DescriptorBufferInfo {
    IRHIBuffer* buffer = nullptr;
    uint64_t offset = 0;
    uint64_t range = 0; // 0 = entire buffer
};

/**
 * Descriptor image info for sampled/storage images
 */
struct DescriptorImageInfo {
    IRHISampler* sampler = nullptr;
    IRHITextureView* imageView = nullptr;
};

/**
 * Write descriptor set operation
 */
struct WriteDescriptor {
    uint32_t binding = 0;
    uint32_t arrayElement = 0;
    uint32_t descriptorCount = 1;
    DescriptorType descriptorType = DescriptorType::UniformBuffer;

    // Use appropriate union member based on descriptorType
    union {
        const DescriptorImageInfo* imageInfo;
        const DescriptorBufferInfo* bufferInfo;
    };

    WriteDescriptor() : imageInfo(nullptr) {}
};

/**
 * Abstract interface for descriptor set layouts
 * Defines the structure and types of descriptors in a set
 */
class IRHIDescriptorSetLayout {
public:
    virtual ~IRHIDescriptorSetLayout() = default;

    /**
     * Get number of bindings
     */
    virtual uint32_t GetBindingCount() const = 0;

    /**
     * Get binding descriptor
     */
    virtual const DescriptorBinding& GetBinding(uint32_t index) const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

/**
 * Abstract interface for descriptor pools
 * Allocates descriptor sets
 */
class IRHIDescriptorPool {
public:
    virtual ~IRHIDescriptorPool() = default;

    /**
     * Allocate descriptor set from pool
     * @param layout Layout for the descriptor set
     * @return Allocated descriptor set or nullptr on failure
     */
    virtual IRHIDescriptorSet* AllocateDescriptorSet(IRHIDescriptorSetLayout* layout) = 0;

    /**
     * Free descriptor set back to pool
     */
    virtual void FreeDescriptorSet(IRHIDescriptorSet* descriptorSet) = 0;

    /**
     * Reset pool (frees all allocated sets)
     */
    virtual void Reset() = 0;
};

/**
 * Abstract interface for descriptor sets
 * Groups of descriptors (buffers, images, samplers) bound to shaders
 */
class IRHIDescriptorSet {
public:
    virtual ~IRHIDescriptorSet() = default;

    /**
     * Get the layout this set was allocated from
     */
    virtual IRHIDescriptorSetLayout* GetLayout() const = 0;

    /**
     * Update descriptor set with new bindings
     * @param writes Array of write operations
     * @param writeCount Number of write operations
     */
    virtual void Update(const WriteDescriptor* writes, uint32_t writeCount) = 0;
};

} // namespace CatEngine::RHI

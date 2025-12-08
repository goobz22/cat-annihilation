#pragma once

#include "../RHIDescriptorSet.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;

/**
 * Vulkan implementation of IRHIDescriptorSetLayout
 * Defines the structure and types of descriptors in a set
 */
class VulkanDescriptorSetLayout : public IRHIDescriptorSetLayout {
public:
    VulkanDescriptorSetLayout(VulkanDevice* device, const DescriptorSetLayoutDesc& desc);
    ~VulkanDescriptorSetLayout() override;

    VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
    VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;

    // ========================================================================
    // IRHIDescriptorSetLayout Interface
    // ========================================================================

    uint32_t GetBindingCount() const override { return static_cast<uint32_t>(m_bindings.size()); }
    const DescriptorBinding& GetBinding(uint32_t index) const override { return m_bindings[index]; }
    const char* GetDebugName() const override { return m_debugName.c_str(); }

    // ========================================================================
    // Vulkan-specific
    // ========================================================================

    VkDescriptorSetLayout GetHandle() const { return m_layout; }
    const std::vector<VkDescriptorSetLayoutBinding>& GetVulkanBindings() const { return m_vulkanBindings; }

private:
    VulkanDevice* m_device;
    VkDescriptorSetLayout m_layout;
    std::vector<DescriptorBinding> m_bindings;
    std::vector<VkDescriptorSetLayoutBinding> m_vulkanBindings;
    std::string m_debugName;
};

/**
 * Vulkan implementation of IRHIDescriptorPool
 * Allocates descriptor sets
 */
class VulkanDescriptorPool : public IRHIDescriptorPool {
public:
    VulkanDescriptorPool(VulkanDevice* device, uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& poolSizes);
    ~VulkanDescriptorPool() override;

    VulkanDescriptorPool(const VulkanDescriptorPool&) = delete;
    VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;

    // ========================================================================
    // IRHIDescriptorPool Interface
    // ========================================================================

    IRHIDescriptorSet* AllocateDescriptorSet(IRHIDescriptorSetLayout* layout) override;
    void FreeDescriptorSet(IRHIDescriptorSet* descriptorSet) override;
    void Reset() override;

    // ========================================================================
    // Vulkan-specific
    // ========================================================================

    VkDescriptorPool GetHandle() const { return m_pool; }

private:
    VulkanDevice* m_device;
    VkDescriptorPool m_pool;
    std::vector<class VulkanDescriptorSet*> m_allocatedSets;
};

/**
 * Vulkan implementation of IRHIDescriptorSet
 * Groups of descriptors (buffers, images, samplers) bound to shaders
 */
class VulkanDescriptorSet : public IRHIDescriptorSet {
public:
    VulkanDescriptorSet(VulkanDevice* device, VkDescriptorSet descriptorSet, VulkanDescriptorSetLayout* layout);
    ~VulkanDescriptorSet() override;

    VulkanDescriptorSet(const VulkanDescriptorSet&) = delete;
    VulkanDescriptorSet& operator=(const VulkanDescriptorSet&) = delete;

    // ========================================================================
    // IRHIDescriptorSet Interface
    // ========================================================================

    IRHIDescriptorSetLayout* GetLayout() const override { return m_layout; }

    void Update(const WriteDescriptor* writes, uint32_t writeCount) override;

    // ========================================================================
    // Vulkan-specific
    // ========================================================================

    VkDescriptorSet GetHandle() const { return m_descriptorSet; }

private:
    VulkanDevice* m_device;
    VkDescriptorSet m_descriptorSet;
    VulkanDescriptorSetLayout* m_layout;
};

/**
 * Helper functions for descriptor operations
 */
namespace VulkanDescriptorHelper {

    /**
     * Convert RHI descriptor type to Vulkan descriptor type
     */
    VkDescriptorType ToVulkanDescriptorType(DescriptorType type);

    /**
     * Convert RHI shader stage flags to Vulkan shader stage flags
     */
    VkShaderStageFlags ToVulkanShaderStageFlags(ShaderStage stages);

    /**
     * Create default pool sizes for common descriptor types
     * @param maxSets Maximum number of descriptor sets
     * @return Vector of pool sizes
     */
    std::vector<VkDescriptorPoolSize> CreateDefaultPoolSizes(uint32_t maxSets);

} // namespace VulkanDescriptorHelper

} // namespace CatEngine::RHI
